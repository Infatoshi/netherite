/* Gate: the native populate driver against the oracle's PopulateProbe, one
 * probe directory per run (out/java/populate/<name>), call by call in record
 * order. Per call it checks the four structure intersect counts, every stage
 * marker's Random state and write index, and the write stream between
 * markers; the tile entity deltas are checked at the six boundaries; after
 * all calls the pending scheduled ticks and the final region with light and
 * height maps.
 *
 * The four structure generators' blocks are their native halves
 * (mineshaft_blocks.c and the three beside it, over structure_blocks.c's
 * sc_generate), driven out of the driver's own structure maps in
 * java.util.HashMap order; the oracle's recorded writes are compared against
 * what the native generation wrote, and nothing is replayed. The oracle's
 * recorded chunk loads are a check too: every chunk the oracle's call
 * generated, the native generation must have generated at the same point.
 *
 * --negative=count and --negative=snow plant the two decorator and freeze
 * mutations, --negative=maporder walks the structure maps in the walk's own
 * order instead of the HashMap's, and --negative=village drops the village
 * gate the lakes read; each expects the failure that names the call and
 * stage. The manifest's dim picks the driver: 0 the overworld populate
 * driver, -1 ChunkProviderHell's populate (csrc/engine/populate_nether.c),
 * 1 the End's BiomeEndDecorator.
 *
 * --negative=helreseed and --negative=spike plant the two mutations the Nether
 * and End report asks for (hellRNG reseeded per populate call, the spike
 * chance) and expect the failure that names the call. */
#include "probe.h"
#include "../engine/env.h"
#include "../engine/gunzip.h"

#include "../engine/decorator.h"
#include "../engine/features_nether.h"
#include "../engine/jrand.h"
#include "../engine/populate.h"
#include "../engine/populate_nether.h"
#include "../engine/randomtick.h"
#include "../engine/ticks.h"
#include "../engine/trace.h"

#include <stdarg.h>
#include <string.h>



static const char *dir;
static char dirbuf[1100];

static int neg_snow, neg_count, neg_muttree, neg_hellreseed, neg_spike;
static int dim;
static long long compared_writes;
static long long structure_steps;   /* every start generation the walk ran */

static char *dup_str(const char *s)
{
    size_t n = strlen(s);
    char *out = malloc(n + 1);
    memcpy(out, s, n + 1);
    return out;
}

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL %s: ", dir);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    trace_close();
    exit(1);
}

/* The oracle's raw chunk replay. The structure generators carve blocks into
 * chunks during provideChunk (MapGenBase's func_151539_a, a chunk-generation
 * stage), before any recording hook, so the native raw chunks differ from the
 * oracle's wherever a mineshaft, village, stronghold or temple crosses one;
 * that carving is not ported, and the chunk-generation lane owns it. The file
 * is one entry per generated chunk in load order: cx int32, cz int32,
 * Probe.CHUNK_BYTES of chunk bytes, 256 updateSkylightColumns bytes, one
 * isGapLightingUpdated byte. */
static unsigned char *rawbin;
static size_t rawlen;

struct raw_ent { int cx, cz; const unsigned char *p; };
static struct raw_ent *rawents;
static int nraw;

/* an open-addressing map (cx, cz) -> the entry index */
static int32_t *rawslot;
static unsigned rawmask;

static uint64_t raw_hash(int cx, int cz)
{
    return (uint64_t)(uint32_t)cx * 0x9E3779B97F4A7C15ULL ^ (uint64_t)(uint32_t)cz;
}

static void raw_init(const char *dir)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/raw.bin.gz", dir);
    if (!gunzip_file(path, &rawbin, &rawlen))
    {
        gzFile g = gzopen(path, "rb");

        if (g == NULL) { perror(path); exit(2); }

        size_t cap = 1 << 20;
        rawbin = malloc(cap);

        if (rawbin == NULL) { fprintf(stderr, "raw.bin.gz: out of memory\n"); exit(2); }

        rawlen = 0;

        for (;;)
        {
            if (rawlen + (1 << 16) > cap)
            {
                cap *= 2;
                rawbin = realloc(rawbin, cap);
            }

            int n = gzread(g, rawbin + rawlen, 1 << 16);

            if (n <= 0) break;

            rawlen += (size_t)n;
        }

        gzclose(g);
    }

    size_t p = 0;
    int n = 0;

    while (p + 8 <= rawlen)
    {
        int cx = (int32_t)le32(rawbin + p), cz = (int32_t)le32(rawbin + p + 4);
        (void)cx; (void)cz;
        p += 8 + CHUNK_BYTES + 257;
        ++n;
    }

    nraw = 0;
    rawents = malloc((size_t)n * sizeof *rawents);
    unsigned slotcap = 4;

    while (slotcap < (unsigned)n * 4) slotcap <<= 1;

    rawmask = slotcap - 1;
    rawslot = malloc((size_t)slotcap * sizeof *rawslot);

    for (unsigned i = 0; i < slotcap; ++i) rawslot[i] = -1;

    p = 0;
    int i = 0;

    while (p + 8 <= rawlen)
    {
        int cx = (int32_t)le32(rawbin + p), cz = (int32_t)le32(rawbin + p + 4);

        rawents[i].cx = cx;
        rawents[i].cz = cz;
        rawents[i].p = rawbin + p + 8;
        ++i;

        uint64_t h = raw_hash(cx, cz);
        unsigned s = (unsigned)(h & rawmask);

        while (rawslot[s] != -1) s = (s + 1) & rawmask;

        rawslot[s] = i - 1;
        p += 8 + CHUNK_BYTES + 257;
    }
}

/* The overworld's provider for a chunk the oracle recorded raw: raw_patch
 * (through on_chunk) replaces all a generated chunk's bytes anyway, so the
 * chunk is inserted without generating it (world_insert_chunk; the provider
 * state a later generation sees is paid there). The Nether and the End keep
 * generating: their populate reads hellRNG as the last generation left it. */
static int raw_has(int cx, int cz);

static struct chunk *raw_provide(void *ctx, int cx, int cz)
{
    struct world *w = ctx;

    if (!raw_has(cx, cz)) return NULL;

    struct chunk *c = world_insert_chunk(w, cx, cz, NULL);

    if (c != NULL) chunk_count_ticking(c);
    return c;
}

/* seed the chunk world_load_chunk just generated with the oracle's raw bytes */
static void raw_patch(struct world *w, int cx, int cz)
{
    uint64_t h = raw_hash(cx, cz);
    unsigned s = (unsigned)(h & rawmask);

    while (rawslot[s] != -1)
    {
        struct raw_ent *e = &rawents[rawslot[s]];

        if (e->cx == cx && e->cz == cz)
        {
            struct chunk *c = world_chunk(w, cx, cz);

            if (c == NULL)
                fail("raw replay: chunk (%d,%d) is not in the world", cx, cz);

            const unsigned char *p = e->p;
            chunk_cells_in(c, (const uint16_t *)(const void *)p, p + 131072, p + 196608, p + 262144);
            p += 327680;

            for (int i = 0; i < 256; ++i) c->height[i] = (int32_t)le32(p + 4 * i);
            p += 1024;

            for (int i = 0; i < 256; ++i) c->precip[i] = (int32_t)le32(p + 4 * i);
            p += 1024;

            c->height_min = (int32_t)le32(p); p += 4;
            c->mask = (uint16_t)(p[0] | p[1] << 8); p += 2;
            memcpy(c->update_skylight_columns, p, 256); p += 256;
            c->gap_lighting_updated = *p;
            return;
        }

        s = (s + 1) & rawmask;
    }
}

static int raw_has(int cx, int cz)
{
    unsigned s = (unsigned)(raw_hash(cx, cz) & rawmask);

    for (; rawslot[s] != -1; s = (s + 1) & rawmask)
        if (rawents[rawslot[s]].cx == cx && rawents[rawslot[s]].cz == cz) return 1;
    return 0;
}

/* the world's on_chunk: the drivers' structure-map offer plus the raw replay.
 * dim 0 the four overworld types, dim -1 the fortress, dim 1 no map. */
static void test_offer_trampoline(void *ctx, int cx, int cz)
{
    struct populate *p = ctx;

    if (dim == -1) populate_hell_offer_chunk(p, cx, cz);
    else if (dim == 0) populate_offer_chunk(p, cx, cz);

    raw_patch(&p->world, cx, cz);
}

/* ------------------------------------------------------------ oracle records */

static unsigned char *calls, *writes, *tes, *ticks;
static int *okept, okept_n;
static size_t calls_len, writes_len, tes_len, ticks_len;
static size_t cp, wp, tp;

static int o_cx, o_cz, o_biome;
static uint64_t o_start_seed;
static int o_counts[4], o_loads_total, o_loads_after[4];
static int o_sizes_before[4], o_sizes_after[4];
struct omark { int stage; uint64_t seed; int widx; int recidx; };
static struct omark o_marks[64];
static int o_nmarks;
static int o_nwrites, o_nentities;

/* the call's entities: name, position, yaw; the entity id is the Det
 * counter's own business (the oracle gives each thread role its range), the
 * dim drivers compare the rest */
struct oent { char name[64]; double x, y, z; float yaw; };
static struct oent o_ents[64];
static int o_nents;

/* The whole file, NUL terminated. */
static unsigned char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");

    if (!f) { perror(path); exit(2); }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *out = malloc((size_t)n + 1);

    if (fread(out, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "short read %s\n", path); exit(2); }
    out[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return out;
}

static void read_call(void)
{
    o_cx = (int32_t)le32(calls + cp); cp += 4;
    o_cz = (int32_t)le32(calls + cp); cp += 4;
    o_biome = (int32_t)le32(calls + cp); cp += 4;

    if (dim != 0) { o_start_seed = le64(calls + cp); cp += 8; }

    for (int g = 0; g < 4; ++g) { o_counts[g] = (int)le32(calls + cp); cp += 4; }
    for (int g = 0; g < 4; ++g) { o_sizes_before[g] = (int)le32(calls + cp); cp += 4; }
    for (int g = 0; g < 4; ++g) { o_sizes_after[g] = (int)le32(calls + cp); cp += 4; }
    o_loads_total = (int)le32(calls + cp); cp += 4;
    for (int g = 0; g < 4; ++g) { o_loads_after[g] = (int)le32(calls + cp); cp += 4; }

    /* the recorded (cx, cz) load pairs: the native side generates the chunks
     * itself, so only the counts are read back; the pairs are skipped */
    cp += (size_t)o_loads_total * 8;

    o_nmarks = calls[cp++];

    for (int i = 0; i < o_nmarks; ++i)
    {
        o_marks[i].stage = calls[cp++];
        o_marks[i].seed = le64(calls + cp); cp += 8;
        o_marks[i].widx = (int)le32(calls + cp); cp += 4;
        o_marks[i].recidx = (int)le32(calls + cp); cp += 4;
    }

    o_nwrites = (int)le32(calls + cp); cp += 4;

    /* the call's fresh entities: name, id, position, yaw. The native keeps
     * the ones its drivers spawn and compares name, position and yaw; the id
     * is the Det counter's own business */
    o_nentities = (int)le32(calls + cp); cp += 4;
    o_nents = o_nentities;

    if (o_nents > (int)(sizeof o_ents / sizeof o_ents[0]))
        fail("call (%d,%d): %d entity records, the test holds %d", o_cx, o_cz, o_nents,
             (int)(sizeof o_ents / sizeof o_ents[0]));

    for (int i = 0; i < o_nentities; ++i)
    {
        int nlen = calls[cp++];
        struct oent *e = dim != 0 && i < (int)(sizeof o_ents / sizeof o_ents[0]) ? &o_ents[i] : NULL;

        if (e != NULL && nlen >= (int)sizeof e->name)
            fail("call (%d,%d): entity name of %d bytes overflows the record", o_cx, o_cz, nlen);

        if (e != NULL)
        {
            memcpy(e->name, calls + cp, (size_t)nlen);
            e->name[nlen] = 0;
        }

        cp += (size_t)nlen + 4;   /* the id */

        if (e != NULL)
        {
            memcpy(&e->x, calls + cp, 8);
            memcpy(&e->y, calls + cp + 8, 8);
            memcpy(&e->z, calls + cp + 16, 8);
            memcpy(&e->yaw, calls + cp + 24, 4);
        }

        cp += 24 + 4;
    }
}

/* ---------------------------------------------------------------- native side */

static struct populate pop;
static jrand world_rand;
static int64_t seed;
static int rx0, rz0, rx1, rz1;

struct nwrite { int32_t x, y, z; uint16_t id; uint8_t meta; };
static struct nwrite *nw;
static int nnw, cap_nw, cap_att;

/* The capture mirrors the oracle's probe: World.setBlock's attempts are
 * captured in attempt order, and only the ones that changed a block survive
 * (World.setBlockMetadataWithNotify has no attempt, and only reports a change).
 * drop_noops() compacts the capture at every marker. */
static uint8_t *nw_keep;        /* 1 for a changed write */
static int *att_stack, att_depth, *att_pos;
static int nkeep;               /* changed writes so far, the widx unit */
static int *kept;               /* the changed entries, in order, per compare */
static int kept_n;

static void nw_reserve(void)
{
    if (nnw == cap_nw)
    {
        cap_nw = cap_nw ? cap_nw * 2 : 4096;
        nw = realloc(nw, (size_t)cap_nw * sizeof *nw);
        nw_keep = realloc(nw_keep, (size_t)cap_nw);
        kept = realloc(kept, (size_t)cap_nw * sizeof *kept);
    }
}

/* The changed entries in attempt order: the capture holds every attempt, and
 * the oracle's markers count only the changed ones. */
static void kept_index(void)
{
    kept_n = 0;

    for (int i = 0; i < nnw; ++i)
        if (nw_keep[i]) kept[kept_n++] = i;
}

static int nw_append(int x, int y, int z, int id, int meta, int keep)
{
    nw_reserve();
    nw[nnw].x = x;
    nw[nnw].y = y;
    nw[nnw].z = z;
    nw[nnw].id = (uint16_t)id;
    nw[nnw].meta = (uint8_t)meta;
    nw_keep[nnw] = (uint8_t)keep;
    return nnw++;
}

static void on_attempt(void *ctx, int x, int y, int z, int id, int meta)
{
    (void)ctx;

    if (att_depth == cap_att)
    {
        cap_att = cap_att ? cap_att * 2 : 256;
        att_stack = realloc(att_stack, (size_t)cap_att * sizeof *att_stack);
        att_pos = realloc(att_pos, (size_t)cap_att * sizeof *att_pos);
    }

    att_pos[att_depth] = nw_append(x, y, z, id, meta, 0);
    ++att_depth;
}

static void on_attempt_end(void *ctx, int x, int y, int z, int id, int meta)
{
    (void)ctx; (void)x; (void)y; (void)z; (void)id; (void)meta;

    if (nw_keep[att_pos[att_depth - 1]]) ++nkeep;

    --att_depth;
}

static void on_block(void *ctx, int x, int y, int z, int id, int meta)
{
    (void)ctx;

    if (id == 0xffff)
    {
        nw_append(x, y, z, id, meta, 1);
        ++nkeep;
    }
    else nw_keep[att_pos[att_depth - 1]] = 1;
}

struct nstage { int stage; uint64_t seed; int widx; };
static struct nstage *nst;
static int nns, cap_ns;

static void on_stage(void *ctx, int stage, uint64_t seed)
{
    (void)ctx;

    if (nns == cap_ns)
    {
        cap_ns = cap_ns ? cap_ns * 2 : 64;
        nst = realloc(nst, (size_t)cap_ns * sizeof *nst);
    }

    nst[nns].stage = stage;
    nst[nns].seed = seed;
    nst[nns].widx = nkeep;
    ++nns;
}

/* The oracle's tile entity state, built by applying the deltas; compared
 * against the native snapshot at every boundary. */
struct ote { int x, y, z, kind; char *text; };
static struct ote ote[1024];
static int nte;

static void ote_remove(int x, int y, int z)
{
    for (int i = 0; i < nte; ++i)
    {
        if (ote[i].x == x && ote[i].y == y && ote[i].z == z)
        {
            memmove(ote + i, ote + i + 1, (size_t)(nte - i - 1) * sizeof ote[0]);
            --nte;
            return;
        }
    }
}

/* Apply one delta segment to the oracle's state. */
static void te_delta_apply(void)
{
    int n = (int)le32(tes + tp);
    tp += 4;

    for (int i = 0; i < n; ++i)
    {
        int x = (int)le32(tes + tp), y = (int)le32(tes + tp + 4), z = (int)le32(tes + tp + 8);
        int kind = tes[tp + 12];
        int nlen = (int)le32(tes + tp + 13), tlen = (int)le32(tes + tp + 17);
        tp += 21;
        char name[256];
        memcpy(name, tes + tp, (size_t)nlen < 256 ? (size_t)nlen : 255);
        char *text = malloc((size_t)tlen + 1);
        memcpy(text, tes + tp + nlen, (size_t)tlen);
        text[tlen] = 0;
        tp += (size_t)nlen + (size_t)tlen;
        (void)name;

        ote_remove(x, y, z);

        if (kind != 0)
        {
            if (nte == (int)(sizeof ote / sizeof ote[0]))
                fail("call (%d,%d): the oracle tile entity record overflows", o_cx, o_cz);

            ote[nte].x = x;
            ote[nte].y = y;
            ote[nte].z = z;
            ote[nte].kind = kind;
            ote[nte].text = dup_str(text);
            ++nte;
        }

        free(text);
    }
}

/* The native snapshot at a boundary: every tile entity the region holds, in
 * (x, z, y) order, as the oracle's snapshot is. */
struct nte { int x, y, z, kind; char *text; };
static struct nte nte_snap[1024];

static int te_snapshot(void)
{
    int n = 0;

    for (int lx = rx0; lx <= rx1; ++lx)
    {
        for (int lz = rz0; lz <= rz1; ++lz)
        {
            struct chunk *c = world_chunk(&pop.world, lx, lz);

            if (c == NULL) continue;

            for (int i = 0; i < c->tes.n; ++i)
            {
                struct tile_entity *te = c->tes.v[i];

                if (n == (int)(sizeof nte_snap / sizeof nte_snap[0]))
                    fail("call (%d,%d): the native tile entity snapshot overflows", o_cx, o_cz);

                nte_snap[n].x = te->x;
                nte_snap[n].y = te->y;
                nte_snap[n].z = te->z;
                nte_snap[n].kind = te->kind;
                nte_snap[n].text = te_render(te);
                ++n;
            }
        }
    }

    for (int i = 1; i < n; ++i)
    {
        struct nte t = nte_snap[i];
        int j = i - 1;

        while (j >= 0 && (nte_snap[j].x > t.x ||
                          (nte_snap[j].x == t.x && nte_snap[j].z > t.z) ||
                          (nte_snap[j].x == t.x && nte_snap[j].z == t.z && nte_snap[j].y > t.y)))
        {
            nte_snap[j + 1] = nte_snap[j];
            --j;
        }

        nte_snap[j + 1] = t;
    }

    return n;
}

static void compare_te_boundary(int seg)
{
    /* the oracle's list accumulates in delta-application order; the native
     * snapshot sorts (x, z, y), so sort the oracle's side the same way */
    for (int i = 1; i < nte; ++i)
    {
        struct ote t = ote[i];
        int j = i - 1;

        while (j >= 0 && (ote[j].x > t.x ||
                          (ote[j].x == t.x && (ote[j].z > t.z ||
                                               (ote[j].z == t.z && ote[j].y > t.y)))))
        {
            ote[j + 1] = ote[j];
            --j;
        }

        ote[j + 1] = t;
    }

    int n = te_snapshot();

    for (int i = 0; i < n && i < nte; ++i)
    {
        if (nte_snap[i].x != ote[i].x || nte_snap[i].y != ote[i].y || nte_snap[i].z != ote[i].z)
        {
            fprintf(stderr, "oracle:");
            for (int k = 0; k < nte && k < 6; ++k) fprintf(stderr, " (%d,%d,%d)k%d", ote[k].x, ote[k].y, ote[k].z, ote[k].kind);
            fprintf(stderr, "\nnative:");
            for (int k = 0; k < n && k < 6; ++k) fprintf(stderr, " (%d,%d,%d)k%d", nte_snap[k].x, nte_snap[k].y, nte_snap[k].z, nte_snap[k].kind);
            fprintf(stderr, "\n");

            fail("call (%d,%d) te segment %d: tile entity at (%d,%d,%d) want (%d,%d,%d)", o_cx, o_cz, seg,
                 ote[i].x, ote[i].y, ote[i].z, nte_snap[i].x, nte_snap[i].y, nte_snap[i].z);
        }

        if (strcmp(nte_snap[i].text, ote[i].text) != 0)
            fail("call (%d,%d) te segment %d: tile entity at (%d,%d,%d) differs, oracle %.160s native %.160s",
                 o_cx, o_cz, seg, ote[i].x, ote[i].y, ote[i].z, ote[i].text, nte_snap[i].text);
    }

    if (n != nte)
        fail("call (%d,%d) te segment %d: %d tile entities in the region, the oracle holds %d",
             o_cx, o_cz, seg, n, nte);
}

/* ---------------------------------------------------------------- the compare */

/* The run's stage names, from the manifest's stages list (every dimension
 * records one); the native drivers' stage ids index it in the same order. */
static char stage_names[32][16];
static int n_stages;

static void stages_parse(const char *json)
{
    const char *v = strstr(json, "\"stages\":\"");

    if (v == NULL) fail("manifest: no stages list");

    v += 10;

    while (*v && *v != '"')
    {
        const char *comma = strchr(v, ','), *endq = strchr(v, '"');
        const char *end = comma && (!endq || comma < endq) ? comma : endq;

        if (end == NULL) break;

        int n = (int)(end - v);

        if (n_stages == 32 || n >= 16) fail("manifest: %d stage names, the test holds 32", n_stages + 1);

        memcpy(stage_names[n_stages], v, (size_t)n);
        stage_names[n_stages][n] = 0;
        ++n_stages;
        v = *end == ',' ? end + 1 : end;
    }
}

static const char *stage_name(int s)
{
    return s >= 0 && s < n_stages ? stage_names[s] : "?";
}

static size_t wbase;
static int nbase;

static void compare_call(void)
{
    kept_index();

    if (nns != o_nmarks)
        fail("call (%d,%d): %d native stage markers, the oracle recorded %d", o_cx, o_cz, nns, o_nmarks);

    for (int i = 0; i < o_nmarks; ++i)
    {
        if (nst[i].stage != o_marks[i].stage)
            fail("call (%d,%d) marker %d: native stage %s, oracle stage %s",
                 o_cx, o_cz, i, stage_name(nst[i].stage), stage_name(o_marks[i].stage));

        if (o_marks[i].stage != POP_SPAWNING && o_marks[i].stage != POP_SNOW && nst[i].seed != o_marks[i].seed)
            fail("call (%d,%d) stage %s: Random state %llx, the oracle recorded %llx",
                 o_cx, o_cz, stage_name(o_marks[i].stage),
                 (unsigned long long)nst[i].seed, (unsigned long long)o_marks[i].seed);

        if (nst[i].widx - nbase != o_marks[i].widx)
        {
            /* name the first write that differs inside the segment before this
             * marker, so the failure is a place and not a count */
            int ob0 = i == 0 ? 0 : o_marks[i - 1].widx;
            int nb0 = nst[i - 1].widx;
            int on = o_marks[i].widx - ob0, gnn = nst[i].widx - nb0;
            int k = 0;

            while (k < on && k < gnn)
            {
                const unsigned char *o = writes + (size_t)okept[wbase + ob0 + k] * 16;
                struct nwrite *g = &nw[kept[nb0 + k]];

                if ((int32_t)le32(o) != g->x || (int32_t)le32(o + 4) != g->y || (int32_t)le32(o + 8) != g->z ||
                    (int)(o[12] | o[13] << 8) != g->id || o[14] != g->meta)
                {
                    fail("call (%d,%d) stage %s write %d: oracle (%d,%d,%d) %d:%d, native (%d,%d,%d) %d:%d",
                         o_cx, o_cz, stage_name(o_marks[i - 1].stage), k,
                         (int32_t)le32(o), (int32_t)le32(o + 4), (int32_t)le32(o + 8),
                         (int)(o[12] | o[13] << 8), o[14],
                         g->x, g->y, g->z, g->id, g->meta);
                }

                ++k;
            }

            fail("call (%d,%d) stage %s: %d native writes, the oracle recorded %d",
                 o_cx, o_cz, stage_name(o_marks[i].stage), gnn, on);
        }

        /* the spawning and snow markers carry draws the native skips */
        if (o_marks[i].stage != POP_SPAWNING && o_marks[i].stage != POP_SNOW && nst[i].seed != o_marks[i].seed)
            fail("call (%d,%d) stage %s: Random state %llx, the oracle recorded %llx",
                 o_cx, o_cz, stage_name(o_marks[i].stage),
                 (unsigned long long)nst[i].seed, (unsigned long long)o_marks[i].seed);
    }

    for (int i = 0; i < o_nmarks; ++i)
    {
        int ob = wbase + o_marks[i].widx;
        int oe = wbase + (i + 1 < o_nmarks ? o_marks[i + 1].widx : o_nwrites);
        int nb = nst[i].widx, ne = i + 1 < o_nmarks ? nst[i + 1].widx : nkeep;

        if (oe - ob != ne - nb)
            fail("call (%d,%d) stage %s..%s: %d native writes, the oracle recorded %d",
                 o_cx, o_cz, stage_name(o_marks[i].stage),
                 i + 1 < o_nmarks ? stage_name(o_marks[i + 1].stage) : "end", ne - nb, oe - ob);

        for (int j = 0; j < oe - ob; ++j)
        {
            const unsigned char *o = writes + (size_t)okept[ob + j] * 16;
            struct nwrite *g = &nw[kept[nb + j]];
            int oid = (int)(o[12] | o[13] << 8);

            if ((int32_t)le32(o) != g->x || (int32_t)le32(o + 4) != g->y || (int32_t)le32(o + 8) != g->z ||
                oid != g->id || o[14] != g->meta)
            {
                fail("call (%d,%d) stage %s write %d: oracle (%d,%d,%d) %d:%d, native (%d,%d,%d) %d:%d",
                     o_cx, o_cz, stage_name(o_marks[i].stage), j,
                     (int32_t)le32(o), (int32_t)le32(o + 4), (int32_t)le32(o + 8), oid, o[14],
                     g->x, g->y, g->z, g->id, g->meta);
            }

            ++compared_writes;
        }
    }
}


/* The two dim drivers keep the overworld driver's shape: ChunkProviderServer
 * populate's own steps (the chunk load, func_150809_p, the tile entity
 * boundaries, the fall-instantly hold), the fortress stage native like the
 * four overworld generators. */

/* The entities the dim drivers spawn: name, position, yaw, against the
 * oracle's record. The id is the Det counter's own business (the oracle gives
 * each thread role its range), the compare is the rest. */
static struct oent n_ents[64];
static int n_nents;

static void ent_spawn(const char *name, double x, double y, double z, float yaw)
{
    if (n_nents == (int)(sizeof n_ents / sizeof n_ents[0]))
        fail("call (%d,%d): more than %d entities this call", o_cx, o_cz, (int)(sizeof n_ents / sizeof n_ents[0]));

    snprintf(n_ents[n_nents].name, sizeof n_ents[n_nents].name, "%s", name);
    n_ents[n_nents].x = x;
    n_ents[n_nents].y = y;
    n_ents[n_nents].z = z;
    n_ents[n_nents].yaw = yaw;
    ++n_nents;
}

static void compare_entities(void)
{
    if (n_nents != o_nents)
        fail("call (%d,%d): %d entities this call, the oracle recorded %d", o_cx, o_cz, n_nents, o_nents);

    for (int i = 0; i < o_nents; ++i)
    {
        struct oent *o = &o_ents[i], *g = &n_ents[i];

        if (strcmp(o->name, g->name) != 0)
            fail("call (%d,%d) entity %d: native %s, oracle %s", o_cx, o_cz, i, g->name, o->name);

        if (memcmp(&o->x, &g->x, 8) || memcmp(&o->y, &g->y, 8) || memcmp(&o->z, &g->z, 8) ||
            memcmp(&o->yaw, &g->yaw, 4))
        {
            fail("call (%d,%d) entity %d (%s): native (%g,%g,%g) yaw %g, oracle (%g,%g,%g) yaw %g",
                 o_cx, o_cz, i, o->name, g->x, g->y, g->z, g->yaw, o->x, o->y, o->z, o->yaw);
        }
    }
}

/* The dim -1 populate call: ChunkProviderServer.populate over
 * ChunkProviderHell.populate, the fortress stage native (fortress_blocks.c
 * over the driver's own map, through the shared sc_generate), the eight
 * feature stages native after it. */
static void populate_call_hell(void)
{
    struct chunk *c = world_load_chunk(&pop.world, o_cx, o_cz);

    nbase = nkeep;
    size_t nload0 = pop.world.lon;

    if (c->terrain_populated)
        fail("call (%d,%d): the chunk is already populated", o_cx, o_cz);

    /* ChunkProviderHell.populate holds BlockFalling.field_149832_M for the
     * whole call, so every falling block the fortress or a feature write
     * wakes drops through the air in place instead of waiting for an entity */
    ticks_set_fall_instantly(1);

    /* the oracle's first tile entity delta, against the previous call's end */
    te_delta_apply();
    compare_te_boundary(0);

    populate_150809_p(&pop, c);

    /* the populate entry state: hellRNG is whatever the last chunk the world
     * generated and every populate load since left; the model derives it, the
     * recorded startSeed is the check */
    if (neg_hellreseed)
        jr_seed(&pop.world.nether.rand,
                (int64_t)((uint64_t)(int64_t)o_cx * 341873128712ULL +
                          (uint64_t)(int64_t)o_cz * 132897987541ULL));

    if (pop.world.nether.rand.seed != (o_start_seed & ((1ULL << 48) - 1)))
        fail("call (%d,%d): populate starts from Random state %llx, the oracle recorded %llx",
             o_cx, o_cz, (unsigned long long)pop.world.nether.rand.seed,
             (unsigned long long)o_start_seed);

    int count = populate_hell_count(&pop, o_cx, o_cz);

    if (count != o_counts[0])
        fail("call (%d,%d) fortress: %d starts intersect, the oracle counted %d",
             o_cx, o_cz, count, o_counts[0]);

    /* MapGenStructure.generateStructuresInChunk over the fortress map: the
     * start runs through hellRNG itself, whose state the stage marker then
     * carries. Each start counts as a structure step. */
    populate_hell_structures(&pop, o_cx, o_cz);
    structure_steps += count;

    te_delta_apply();
    compare_te_boundary(1);

    /* the chunks the oracle's call had generated by the end of the fortress
     * stage: the native side generates a chunk when a read or a write reaches
     * it, as vanilla does, so the count must already match */
    if ((long long)pop.world.lon - (long long)nload0 != o_loads_after[0])
        fail("call (%d,%d) fortress: %lld chunks loaded, the oracle's call had generated %d",
             o_cx, o_cz, (long long)pop.world.lon - (long long)nload0, o_loads_after[0]);

    /* the eight feature stages, native, the stage hook after each */
    static void (*const STAGE[8])(struct populate *, int, int) = {
        populate_hell_lava, populate_hell_fire, populate_hell_glow1, populate_hell_glow2,
        populate_hell_brown, populate_hell_red, populate_hell_quartz, populate_hell_hidden,
    };

    for (int st = 0; st < 8; ++st)
    {
        STAGE[st](&pop, o_cx, o_cz);

        te_delta_apply();
        compare_te_boundary(st + 2);
    }

    /* every chunk the oracle's call generated, the native side generated too */
    if ((long long)pop.world.lon - (long long)nload0 != o_loads_total)
        fail("call (%d,%d): %lld chunks loaded, the oracle's call generated %d",
             o_cx, o_cz, (long long)pop.world.lon - (long long)nload0, o_loads_total);

    /* the oracle's end-of-call tile entity snapshot */
    te_delta_apply();
    compare_te_boundary(10);

    ticks_set_fall_instantly(0);

    compare_call();
    nns = 0;
    wbase += o_nwrites;   /* record index, not bytes */
}

/* The End populate call: ChunkProviderServer.populate over
 * ChunkProviderEnd.populate's BiomeEndDecorator. There is no structure map,
 * so nothing but the decorate runs; every chunk load the decorate makes (a
 * spike or a vein reaching past the loaded edge) is the world core's own, and
 * end_rand is the decorate stream: the first call seeds it from the recorded
 * start state. */
static int end_seeded;

static void populate_call_end(void)
{
    struct chunk *c = world_load_chunk(&pop.world, o_cx, o_cz);

    nbase = nkeep;
    size_t nload0 = pop.world.lon;

    if (c->terrain_populated)
        fail("call (%d,%d): the chunk is already populated", o_cx, o_cz);

    /* ChunkProviderEnd.populate holds BlockFalling.field_149832_M for the
     * whole call */
    ticks_set_fall_instantly(1);

    /* the oracle's first tile entity delta, against the previous call's end */
    te_delta_apply();
    compare_te_boundary(0);

    populate_150809_p(&pop, c);

    /* the populate entry state: the End World's rand, seeded by the first
     * call's recorded start state and carried across calls by the model */
    if (!end_seeded)
    {
        pop.end_rand.seed = o_start_seed & ((1ULL << 48) - 1);
        end_seeded = 1;
    }

    if (pop.end_rand.seed != (o_start_seed & ((1ULL << 48) - 1)))
        fail("call (%d,%d): populate starts from Random state %llx, the oracle recorded %llx",
             o_cx, o_cz, (unsigned long long)pop.end_rand.seed,
             (unsigned long long)o_start_seed);

    n_nents = 0;

    populate_end_ores(&pop, o_cx, o_cz);
    te_delta_apply();
    compare_te_boundary(1);

    /* the feature clears the report at every generate; a call the spike skips
     * would leave the previous call's report standing */
    spike_crystal_last.spawned = 0;
    populate_end_spike(&pop, o_cx, o_cz);

    if (spike_crystal_last.spawned)
        ent_spawn("EntityEnderCrystal", spike_crystal_last.x, spike_crystal_last.y,
                  spike_crystal_last.z, spike_crystal_last.yaw);

    te_delta_apply();
    compare_te_boundary(2);

    populate_end_dragon(&pop, o_cx, o_cz);

    if (end_dragon_last.spawned) ent_spawn("EntityDragon", 0.0, 128.0, 0.0, end_dragon_last.yaw);

    /* every chunk the oracle's call generated, the native side generated too */
    if ((long long)pop.world.lon - (long long)nload0 != o_loads_total)
        fail("call (%d,%d): %lld chunks loaded, the oracle's call generated %d",
             o_cx, o_cz, (long long)pop.world.lon - (long long)nload0, o_loads_total);

    /* the oracle's end-of-call tile entity snapshot */
    te_delta_apply();
    compare_te_boundary(3);

    compare_call();
    compare_entities();

    ticks_set_fall_instantly(0);

    nns = 0;
    wbase += o_nwrites;   /* record index, not bytes */
}

/* One populate call, driven the way ChunkProviderServer.populate runs. */
static void populate_call(void)
{
    struct chunk *c = world_load_chunk(&pop.world, o_cx, o_cz);

    nbase = nkeep;
    size_t nload0 = pop.world.lon;

    if (c->terrain_populated)
        fail("call (%d,%d): the chunk is already populated", o_cx, o_cz);

    /* ChunkProviderGenerate.populate holds BlockFalling.fallInstantly for the
     * whole call, so every falling block the decorator or a structure write
     * wakes drops through the air in place instead of waiting for an entity */
    ticks_set_fall_instantly(1);

    /* the oracle's first tile entity delta, against the previous call's end */
    te_delta_apply();
    compare_te_boundary(0);

    populate_150809_p(&pop, c);
    populate_seed(&pop, o_cx, o_cz);

    int village = 0;

    for (int g = 0; g < 4; ++g)
    {
        /* MapGenStructure.generateStructuresInChunk works over the very map
         * the oracle held; the record carries its size before and after each
         * generator, so a missing or extra start is named here instead of
         * surfacing as a write diff later. */
        int size = populate_map_size(&pop, g);

        if (size != o_sizes_before[g])
            fail("call (%d,%d) generator %d: %d starts in the map, the oracle's map held %d",
                 o_cx, o_cz, g, size, o_sizes_before[g]);

        int count = populate_structures_count(&pop, g, o_cx, o_cz);

        if (count != o_counts[g])
            fail("call (%d,%d) generator %d: %d starts intersect, the oracle counted %d",
                 o_cx, o_cz, g, count, o_counts[g]);

        /* MapGenStructure.generateStructuresInChunk: the generator runs every
         * one of those starts through the shared population Random, then
         * ChunkProviderGenerate.populate marks the stage. Each start counts as
         * a structure step. */
        int any = populate_structures(&pop, g, o_cx, o_cz);

        structure_steps += count;

        if (g == 1) village = any;

        if (populate_map_size(&pop, g) != o_sizes_after[g])
            fail("call (%d,%d) generator %d: %d starts in the map after the generator, the oracle's map held %d",
                 o_cx, o_cz, g, populate_map_size(&pop, g), o_sizes_after[g]);

        /* the tile entity state the generator left */
        te_delta_apply();
        compare_te_boundary(g + 1);

        /* The chunks the oracle's call had generated by the end of this
         * generator: the native side generates a chunk when a read or a write
         * reaches it, as vanilla does, so the count must already match. */
        if ((long long)pop.world.lon - (long long)nload0 != o_loads_after[g])
            fail("call (%d,%d) generator %d: %lld chunks loaded, the oracle's call had generated %d",
                 o_cx, o_cz, g, (long long)pop.world.lon - (long long)nload0, o_loads_after[g]);
    }

    populate_lakes(&pop, o_cx, o_cz, village);
    populate_dungeons(&pop, o_cx, o_cz);
    decorator_decorate(&pop, o_biome, o_cx, o_cz);
    populate_stage_now(&pop, POP_DECORATE);

    /* every chunk the oracle's call generated, the native side generated too */
    if ((long long)pop.world.lon - (long long)nload0 != o_loads_total)
        fail("call (%d,%d): %lld chunks loaded, the oracle's call generated %d",
             o_cx, o_cz, (long long)pop.world.lon - (long long)nload0, o_loads_total);

    /* the native skips performWorldGenSpawning: fire its marker for the index
     * alignment, its Random state is not compared */
    populate_stage_now(&pop, POP_SPAWNING);

    populate_freeze(&pop, o_cx, o_cz);

    /* the oracle's end-of-call tile entity snapshot */
    te_delta_apply();
    compare_te_boundary(5);

    ticks_set_fall_instantly(0);

    compare_call();
    nns = 0;
    wbase += o_nwrites;   /* record index, not bytes */
}

/* The oracle's pending scheduled ticks, in the TreeSet's own order: scheduled
 * time, then priority, then the tick entry id, which follows insertion. */
static void compare_ticks(void)
{
    int n = (int)le32(ticks);
    int np = ticks_pending_count();

    /* the native set is a min-heap on (time, priority, entry), which is the
     * TreeSet's own total order, so ticks_peek hands the entries back in it */
    struct tick_entry *sorted = malloc((size_t)(np > 0 ? np : 1) * sizeof *sorted);

    if (sorted == NULL) fail("ticks: out of memory for %d entries", np);

    if (np > 0 && ticks_peek(np, sorted) != np) fail("ticks: peek returned fewer than %d entries", np);

    if (np != n)
        fail("ticks: %d pending entries, the oracle has %d", np, n);

    for (int i = 0; i < n; ++i)
    {
        const unsigned char *e = ticks + 4 + (size_t)i * 36;

        if ((int32_t)le32(e) != sorted[i].x || (int32_t)le32(e + 4) != sorted[i].y ||
            (int32_t)le32(e + 8) != sorted[i].z || (int)le32(e + 12) != sorted[i].block ||
            (int64_t)le64(e + 16) != sorted[i].time || (int)le32(e + 24) != sorted[i].priority)
        {
            fail("ticks entry %d: oracle (%d,%d,%d) block %d at %lld prio %d, native (%d,%d,%d) block %d at %lld prio %d",
                 i, (int32_t)le32(e), (int32_t)le32(e + 4), (int32_t)le32(e + 8),
                 (int)le32(e + 12), (long long)le64(e + 16), (int)le32(e + 24),
                 sorted[i].x, sorted[i].y, sorted[i].z, sorted[i].block,
                 (long long)sorted[i].time, sorted[i].priority);
        }
    }

    free(sorted);
}

/* The final region: every loaded chunk of the ring, in record order, with
 * light and height maps. */
static void compare_final(void)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/final.bin.gz", dir);
    struct gunzip *g = gunzip_open(path);

    if (!g) { perror(path); exit(2); }

    static unsigned char buf[4 + 4 + CHUNK_BYTES + 256 + 1];
    static unsigned char cols[256];

    for (int lx = rx0; lx <= rx1; ++lx)
    {
        for (int lz = rz0; lz <= rz1; ++lz)
        {
            if (gunzip_read(g, buf, sizeof buf) != (int)sizeof buf)
                fail("final region: short read at chunk (%d,%d)", lx, lz);

            int ocx = (int32_t)le32(buf), ocz = (int32_t)le32(buf + 4);

            if (ocx != lx || ocz != lz)
                fail("final region: record for chunk (%d,%d), expected (%d,%d)", ocx, ocz, lx, lz);

            struct chunk *c = world_chunk(&pop.world, lx, lz);

            if (c == NULL)
                fail("final region: chunk (%d,%d) is not loaded natively", lx, lz);

            const unsigned char *p = buf + 8;

            for (int i = 0; i < 65536; ++i)
            {
                int id = p[2 * i] | p[2 * i + 1] << 8;

                if (chunk_cell_id(c, i) != id)
                    fail("final chunk (%d,%d) cell (%d,%d,%d): id want %d got %d",
                         lx, lz, i >> 12, i & 255, (i >> 8) & 15, id, chunk_cell_id(c, i));
            }

            for (int i = 0; i < 65536; ++i)
                if (chunk_cell_meta(c, i) != p[65536 * 2 + i])
                    fail("final chunk (%d,%d) cell (%d,%d,%d): meta want %d got %d",
                         lx, lz, i >> 12, i & 255, (i >> 8) & 15, p[65536 * 2 + i], chunk_cell_meta(c, i));

            for (int i = 0; i < 65536; ++i)
                if (chunk_cell_sky(c, i) != p[196608 + i])
                    fail("final chunk (%d,%d) cell (%d,%d,%d): sky want %d got %d",
                         lx, lz, i >> 12, i & 255, (i >> 8) & 15, p[196608 + i], chunk_cell_sky(c, i));

            for (int i = 0; i < 65536; ++i)
                if (chunk_cell_blocklight(c, i) != p[262144 + i])
                    fail("final chunk (%d,%d) cell (%d,%d,%d): block light want %d got %d",
                         lx, lz, i >> 12, i & 255, (i >> 8) & 15, p[262144 + i], chunk_cell_blocklight(c, i));

            for (int i = 0; i < 256; ++i)
            {
                int32_t h = (int32_t)le32(p + 327680 + 4 * i);

                if (c->height[i] != h)
                    fail("final chunk (%d,%d): heightMap (%d,%d) want %d got %d",
                         lx, lz, i & 15, i >> 4, h, c->height[i]);
            }

            for (int i = 0; i < 256; ++i)
            {
                int32_t ph = (int32_t)le32(p + 328704 + 4 * i);

                if (c->precip[i] != ph)
                    fail("final chunk (%d,%d): precipitation (%d,%d) want %d got %d",
                         lx, lz, i & 15, i >> 4, ph, c->precip[i]);
            }
            int32_t hmin = (int32_t)le32(p + 329728);
            if (c->height_min != hmin) fail("final chunk (%d,%d): heightMapMinimum want %d got %d", lx, lz, hmin, c->height_min);
            unsigned mask = p[329732] | p[329733] << 8;
            if (c->mask != mask) fail("final chunk (%d,%d): section mask want 0x%04x got 0x%04x", lx, lz, mask, c->mask);

            memcpy(cols, buf + 8 + CHUNK_BYTES, 256);
            for (int i = 0; i < 256; ++i)
                if (c->update_skylight_columns[i] != cols[i])
                    fail("final chunk (%d,%d): updateSkylightColumns (%d,%d) want %d got %d",
                         lx, lz, i & 15, i >> 4, cols[i], c->update_skylight_columns[i]);

            if (c->gap_lighting_updated != buf[sizeof buf - 1])
                fail("final chunk (%d,%d): isGapLightingUpdated want %d got %d",
                     lx, lz, buf[sizeof buf - 1], c->gap_lighting_updated);
        }
    }

    gunzip_close(g);
}

/* ------------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--negative=snow")) neg_snow = 1;
        else if (!strcmp(argv[i], "--negative=count")) neg_count = 1;
        else if (!strcmp(argv[i], "--negative=muttree")) neg_muttree = 1;
        else if (!strcmp(argv[i], "--negative=maporder")) nw_env->cfg.populate_negative_maporder = 1;
        else if (!strcmp(argv[i], "--negative=village")) nw_env->cfg.populate_negative_village = 1;
        else if (!strcmp(argv[i], "--negative=helreseed")) neg_hellreseed = 1;
        else if (!strcmp(argv[i], "--negative=spike")) neg_spike = 1;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace_open(argv[++i]);
        else if (!dir) dir = argv[i];
        else { fprintf(stderr, "usage: test_populate [--negative=snow|count|muttree|maporder|village|helreseed|spike] [--trace PATH] DIR\n"); return 2; }
    }

    if (!dir) { fprintf(stderr, "usage: test_populate DIR\n"); return 2; }
    snprintf(dirbuf, sizeof dirbuf, "%s", dir);

    char mpath[1200];
    snprintf(mpath, sizeof mpath, "%s/manifest.json", dir);
    FILE *mf = fopen(mpath, "rb");

    if (!mf) { perror(mpath); return 2; }

    static char manifest[1 << 20];
    size_t mn = fread(manifest, 1, sizeof manifest - 1, mf);
    manifest[mn] = 0;
    fclose(mf);

    seed = manifest_int(manifest, "seed");
    dim = strstr(manifest, "\"dim\":") != NULL ? (int)manifest_int(manifest, "dim") : 0;
    int x0 = (int)manifest_int(manifest, "x0");
    int z0 = (int)manifest_int(manifest, "z0");
    int width = (int)manifest_int(manifest, "width");
    stages_parse(manifest);
    rx0 = x0 - 1; rz0 = z0 - 1; rx1 = x0 + width; rz1 = z0 + width;

    char path[1200];
    snprintf(path, sizeof path, "%s/calls.bin", dir);
    calls = slurp(path, &calls_len);
    snprintf(path, sizeof path, "%s/writes.bin", dir);
    writes = slurp(path, &writes_len);
    snprintf(path, sizeof path, "%s/tileentities.bin", dir);
    tes = slurp(path, &tes_len);
    snprintf(path, sizeof path, "%s/ticks.bin", dir);
    ticks = slurp(path, &ticks_len);
    cp = wp = tp = 0;

    /* The oracle's changed records, in order: the markers' write indices count
     * only these, while the stream also holds the no-op attempts. */
    okept = malloc((writes_len / 16 + 1) * sizeof *okept);

    if (okept == NULL) { fprintf(stderr, "okey: out of memory\n"); return 2; }

    for (size_t i = 0; i < writes_len / 16; ++i)
        if ((writes[i * 16 + 15] & 0x80) == 0) okept[okept_n++] = (int)i;

    populate_init(&pop, seed);
    pop.world.dim = dim;   /* before the first chunk load */
    pop.stage.fn = on_stage;
    pop.stage.ctx = &pop;
    pop.world.on_block = on_block;
    pop.world.on_block_ctx = &pop;
    pop.world.on_attempt = on_attempt;
    pop.world.on_attempt_end = on_attempt_end;
    pop.world.on_chunk = test_offer_trampoline;
    pop.world.on_chunk_ctx = &pop;
    raw_init(dir);
    if (dim == 0)
    {
        pop.world.provide = raw_provide;
        pop.world.provide_ctx = &pop.world;
    }

    /* the worlds beside 0 record their World.rand's state at record start
     * (the manifest's worldRand): the fire placements' 30 + nextInt(10) and
     * the immediate liquid updateTicks draw the delays from it. The manifest
     * holds the Random's internal (scrambled) seed; jr_seed scrambles again,
     * so unscramble first. */
    if (dim != 0)
    {
        jr_seed(&world_rand, (int64_t)((uint64_t)manifest_int(manifest, "worldRand") ^ 0x5DEECE66DULL));
        ticks_set_rand(&world_rand);
        randomtick_tick_rand(&world_rand);
    }

    if (dim == -1) populate_hell_init(&pop, seed);

    if (neg_snow) nw_env->cfg.populate_negative_snow = 1;
    if (neg_count) nw_env->cfg.decorator_negative_count = 1;
    if (neg_muttree) nw_env->cfg.decorator_negative_muttree = 1;
    if (neg_spike) nw_env->cfg.populate_negative_spike = 1;

    /* the world's own initial chunk load, before any populate call: the oracle
     * ran it at world creation, so its 625 walks are already in the maps the
     * calls see */
    populate_initial_chunks(&pop);

    /* the raw ring, cx-major cz inner, the recorded load order */
    for (int lx = rx0; lx <= rx1; ++lx)
        for (int lz = rz0; lz <= rz1; ++lz)
            world_load_chunk(&pop.world, lx, lz);

    long long calls_n = 0;

    while (cp < calls_len)
    {
        read_call();

        if (dim == -1) populate_call_hell();
        else if (dim == 1) populate_call_end();
        else populate_call();

        ++calls_n;
    }

    compare_ticks();

    if (dim == -1) populate_hell_free();

    compare_final();
    trace_close();

    printf("PASS %s: %lld calls, %lld structure steps native, %lld writes compared\n",
           dir, calls_n, structure_steps, compared_writes);
    return 0;
}