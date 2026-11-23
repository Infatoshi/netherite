/* Gate: native Nether and End raw worldgen against the oracle's dimension dumps
 * (make -C oracle run CLASS=DimDump ...). The manifest's "dim" picks the
 * pipeline and its "stages" names the stages in order; each stage runs on
 * native output from the stage before, not on the dump.
 *   nether: terrain  func_147419_a (density pass, lava ocean below y = 32)
 *           surface  func_147418_b (soul sand, gravel, lava ocean, bedrock)
 *           caves    MapGenCavesHell
 *   end:    biomes   the 256 sky ids func_147420_a is handed
 *           terrain  func_147420_a (density pass, end stone)
 *           surface  func_147421_b (a no-op on end stone, ported literally)
 * A block stage is ids (uint16 LE) then metas (uint8), 32768 cells, index
 * x << 11 | z << 7 | y; the nether has no structure generator here (the
 * fortress is applied after these stages and its chunks still compare). */
#include "dump.h"
#include "../engine/nether.h"
#include "../engine/end.h"

#define DIM_BLOCK_BYTES (DIM_CELLS * 3)

struct stage { char name[16]; gzFile in; int bad; };

static void report(struct stage *s, int cx, int cz, const char *what)
{
    if (!s->bad++) printf("FIRST DIFF %s chunk (%d,%d): %s\n", s->name, cx, cz, what);
}

/* want is ids then metas, as the dump stores a block stage. Metas are compared
 * everywhere, including under air, since the raw array keeps them. */
static int cmp_blocks(const unsigned char *want, const uint16_t *ids, const uint8_t *metas, char *what, size_t n)
{
    for (int i = 0; i < DIM_CELLS; ++i)
    {
        int id = want[2 * i] | want[2 * i + 1] << 8, meta = want[2 * DIM_CELLS + i];
        if (ids[i] != id || metas[i] != meta)
        {
            snprintf(what, n, "block (%d,%d,%d) want %d:%d got %d:%d", i >> 11, (i >> 7) & 15, i & 127, id, meta,
                     ids[i], metas[i]);
            return 1;
        }
    }
    return 0;
}

static int cmp_biomes(const unsigned char *want, const uint8_t *got, char *what, size_t n)
{
    for (int i = 0; i < 256; ++i)
        if (got[i] != want[i])
        {
            snprintf(what, n, "biome (%d,%d) want %d got %d", i & 15, i >> 4, want[i], got[i]);
            return 1;
        }
    return 0;
}

static const char *manifest_text(const char *dir, char *buf, size_t n)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    size_t got = fread(buf, 1, n - 1, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

/* The manifest's space-separated "stages" value, one name per slot. */
static int manifest_stages(const char *json, char names[][16], int max)
{
    const char *p = strstr(json, "\"stages\":\"");
    if (!p) { fprintf(stderr, "manifest: no stages\n"); exit(2); }
    p += strlen("\"stages\":\"");
    const char *e = strchr(p, '"');
    if (!e) { fprintf(stderr, "manifest: unterminated stages\n"); exit(2); }
    int n = 0;
    while (p < e && n < max)
    {
        while (p < e && *p == ' ') ++p;
        int len = 0;
        while (p < e && *p != ' ' && len < 15) names[n][len++] = *p++;
        names[n][len] = 0;
        if (len) ++n;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *dir = NULL;
    int ocx = 0, ocz = 0, only = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--only") && i + 1 < argc)
        {
            if (sscanf(argv[++i], "%d,%d", &ocx, &ocz) != 2) { fprintf(stderr, "bad --only CX,CZ\n"); return 2; }
            only = 1;
        }
        else if (!dir) dir = argv[i];
        else { fprintf(stderr, "usage: test_dims [--only CX,CZ] DUMP_DIR\n"); return 2; }
    }
    if (!dir) { fprintf(stderr, "usage: test_dims [--only CX,CZ] DUMP_DIR\n"); return 2; }

    struct dump d;
    dump_open(&d, dir);
    static char json[1 << 16];
    manifest_text(dir, json, sizeof json);
    int dim = (int)manifest_num(json, "dim");
    if (dim != -1 && dim != 1) { fprintf(stderr, "manifest: dim %d is neither the Nether nor the End\n", dim); return 2; }

    static struct stage stages[4];
    char names[4][16];
    int ns = manifest_stages(json, names, 4);
    for (int i = 0; i < ns; ++i)
    {
        snprintf(stages[i].name, sizeof stages[i].name, "%.15s", names[i]);
        stages[i].in = dump_stage(&d, names[i]);
        stages[i].bad = 0;
    }

    struct nether *n = malloc(sizeof *n);
    struct end *e = malloc(sizeof *e);
    if (dim == -1) nether_init(n, d.seed);
    else end_init(e, d.seed);

    static uint16_t blocks[DIM_CELLS];
    static uint8_t metas[DIM_CELLS];
    static unsigned char want[DIM_BLOCK_BYTES];
    static uint8_t biome_buf[256];
    char what[256];
    int chunks = 0;
    for (int cx = d.x0; cx <= d.x1; cx += d.step)
        for (int cz = d.z0; cz <= d.z1; cz += d.step)
        {
            if (!only || (cx == ocx && cz == ocz)) ++chunks;
            /* provideChunk reseeds the provider rand before the density pass */
            if (dim == -1)
                jr_seed(&n->rand,
                        (int64_t)((uint64_t)(int64_t)cx * 341873128712ULL + (uint64_t)(int64_t)cz * 132897987541ULL));
            else
                jr_seed(&e->rand,
                        (int64_t)((uint64_t)(int64_t)cx * 341873128712ULL + (uint64_t)(int64_t)cz * 132897987541ULL));

            for (int s = 0; s < ns; ++s)
            {
                struct stage *st = &stages[s];
                if (!strcmp(st->name, "biomes"))
                {
                    unsigned char wb[256];
                    dump_read(st->in, wb, sizeof wb);
                    if (only && (cx != ocx || cz != ocz)) continue;
                    end_biomes(biome_buf);
                    if (cmp_biomes(wb, biome_buf, what, sizeof what)) report(st, cx, cz, what);
                    continue;
                }
                dump_read(st->in, want, sizeof want);
                if (only && (cx != ocx || cz != ocz)) continue;
                if (!strcmp(st->name, "terrain"))
                {
                    if (dim == -1) nether_terrain(n, cx, cz, blocks);
                    else end_terrain(e, cx, cz, blocks);
                }
                else if (!strcmp(st->name, "surface"))
                {
                    if (dim == -1) nether_surface(n, cx, cz, blocks);
                    else end_surface(cx, cz, blocks);
                }
                else if (!strcmp(st->name, "caves"))
                    nether_caves(d.seed, cx, cz, blocks);
                else
                {
                    fprintf(stderr, "manifest: unknown stage %s\n", st->name);
                    return 2;
                }
                if (cmp_blocks(want, blocks, metas, what, sizeof what)) report(st, cx, cz, what);
            }
        }
    if (chunks == 0)
    {
        fprintf(stderr, "FAIL: no chunks compared in %s\n", d.dir);
        return 2;
    }

    int fail = 0;
    for (int i = 0; i < ns; ++i)
    {
        printf("%-8s %s: %d/%d chunks differ\n", stages[i].name, stages[i].bad ? "FAIL" : "PASS", stages[i].bad, chunks);
        fail |= stages[i].bad != 0;
    }
    printf("%s dim %d seed %lld (%s)\n", fail ? "FAIL" : "PASS", dim, (long long)d.seed, d.dir);
    return fail;
}
