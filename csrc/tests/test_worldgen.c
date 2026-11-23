/* Gate: native worldgen against the oracle's stage dumps, per chunk, in
 * pipeline order. For each stage it counts chunks that differ and prints the
 * first differing cell, so a failure names both the stage and the place.
 *   genbiomes  getBiomesForGeneration(cx*4-2, cz*4-2, 10, 10)
 *   biomes     loadBlockGeneratorData(cx*16, cz*16, 16, 16)
 *   terrain    ChunkProviderGenerate.func_147424_a (density pass)
 *   surface    func_147422_a (per-biome surface), ids and metas
 *   caves      MapGenCaves, ids and metas
 *   ravines    MapGenRavine, ids and metas: the raw chunk before structures
 *   light      Chunk.generateSkylightMap: height maps, section mask, sky light
 * Each stage runs on native output from the stage before, not on the dump.
 * The light stage arrived later than the shared dumps, so a dump without
 * light.bin.gz skips it. */
#include "dump.h"
#include "../engine/surface.h"
#include "../engine/carve.h"
#include "../engine/light.h"
#include "../engine/trace.h"

struct stage { const char *name; gzFile in; int bad; };

/* A stage that older dumps do not have: absent means skip, not fail. */
static gzFile stage_opt(const struct dump *d, const char *stage)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/%s.bin.gz", d->dir, stage);
    return gzopen(path, "rb");
}

static void report(struct stage *s, int cx, int cz, const char *what)
{
    if (!s->bad++) printf("FIRST DIFF %s chunk (%d,%d): %s\n", s->name, cx, cz, what);
}

/* want is ids (uint16 LE) then metas, as the dump stores a block stage. Metas
 * are compared everywhere, including under air, since the raw array keeps them. */
static int cmp_blocks(const unsigned char *want, const uint16_t *ids, const uint8_t *metas, char *what, size_t n)
{
    for (int i = 0; i < CHUNK_CELLS; ++i)
    {
        int id = want[2 * i] | want[2 * i + 1] << 8, meta = want[2 * CHUNK_CELLS + i];
        if (ids[i] != id || metas[i] != meta)
        {
            snprintf(what, n, "block (%d,%d,%d) want %d:%d got %d:%d", i >> 12, i & 255, (i >> 8) & 15, id, meta, ids[i], metas[i]);
            return 1;
        }
    }
    return 0;
}

/* The light record: 256 int32 heightMap, int32 heightMapMinimum, 256 int32
 * precipitationHeightMap, uint16 section mask, 65536 sky light bytes. */
#define LIGHT_BYTES (256 * 4 + 4 + 256 * 4 + 2 + CHUNK_CELLS)

static int32_t le32(const unsigned char *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static int cmp_light(const unsigned char *want, const struct light_map *m, char *what, size_t n)
{
    for (int i = 0; i < 256; ++i)
    {
        int32_t h = le32(want + 4 * i);
        if (m->height[i] != h)
        {
            snprintf(what, n, "heightMap (%d,%d) want %d got %d", i & 15, i >> 4, h, m->height[i]);
            return 1;
        }
    }
    int32_t min = le32(want + 1024);
    if (m->height_min != min)
    {
        snprintf(what, n, "heightMapMinimum want %d got %d", min, m->height_min);
        return 1;
    }
    for (int i = 0; i < 256; ++i)
    {
        int32_t p = le32(want + 1028 + 4 * i);
        if (m->precipitation[i] != p)
        {
            snprintf(what, n, "precipitationHeightMap (%d,%d) want %d got %d", i & 15, i >> 4, p, m->precipitation[i]);
            return 1;
        }
    }
    unsigned mask = want[2052] | want[2053] << 8;
    if (m->mask != mask)
    {
        snprintf(what, n, "section mask want 0x%04x got 0x%04x", mask, m->mask);
        return 1;
    }
    for (int i = 0; i < CHUNK_CELLS; ++i)
    {
        if (m->sky[i] != want[2054 + i])
        {
            snprintf(what, n, "sky (%d,%d,%d) want %d got %d", i >> 12, i & 255, (i >> 8) & 15, want[2054 + i], m->sky[i]);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = NULL, *trace_path = NULL;
    int ocx = 0, ocz = 0, only = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--only") && i + 1 < argc)
        {
            if (sscanf(argv[++i], "%d,%d", &ocx, &ocz) != 2) { fprintf(stderr, "bad --only CX,CZ\n"); return 2; }
            only = 1;
        }
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace_path = argv[++i];
        else if (!dir) dir = argv[i];
        else { fprintf(stderr, "usage: test_worldgen [--only CX,CZ] [--trace PATH] DUMP_DIR\n"); return 2; }
    }
    if (!dir) { fprintf(stderr, "usage: test_worldgen [--only CX,CZ] [--trace PATH] DUMP_DIR\n"); return 2; }
    trace_open(trace_path);
    struct dump d;
    dump_open(&d, dir);
    struct terrain *t = malloc(sizeof *t);
    terrain_init(t, d.seed);
    struct stage gen = {"genbiomes", dump_stage(&d, "genbiomes"), 0};
    struct stage full = {"biomes", dump_stage(&d, "biomes"), 0};
    struct stage ter = {"terrain", dump_stage(&d, "terrain"), 0};
    struct stage sur = {"surface", dump_stage(&d, "surface"), 0};
    struct stage cav = {"caves", dump_stage(&d, "caves"), 0};
    struct stage rav = {"ravines", dump_stage(&d, "ravines"), 0};
    struct stage lit = {"light", stage_opt(&d, "light"), 0};
    struct surface *surf = malloc(sizeof *surf);
    surface_init(surf, d.seed);
    static unsigned char want16[CHUNK_CELLS * 3];
    static uint16_t blocks[CHUNK_CELLS];
    static uint8_t metas[CHUNK_CELLS];
    static unsigned char want_light[LIGHT_BYTES];
    static struct light_map lm;
    char what[256];
    int chunks = 0;
    for (int cx = d.x0; cx <= d.x1; cx += d.step)
        for (int cz = d.z0; cz <= d.z1; cz += d.step)
        {
            unsigned char wg[100], wf[256];
            int g[100], f[256];
            if (only && (cx != ocx || cz != ocz))
            {
                /* not selected: consume one record per stage, in order, to keep every
                 * stream where the selected chunk leaves it, then skip the work */
                dump_read(gen.in, wg, sizeof wg);
                dump_read(full.in, wf, sizeof wf);
                dump_read(ter.in, want16, CHUNK_CELLS * 2);
                dump_read(sur.in, want16, CHUNK_CELLS * 3);
                dump_read(cav.in, want16, CHUNK_CELLS * 3);
                dump_read(rav.in, want16, CHUNK_CELLS * 3);
                if (lit.in) dump_read(lit.in, want_light, sizeof want_light);
                continue;
            }
            ++chunks;
            dump_read(gen.in, wg, sizeof wg);
            dump_read(full.in, wf, sizeof wf);
            biomes_gen(&t->layers, cx * 4 - 2, cz * 4 - 2, 10, 10, g);
            biomes_full(&t->layers, cx * 16, cz * 16, 16, 16, f);
            for (int i = 0; i < 100; ++i)
                if (g[i] != wg[i])
                {
                    snprintf(what, sizeof what, "cell (%d,%d) want %d got %d", i % 10, i / 10, wg[i], g[i]);
                    report(&gen, cx, cz, what);
                    break;
                }
            for (int i = 0; i < 256; ++i)
                if (f[i] != wf[i])
                {
                    snprintf(what, sizeof what, "block (%d,%d) want %d got %d", i % 16, i / 16, wf[i], f[i]);
                    report(&full, cx, cz, what);
                    break;
                }

            dump_read(ter.in, want16, CHUNK_CELLS * 2);
            terrain_density(t, cx, cz, blocks);
            for (int i = 0; i < CHUNK_CELLS; ++i)
            {
                int want = want16[2 * i] | want16[2 * i + 1] << 8;
                if (blocks[i] != want)
                {
                    snprintf(what, sizeof what, "block (%d,%d,%d) want %d got %d", i >> 12, i & 255, (i >> 8) & 15, want, blocks[i]);
                    report(&ter, cx, cz, what);
                    break;
                }
            }

            /* provideChunk seeds the provider rand before the density pass, which draws nothing */
            jr_seed(&t->rand, (int64_t)((uint64_t)(int64_t)cx * 341873128712ULL + (uint64_t)(int64_t)cz * 132897987541ULL));
            memset(metas, 0, sizeof metas);
            surface_pass(t, surf, cx, cz, f, blocks, metas);
            dump_read(sur.in, want16, CHUNK_CELLS * 3);
            if (cmp_blocks(want16, blocks, metas, what, sizeof what)) report(&sur, cx, cz, what);

            caves_pass(d.seed, cx, cz, f, surf->top, blocks);
            dump_read(cav.in, want16, CHUNK_CELLS * 3);
            if (cmp_blocks(want16, blocks, metas, what, sizeof what)) report(&cav, cx, cz, what);

            ravines_pass(d.seed, cx, cz, f, surf->top, blocks);
            dump_read(rav.in, want16, CHUNK_CELLS * 3);
            if (cmp_blocks(want16, blocks, metas, what, sizeof what)) report(&rav, cx, cz, what);

            if (lit.in)
            {
                light_pass(blocks, &lm);
                dump_read(lit.in, want_light, sizeof want_light);
                if (cmp_light(want_light, &lm, what, sizeof what)) report(&lit, cx, cz, what);
            }
        }
    trace_close();
    if (chunks == 0)
    {
        fprintf(stderr, "FAIL: no chunks compared in %s\n", d.dir);
        return 2;
    }
    struct stage *all[] = {&gen, &full, &ter, &sur, &cav, &rav, &lit};
    int fail = 0;
    for (int i = 0; i < 7; ++i)
    {
        if (!all[i]->in)
        {
            printf("%-10s skipped (no light.bin.gz)\n", all[i]->name);
            continue;
        }
        printf("%-10s %s: %d/%d chunks differ\n", all[i]->name, all[i]->bad ? "FAIL" : "PASS", all[i]->bad, chunks);
        fail |= all[i]->bad != 0;
    }
    printf("%s seed %lld (%s)\n", fail ? "FAIL" : "PASS", (long long)d.seed, d.dir);
    return fail;
}
