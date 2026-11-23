/* worldgen_check: the CUDA chunk generator (host.cu and its stages) against the C engine's
 * provide_chunk, byte for byte, for every chunk of the given oracle chunk
 * dumps (out/java/chunks/<name>: the manifest's region) and seed-world spawn
 * areas (out/java/seedworld/<name>: the chunks the C seed-world build loads,
 * the overworld only).
 *
 * Each batch runs the GPU stages one at a time and compares each stage with
 * the C function that computes it, so a difference names its stage:
 *   genbiomes  biomes_gen 10x10     field    terrain_field (825 doubles, bits)
 *   biomes     biomes_full 16x16    terrain  terrain_density ids
 *   surface    surface_pass ids and metas
 *   caves      caves_pass ids       ravines  ravines_pass ids
 *   final      provide_chunk: the chunk's ids, metas and biome array
 *   generate   the same batch through worldgen_generate (no stage copies)
 *
 *   worldgen_check [--batch N] [--no-stages] [--negative=STAGE] [--store DIR KEY]
 *                 DUMP_DIR... [--spawn SEEDWORLD_DIR]...
 * --store keeps C's side in DIR under KEY (ref.h): a later run with the
 * same KEY and chunks runs C only where the device differs.
 * --negative=layers|field|surface|caves|ravines perturbs one GPU constant and
 * passes only when the first failing stage is the perturbed one. */
#define _POSIX_C_SOURCE 200809L
#include "worldgen.h"
#include "ref.h"
#include "../../engine/carve.h"
#include "../../engine/chunkgen.h"
#include "../../engine/jmath.h"
#include "../../engine/layers.h"
#include "../../engine/seedworld.h"
#include "../../engine/surface.h"
#include "../../engine/terrain.h"
#include "../../engine/world.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct req { int64_t seed; int cx, cz; };

static struct req *reqs;
static size_t nreq, capreq;

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

static const char *store_dir, *store_key;

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory (%zu bytes)\n", n); exit(2); }
    return p;
}

static int req_cmp(const void *a, const void *b)
{
    const struct req *x = a, *y = b;
    if (x->seed != y->seed) return x->seed < y->seed ? -1 : 1;
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

static size_t add_region(const char *dir)
{
    const char *m = read_manifest(dir);
    int64_t seed = num(m, "seed");
    int x0 = (int)num(m, "x0"), z0 = (int)num(m, "z0"), x1 = (int)num(m, "x1"), z1 = (int)num(m, "z1");
    int step = has_key(m, "step") ? (int)num(m, "step") : 1;
    size_t before = nreq;
    for (int cx = x0; cx <= x1; cx += step)
        for (int cz = z0; cz <= z1; cz += step) add_req(seed, cx, cz);
    return nreq - before;
}

/* The chunks the seed-world build generates: the 625 around the spawn point,
 * the ones population and the spawn search load, in load order. */
static size_t add_spawn(const char *dir)
{
    const char *m = read_manifest(dir);
    const char *dim = strstr(m, "\"dim\":");
    if (dim && strncmp(dim + 6, "null", 4) != 0)
    {
        printf("spawn %s: skipped (not the overworld)\n", dir);
        return 0;
    }
    int64_t seed = num(m, "seed");
    uint64_t *keys;
    size_t n = gr_spawn_load(store_dir, store_key, seed, 0, &keys);
    if (n == 0)
    {
        struct seedworld *sw = calloc(1, sizeof *sw);
        if (!sw) { perror("calloc"); exit(2); }
        seedworld_init(sw, seed);
        seedworld_build(sw);
        const struct world *w = &sw->p.world;
        n = w->lon;
        keys = xmalloc(sizeof *keys * (n ? n : 1));
        for (size_t i = 0; i < n; ++i) keys[i] = (uint64_t)w->load_order[i];
        seedworld_free(sw);
        free(sw);
        gr_spawn_save(store_dir, store_key, seed, 0, keys, n);
    }
    for (size_t i = 0; i < n; ++i)
        add_req(seed, (int32_t)(uint32_t)(keys[i] & 0xffffffffu), (int32_t)(uint32_t)(keys[i] >> 32));
    free(keys);
    printf("spawn seed %lld: %zu chunks\n", (long long)seed, n);
    return n;
}

enum { S_SIN, S_GEN, S_FULL, S_FIELD, S_TERRAIN, S_SURFACE, S_CAVES, S_RAVINES, S_FINAL, S_GENERATE, S_N };
static const char *stage_name[S_N] = {"sin", "genbiomes", "biomes", "field", "terrain", "surface", "caves", "ravines",
                                      "final", "generate"};
static long bad[S_N], checked[S_N];
static char first[S_N][300];

static void diff(int s, const struct req *r, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void diff(int s, const struct req *r, const char *fmt, ...)
{
    if (!bad[s]++)
    {
        int o = snprintf(first[s], sizeof first[s], "seed %lld chunk (%d,%d): ", (long long)r->seed, r->cx, r->cz);
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(first[s] + o, sizeof first[s] - (size_t)o, fmt, ap);
        va_end(ap);
    }
}

/* first differing cell of a block stage, or -1 */
static int cmp_ids(const uint8_t *gpu, const uint16_t *c)
{
    for (int i = 0; i < WORLDGEN_CELLS; ++i)
        if (gpu[i] != c[i]) return i;
    return -1;
}

static int cmp_metas(const uint8_t *gpu, const uint8_t *c)
{
    for (int i = 0; i < WORLDGEN_CELLS; ++i)
        if (gpu[i] != c[i]) return i;
    return -1;
}

static void check_blocks(int s, const struct req *r, const uint8_t *gids, const uint16_t *cids,
                         const uint8_t *gmetas, const uint8_t *cmetas)
{
    ++checked[s];
    int i = cmp_ids(gids, cids);
    if (i >= 0)
    {
        diff(s, r, "id (%d,%d,%d) want %d got %d", i >> 12, i & 255, (i >> 8) & 15, cids[i], gids[i]);
        return;
    }
    if (!gmetas) return;
    i = cmp_metas(gmetas, cmetas);
    if (i >= 0) diff(s, r, "meta (%d,%d,%d) want %d got %d", i >> 12, i & 255, (i >> 8) & 15, cmetas[i], gmetas[i]);
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
    int batch = 1024, stages = 1, negative = WORLDGEN_NEG_NONE;
    size_t from_regions = 0, from_spawn = 0;
    int nregions = 0, nspawn = 0;
    const char **spawns = xmalloc(sizeof *spawns * (size_t)argc);
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--batch") && i + 1 < argc) batch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-stages")) stages = 0;
        else if (!strcmp(argv[i], "--spawn") && i + 1 < argc) spawns[nspawn++] = argv[++i];
        else if (!strcmp(argv[i], "--store") && i + 2 < argc) { store_dir = argv[i + 1]; store_key = argv[i + 2]; i += 2; }
        else if (!strncmp(argv[i], "--negative=", 11))
        {
            const char *s = argv[i] + 11;
            negative = !strcmp(s, "layers") ? WORLDGEN_NEG_LAYERS : !strcmp(s, "field") ? WORLDGEN_NEG_FIELD
                     : !strcmp(s, "surface") ? WORLDGEN_NEG_SURFACE : !strcmp(s, "caves") ? WORLDGEN_NEG_CAVES
                     : !strcmp(s, "ravines") ? WORLDGEN_NEG_RAVINES : -1;
            if (negative < 0) { fprintf(stderr, "unknown negative %s\n", s); return 2; }
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "usage: worldgen_check [--batch N] [--no-stages] [--negative=STAGE] [--store DIR KEY] DUMP_DIR... [--spawn DIR]...\n"); return 2; }
        else { from_regions += add_region(argv[i]); ++nregions; }
    }
    for (int i = 0; i < nspawn; ++i) from_spawn += add_spawn(spawns[i]);
    free(spawns);
    if (batch < 1) batch = 1;
    if (nreq == 0) { fprintf(stderr, "FAIL: no chunks to generate\n"); return 2; }
    size_t listed = nreq;
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
    printf("%zu chunks (%zu listed: %zu from %d regions, %zu from %d spawn areas), %d seeds, batch %d%s\n",
           nreq, listed, from_regions, nregions, from_spawn, nspawn, nseeds, batch, stages ? ", staged" : "");

    /* C's stage hashes: genbiomes, biomes, field, terrain, surface, caves,
     * ravines (staged runs), final */
    enum { GR_NST = 8 };
    struct gr_store ref;
    gr_open(&ref, store_dir, store_key, stages ? "gen-staged" : "gen-final", reqs, sizeof *reqs, nreq, GR_NST);

    struct worldgen *g = worldgen_create(batch, nseeds, negative);
    if (!g) return 2;
    CK(worldgen_set_seeds(g, seeds, nseeds));
    printf("layer kernel shared memory: %d bytes per chunk\n", worldgen_layer_shared_bytes(g));

    /* MathHelper's table: the GPU builds it with CUDA's sin */
    {
        float *tab = xmalloc(sizeof(float) * 65536);
        CK(worldgen_fetch_sin(g, tab));
        jmath_init();
        checked[S_SIN] = 1;
        for (int i = 0; i < 65536; ++i)
            if (memcmp(&tab[i], &MH_SIN[i], sizeof(float)) != 0)
            {
                bad[S_SIN] = 1;
                snprintf(first[S_SIN], sizeof first[S_SIN], "entry %d want %a got %a", i, (double)MH_SIN[i], (double)tab[i]);
                break;
            }
        free(tab);
    }

    /* the C side: one generator per seed for provide_chunk and one for the stages */
    struct chunkgen **cg = xmalloc(sizeof *cg * (size_t)nseeds);
    struct terrain **ter = xmalloc(sizeof *ter * (size_t)nseeds);
    struct surface **sur = xmalloc(sizeof *sur * (size_t)nseeds);
    for (int i = 0; i < nseeds; ++i)
    {
        cg[i] = xmalloc(sizeof **cg);
        chunkgen_init(cg[i], seeds[i]);
        if (stages)
        {
            ter[i] = xmalloc(sizeof **ter);
            terrain_init(ter[i], seeds[i]);
            sur[i] = xmalloc(sizeof **sur);
            surface_init(sur[i], seeds[i]);
        }
    }

    size_t B = (size_t)batch, C = WORLDGEN_CELLS;
    struct worldgen_req *greq = xmalloc(sizeof *greq * B);
    uint8_t *hgen = xmalloc(100 * B), *hfull = xmalloc(256 * B);
    uint8_t *hids = xmalloc(C * B), *hmetas = xmalloc(C * B);
    uint8_t *pids = xmalloc(C * B), *pmetas = xmalloc(C * B), *pgen = xmalloc(100 * B), *pfull = xmalloc(256 * B);
    double *hfield = NULL;
    uint8_t *hter = NULL, *hsur = NULL, *hsurm = NULL, *hcav = NULL, *hrav = NULL, *htops = NULL;
    uint64_t *hrands = NULL;
    if (stages)
    {
        htops = xmalloc(256 * B);
        hrands = xmalloc(sizeof *hrands * B);
        hfield = xmalloc(sizeof(double) * 825 * B);
        hter = xmalloc(C * B);
        hsur = xmalloc(C * B);
        hsurm = xmalloc(C * B);
        hcav = xmalloc(C * B);
        hrav = xmalloc(C * B);
    }
    struct chunk *ch = chunk_new();
    static uint16_t ch_ids[WORLDGEN_CELLS];
    static uint8_t ch_metas[WORLDGEN_CELLS];
    static uint16_t blocks[WORLDGEN_CELLS];
    static uint8_t metas[WORLDGEN_CELLS];
    double gpu_s = 0.0, cpu_s = 0.0;
    int batches = 0;

    for (size_t start = 0; start < nreq; start += B)
    {
        size_t n = nreq - start < B ? nreq - start : B;
        const struct req *R = reqs + start;
        int si = 0;
        for (size_t k = 0; k < n; ++k)
        {
            while (seeds[si] != R[k].seed) si = (si + 1) % nseeds;
            greq[k] = (struct worldgen_req){si, R[k].cx, R[k].cz};
        }
        CK(worldgen_submit(g, greq, (int)n));
        if (stages)
        {
            CK(worldgen_run(g, WORLDGEN_LAYERS));
            CK(worldgen_fetch_biomes(g, hgen, hfull));
            CK(worldgen_run(g, WORLDGEN_DENSITY));
            CK(worldgen_fetch_field(g, hfield));
            CK(worldgen_fetch_ids(g, hter));
            CK(worldgen_run(g, WORLDGEN_SURFACE));
            CK(worldgen_fetch_ids(g, hsur));
            CK(worldgen_fetch_metas(g, hsurm));
            CK(worldgen_fetch_tops(g, htops, hrands));
            CK(worldgen_run(g, WORLDGEN_CAVES));
            CK(worldgen_fetch_ids(g, hcav));
            CK(worldgen_run(g, WORLDGEN_RAVINES));
            CK(worldgen_fetch_ids(g, hrav));
            CK(worldgen_run(g, WORLDGEN_CONSTRUCT));
            CK(worldgen_fetch_ids(g, hids));
            CK(worldgen_fetch_metas(g, hmetas));
        }

        /* the product path: every stage back to back, then one copy */
        double t0 = now();
        CK(worldgen_generate(g));
        gpu_s += now() - t0;
        CK(worldgen_fetch_ids(g, pids));
        CK(worldgen_fetch_metas(g, pmetas));
        CK(worldgen_fetch_biomes(g, pgen, pfull));
        ++batches;

        for (size_t k = 0; k < n; ++k)
        {
            const struct req *r = &R[k];
            int s = greq[k].seed_index;
            const uint8_t *fin_ids = (stages ? hids : pids) + C * k, *fin_metas = (stages ? hmetas : pmetas) + C * k;
            const uint8_t *fin_full = (stages ? hfull : pfull) + 256 * k;
            uint64_t dh[GR_NST] = {0}, ch_h[GR_NST] = {0};
            if (stages)
            {
                dh[0] = gr_u8(0, hgen + 100 * k, 100);
                dh[1] = gr_u8(0, hfull + 256 * k, 256);
                dh[2] = gr_f64(0, hfield + 825 * k, 825);
                dh[3] = gr_u8(0, hter + C * k, C);
                /* the surface pass's state too: each column's topBlock entry
                 * (what the carvers read) and the provider rand after it */
                uint8_t st[256 + 8];
                for (int i = 0; i < 256; ++i) st[i] = htops[256 * k + hfull[256 * k + i]];
                memcpy(st + 256, &hrands[k], 8);
                dh[4] = gr_u8(gr_u8(gr_u8(0, hsur + C * k, C), hsurm + C * k, C), st, sizeof st);
                dh[5] = gr_u8(0, hcav + C * k, C);
                dh[6] = gr_u8(0, hrav + C * k, C);
            }
            dh[7] = gr_u8(gr_u8(gr_u8(0, fin_ids, C), fin_metas, C), fin_full, 256);
            if (gr_match(&ref, start + k, dh))
            {
                /* every stage is C's stored result: no C for this chunk */
                if (stages)
                    for (int st = S_GEN; st <= S_RAVINES; ++st) ++checked[st];
                ++checked[S_FINAL];
                goto product;
            }
            if (stages)
            {
                struct terrain *t = ter[s];
                int gb[100], fb[256];
                double fld[825];
                biomes_gen(&t->layers, r->cx * 4 - 2, r->cz * 4 - 2, 10, 10, gb);
                biomes_full(&t->layers, r->cx * 16, r->cz * 16, 16, 16, fb);
                ++checked[S_GEN];
                for (int i = 0; i < 100; ++i)
                    if (hgen[100 * k + i] != gb[i])
                    {
                        diff(S_GEN, r, "cell (%d,%d) want %d got %d", i % 10, i / 10, gb[i], hgen[100 * k + i]);
                        break;
                    }
                ++checked[S_FULL];
                for (int i = 0; i < 256; ++i)
                    if (hfull[256 * k + i] != fb[i])
                    {
                        diff(S_FULL, r, "column (%d,%d) want %d got %d", i % 16, i / 16, fb[i], hfull[256 * k + i]);
                        break;
                    }
                ch_h[0] = gr_i32(0, gb, 100);
                ch_h[1] = gr_i32(0, fb, 256);
                terrain_field(t, r->cx * 4, r->cz * 4, gb, fld);
                ch_h[2] = gr_f64(0, fld, 825);
                ++checked[S_FIELD];
                for (int i = 0; i < 825; ++i)
                    if (memcmp(&hfield[825 * k + i], &fld[i], sizeof(double)) != 0)
                    {
                        diff(S_FIELD, r, "sample %d (i %d j %d y %d) want %a got %a", i, i / 165, i / 33 % 5, i % 33,
                             fld[i], hfield[825 * k + i]);
                        break;
                    }
                terrain_density(t, r->cx, r->cz, blocks);
                ch_h[3] = gr_u16(0, blocks, C);
                check_blocks(S_TERRAIN, r, hter + C * k, blocks, NULL, NULL);
                jr_seed(&t->rand, (int64_t)((uint64_t)(int64_t)r->cx * 341873128712ULL +
                                            (uint64_t)(int64_t)r->cz * 132897987541ULL));
                memset(metas, 0, sizeof metas);
                surface_pass(t, sur[s], r->cx, r->cz, fb, blocks, metas);
                uint8_t st[256 + 8];
                for (int i = 0; i < 256; ++i) st[i] = (uint8_t)sur[s]->top[fb[i]];
                memcpy(st + 256, &t->rand.seed, 8);
                ch_h[4] = gr_u8(gr_u8(gr_u16(0, blocks, C), metas, C), st, sizeof st);
                check_blocks(S_SURFACE, r, hsur + C * k, blocks, hsurm + C * k, metas);
                for (int i = 0; i < 256; ++i)
                    if (htops[256 * k + hfull[256 * k + i]] != st[i])
                    {
                        diff(S_SURFACE, r, "column (%d,%d)'s topBlock entry (biome %d) want %d got %d", i & 15, i >> 4, fb[i],
                             st[i], htops[256 * k + hfull[256 * k + i]]);
                        break;
                    }
                if (hrands[k] != t->rand.seed)
                    diff(S_SURFACE, r, "provider rand want %llx got %llx", (unsigned long long)t->rand.seed,
                         (unsigned long long)hrands[k]);
                caves_pass(seeds[s], r->cx, r->cz, fb, sur[s]->top, blocks);
                ch_h[5] = gr_u16(0, blocks, C);
                check_blocks(S_CAVES, r, hcav + C * k, blocks, NULL, NULL);
                ravines_pass(seeds[s], r->cx, r->cz, fb, sur[s]->top, blocks);
                ch_h[6] = gr_u16(0, blocks, C);
                check_blocks(S_RAVINES, r, hrav + C * k, blocks, NULL, NULL);
            }

            double c0 = now();
            provide_chunk(cg[s], r->cx, r->cz, ch);
            cpu_s += now() - c0;
            chunk_cells_out(ch, ch_ids, ch_metas, NULL, NULL);
            check_blocks(S_FINAL, r, fin_ids, ch_ids, fin_metas, ch_metas);
            for (int i = 0; i < 256; ++i)
                if (fin_full[i] != ch->biome[i])
                {
                    diff(S_FINAL, r, "biome (%d,%d) want %d got %d", i & 15, i >> 4, ch->biome[i], fin_full[i]);
                    break;
                }
            ch_h[7] = gr_u8(gr_u8(gr_u16(0, ch_ids, C), ch_metas, C), ch->biome, 256);
            gr_made(&ref, start + k, ch_h);

        product:

            if (stages)
            {
                ++checked[S_GENERATE];
                if (memcmp(pids + C * k, hids + C * k, C) || memcmp(pmetas + C * k, hmetas + C * k, C)
                    || memcmp(pfull + 256 * k, hfull + 256 * k, 256) || memcmp(pgen + 100 * k, hgen + 100 * k, 100))
                    diff(S_GENERATE, r, "worldgen_generate differs from the staged run");
            }
        }
    }

    gr_close(&ref);
    unsigned errs = worldgen_errors(g);
    int fail = errs != 0, first_bad = -1;
    for (int s = 0; s < S_N; ++s)
    {
        if (!checked[s])
        {
            printf("%-10s skipped\n", stage_name[s]);
            continue;
        }
        printf("%-10s %s: %ld/%ld differ%s%s\n", stage_name[s], bad[s] ? "FAIL" : "PASS", bad[s], checked[s],
               bad[s] ? "; first " : "", bad[s] ? first[s] : "");
        if (bad[s] && first_bad < 0) first_bad = s;
        fail |= bad[s] != 0;
    }
    printf("device errors: 0x%x\n", errs);
    printf("timing (GPU shared with a running vLLM server, not representative): worldgen_generate %.3f s over %d batches, "
           "%.0f chunks/s; C provide_chunk one core %.3f s, %.0f chunks/s\n",
           gpu_s, batches, (double)nreq / gpu_s, cpu_s, (double)nreq / cpu_s);
    worldgen_free(g);

    if (negative != WORLDGEN_NEG_NONE)
    {
        static const int expect[] = {0, S_FULL, S_FIELD, S_SURFACE, S_CAVES, S_RAVINES};
        int want = expect[negative];
        int ok = first_bad == want && errs == 0;
        printf("NEGATIVE %s: first failing stage %s: %s\n", stage_name[want], first_bad >= 0 ? stage_name[first_bad] : "none",
               ok ? "CAUGHT" : "NOT CAUGHT");
        return ok ? 0 : 1;
    }
    printf("%s %zu chunks\n", fail ? "FAIL" : "PASS", nreq);
    return fail;
}
