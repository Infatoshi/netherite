/* Gate: the mob spawner against the oracle's SpawnProbe recording
 * (oracle/harness/netherite/oracle/SpawnProbe.java, recorded with
 * `make -C oracle run SEED=N CLASS=SpawnProbe NAME=x CMD='{"kind":"spawner",...}'`).
 *
 *   test_spawning RECORDING_DIR
 *
 * DIR is a snapshot directory with the probe's keys in the manifest. The
 * checks, tick by tick over the recording:
 *
 *   1. the snapshot loads and Det's SERVER streams come off det.nbt;
 *   2. the spawner runs at the tick head (the servertick hook), with the
 *      parked player, the recorded active set's neighbourhood and the
 *      chunkstate's inhabitedTime as its scene;
 *   3. the write rows byte for byte against tickwrites.bin, the tick body's
 *      own spawns (item, falling block, lightning) against their lines, the
 *      mob records (class, id, position, the entity Random's state, the UUID,
 *      onSpawnWithEgg's equipment) against their fields, the scalars
 *      (World.rand, updateLCG, the clocks, the weather, skylightSubtracted,
 *      ambientTickCountdown), the pending set and Det's SERVER digests.
 *
 * The first difference names the tick, the spawn and both values; the run
 * stops there. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include "../engine/det.h"
#include "../engine/env.h"
#include "../engine/living.h"
#include "../engine/item_entity.h"
#include "../engine/jmath.h"
#include "../engine/ticks.h"
#include "../engine/trace.h"
#include "../engine/populate.h"
#include "../engine/spawning.h"
#include "../engine/snapshot.h"
#include "../engine/nbtjson.h"
#include "probe.h"

static int fails;
static char path[1200];
static int neg_cap, neg_pack;

static const char *value_of(const char *line, const char *key)
{
    static char keys[8][32];
    static int slot;

    snprintf(keys[slot % 8], sizeof keys[0], "\"%s\":", key);
    const char *p = strstr(line, keys[slot % 8]);
    ++slot;

    if (p == NULL) return NULL;
    p += strlen(keys[(slot - 1) % 8]);

    if (*p == '"') return p + 1;

    return p;
}

static const char *short_value(const char *v)
{
    static char ring[8][24];
    static int slot;
    char *out = ring[slot++ % 8];

    if (v == NULL) return "(absent)";

    int n = 0;

    while (v[n] != 0 && v[n] != '"' && v[n] != ',' && v[n] != ']' && n < 23) ++n;

    memcpy(out, v, (size_t)n);
    out[n] = 0;
    return out;
}

static int64_t as_long(const char *v)
{
    if (v == NULL) return INT64_MIN;

    if (v[1] == ':') v += 2;

    return strtoll(v, NULL, 10);
}

static float as_float(const char *v)
{
    if (v == NULL) return 0.0F;

    if (v[0] == 'f' && v[1] == ':')
    {
        uint32_t bits = (uint32_t)strtoul(v + 2, NULL, 16);
        float f;

        memcpy(&f, &bits, sizeof f);
        return f;
    }

    return strtof(v, NULL);
}

static double as_double(const char *v)
{
    if (v == NULL || v[0] != 'd' || v[1] != ':') return 0.0;

    uint64_t bits = strtoull(v + 2, NULL, 16);
    double d;

    memcpy(&d, &bits, sizeof d);
    return d;
}

/* A bare hex string, the entity Random's 48-bit state. */
static uint64_t as_hex48(const char *v)
{
    if (v == NULL) return UINT64_MAX;

    return strtoull(v, NULL, 16);
}

static double le_double(const unsigned char *p)
{
    uint64_t b = le64(p);
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

static float le_float(const unsigned char *p)
{
    uint32_t b = (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24);
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

static uint32_t fbits(float f)
{
    uint32_t u;

    memcpy(&u, &f, sizeof u);
    return u;
}

static void fail(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("FAIL ", stdout);
    vprintf(fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
    ++fails;
}

static int64_t nested_int(const char *json, const char *obj, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":{", obj);
    const char *p = strstr(json, pat);

    if (p == NULL) return INT64_MIN;

    char kpat[48];
    snprintf(kpat, sizeof kpat, "\"%s\":", key);
    p = strstr(p, kpat);

    if (p == NULL) return INT64_MIN;

    return as_long(p + strlen(kpat));
}

/* The kind's class name, the recording's cls field. */
static const char *kind_class(int kind)
{
    switch (kind)
    {
    case SP_ZOMBIE: return "EntityZombie";
    case SP_SKELETON: return "EntitySkeleton";
    case SP_SPIDER: return "EntitySpider";
    case SP_CREEPER: return "EntityCreeper";
    case SP_SLIME: return "EntitySlime";
    case SP_ENDERMAN: return "EntityEnderman";
    case SP_WITCH: return "EntityWitch";
    case SP_SHEEP: return "EntitySheep";
    case SP_PIG: return "EntityPig";
    case SP_CHICKEN: return "EntityChicken";
    case SP_COW: return "EntityCow";
    case SP_MOOSHROOM: return "EntityMooshroom";
    case SP_BAT: return "EntityBat";
    case SP_SQUID: return "EntitySquid";
    default: return "Entity?";
    }
}

/* ------------------------------------------------- the populate-spawn kind */

/* The worldgen spawning recording (SpawnProbe's "populate" kind): a region of
 * populate calls, each with the biome at (x+16, z+16), World.rand's state
 * entering and leaving, and the worldgen animals' records. The native side
 * replays the recording's raw chunks (the structure generators carve during
 * provideChunk), drives every call's populate stages in vanilla order with
 * the structure maps included, runs the spawning stage through
 * spawner_world_gen over Det's SERVER streams (the state the recording's
 * start.txt holds) and compares every spawn record and the leaving World.rand
 * state. */

#include "../engine/populate.h"
#include "../engine/decorator.h"
#include "../engine/randomtick.h"

#define PS_CHUNK_BYTES (2 * 65536 + 3 * 65536 + 2 * 256 * 4 + 4 + 2)

static unsigned char *ps_rawbin;
static size_t ps_rawlen;

struct ps_raw_ent { int cx, cz; const unsigned char *p; };
static struct ps_raw_ent *ps_rawents;
static int ps_nraw;
static int32_t *ps_rawslot;
static unsigned ps_rawmask;

static uint64_t ps_raw_hash(int cx, int cz)
{
    return (uint64_t)(uint32_t)cx * 0x9E3779B97F4A7C15ULL ^ (uint64_t)(uint32_t)cz;
}

static void ps_raw_init(const char *dir)
{
    snprintf(path, sizeof path, "%s/raw.bin.gz", dir);
    gzFile g = gzopen(path, "rb");

    if (g == NULL) { perror(path); exit(2); }

    size_t cap = 1 << 20;
    ps_rawbin = malloc(cap);
    ps_rawlen = 0;

    for (;;)
    {
        if (ps_rawlen + (1 << 16) > cap)
        {
            cap *= 2;
            ps_rawbin = realloc(ps_rawbin, cap);
        }

        int n = gzread(g, ps_rawbin + ps_rawlen, 1 << 16);

        if (n <= 0) break;

        ps_rawlen += (size_t)n;
    }

    gzclose(g);

    size_t p = 0;
    int n = 0;

    while (p + 8 <= ps_rawlen)
    {
        p += 8 + PS_CHUNK_BYTES + 257;
        ++n;
    }

    ps_nraw = 0;
    ps_rawents = malloc((size_t)n * sizeof *ps_rawents);
    unsigned slotcap = 4;

    while (slotcap < (unsigned)n * 4) slotcap <<= 1;

    ps_rawmask = slotcap - 1;
    ps_rawslot = malloc((size_t)slotcap * sizeof *ps_rawslot);

    for (unsigned i = 0; i < slotcap; ++i) ps_rawslot[i] = -1;

    p = 0;

    while (p + 8 <= ps_rawlen)
    {
        int cx = (int32_t)le32(ps_rawbin + p), cz = (int32_t)le32(ps_rawbin + p + 4);

        ps_rawents[ps_nraw].cx = cx;
        ps_rawents[ps_nraw].cz = cz;
        ps_rawents[ps_nraw].p = ps_rawbin + p + 8;
        ++ps_nraw;

        uint64_t h = ps_raw_hash(cx, cz);
        unsigned s = (unsigned)(h & ps_rawmask);

        while (ps_rawslot[s] != -1) s = (s + 1) & ps_rawmask;

        ps_rawslot[s] = ps_nraw - 1;
        p += 8 + PS_CHUNK_BYTES + 257;
    }
}

/* seed the chunk world_load_chunk just generated with the oracle's raw bytes */
static void ps_raw_patch(struct world *w, int cx, int cz)
{
    uint64_t h = ps_raw_hash(cx, cz);
    unsigned s = (unsigned)(h & ps_rawmask);

    while (ps_rawslot[s] != -1)
    {
        struct ps_raw_ent *e = &ps_rawents[ps_rawslot[s]];

        if (e->cx == cx && e->cz == cz)
        {
            struct chunk *c = world_chunk(w, cx, cz);

            if (c == NULL) return;

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

        s = (s + 1) & ps_rawmask;
    }
}

static void ps_offer_trampoline(void *ctx, int cx, int cz)
{
    populate_offer_chunk((struct populate *)ctx, cx, cz);
    ps_raw_patch(&((struct populate *)ctx)->world, cx, cz);
}

/* the call's stage marks, the populate Random's state at each vanilla
 * marker, in fire order; the runner compares them against the recording */
static const char *ps_stage_names[POP_STAGES] = {
    "seed", "mineshaft", "village", "stronghold", "scattered", "waterlake",
    "lavalake", "dungeons", "decorate", "spawning", "snow",
    "ores", "sand", "clay", "gravel", "trees", "bigmushrooms", "flowers",
    "grass", "deadbush", "waterlily", "mushrooms", "mushroom1", "mushroom2",
    "reeds", "reeds10", "pumpkin", "cactus", "springs", "springslava",
};

struct ps_mark { int stage; uint64_t seed; };
static struct ps_mark ps_marks[64];
static int ps_nmarks;

static void ps_on_stage(void *ctx, int stage, uint64_t seed)
{
    (void)ctx;

    if (ps_nmarks < (int)(sizeof ps_marks / sizeof ps_marks[0]))
    {
        ps_marks[ps_nmarks].stage = stage;
        ps_marks[ps_nmarks].seed = seed;
        ++ps_nmarks;
    }
}

struct ps_ref {
    int64_t world_seed;
    uint64_t seeder[4], math[4], split[4];
    int next_id[4];
};

static void ps_read_ref(struct ps_ref *r, const char *dir)
{
    snprintf(path, sizeof path, "%s/start.txt", dir);
    FILE *f = fopen(path, "r");

    if (!f)
    {
        fail("%s: no start.txt", dir);
        return;
    }

    memset(r, 0, sizeof *r);
    char line[2048];

    while (fgets(line, sizeof line, f))
    {
        long long v;
        int role;
        unsigned long long a, b, c;

        if (sscanf(line, "worldSeed %lld", &v) == 1) { r->world_seed = v; continue; }

        if (sscanf(line, "nextId %d %d %d %d", &r->next_id[0], &r->next_id[1], &r->next_id[2], &r->next_id[3]) == 4) continue;

        if (sscanf(line, "digest %d %llx %llx %llx", &role, &a, &b, &c) == 4)
        {
            r->seeder[role] = a;
            r->math[role] = b;
            r->split[role] = c;
        }
    }

    fclose(f);
}

static int populate_spawn_run(const char *dir, const char *manifest)
{
    int64_t seed = manifest_int(manifest, "seed");
    int x0 = (int)manifest_int(manifest, "x0");
    int z0 = (int)manifest_int(manifest, "z0");
    int width = (int)manifest_int(manifest, "width");
    int64_t calls_want = manifest_int(manifest, "calls");
    int64_t spawns_want = manifest_int(manifest, "spawns");

    /* Det as the calls start: the constructors draw from the SERVER role (the
     * probe ran them on its own thread, registered as the server thread) */
    struct ps_ref ref;
    memset(&ref, 0, sizeof ref);
    ps_read_ref(&ref, dir);

    if (fails) return 1;

    det_state det;
    det_init(&det);

    uint64_t seeders[4] = { ref.seeder[0], ref.seeder[1], ref.seeder[2], ref.seeder[3] };
    uint64_t maths[4] = { ref.math[0], ref.math[1], ref.math[2], ref.math[3] };
    int nexts[4] = { ref.next_id[0], ref.next_id[1], ref.next_id[2], ref.next_id[3] };
    det_load(&det, seed, seeders, maths, nexts);

    struct populate pop;
    jrand wr;
    populate_init(&pop, seed);
    populate_world_rand = &wr;
    populate_det = &det;
    pop.stage.fn = ps_on_stage;
    pop.stage.ctx = &pop;

    /* the structure generators carve during provideChunk, so the world loads
     * the oracle's raw bytes at every generated chunk (the same replay the
     * populate probe's test runs); the map walk still runs per chunk, over
     * the walk's own Randoms and the world rand the caller published */
    ps_raw_init(dir);
    pop.world.on_chunk = ps_offer_trampoline;
    pop.world.on_chunk_ctx = &pop;

    /* the world's own initial chunk load's structure walk, then the ring's
     * raw loads, both before any call */
    populate_initial_chunks(&pop);

    for (int lx = x0 - 1; lx <= x0 + width; ++lx)
        for (int lz = z0 - 1; lz <= z0 + width; ++lz)
            world_load_chunk(&pop.world, lx, lz);

    struct spawner sp;
    spawner_init(&sp, &pop.world, &det, &wr);
    sp.role = DET_SERVER;
    sp.pop_rand = &pop.rand;

    snprintf(path, sizeof path, "%s/calls.jsonl.gz", dir);
    gzFile cg = gzopen(path, "rb");

    if (cg == NULL)
    {
        printf("FAIL %s: cannot read calls.jsonl.gz\n", dir);
        return 1;
    }

    int rc = 0;
    long long calls = 0, nspawns = 0, ncalls_exact = 0;

    const char *line;
    struct lines in;
    lines_init(&in);
    lines_gz(&in, cg);

    while ((line = lines_next(&in)) != NULL && !rc)
    {
        if (!line[0]) continue;
        ++calls;

        int cx = (int)as_long(value_of(line, "cx"));
        int cz = (int)as_long(value_of(line, "cz"));
        int biome = (int)as_long(value_of(line, "biome"));
        uint64_t wrb = as_hex48(value_of(line, "wrb"));
        uint64_t wra = as_hex48(value_of(line, "wra"));

        /* World.rand's state entering the call: jr_seed scrambles again, so
         * unscramble (Random.seed ^ multiplier) first */
        jr_seed(&wr, (int64_t)(wrb ^ 0x5DEECE66DULL));

        struct chunk *c = world_load_chunk(&pop.world, cx, cz);

        if (c->terrain_populated)
        {
            fail("%s: call (%d,%d): the chunk is already populated", dir, cx, cz);
            rc = 1;
            break;
        }

        /* the spawn capture: the records this call's spawning stage makes */
        sp.nout = 0;
        ps_nmarks = 0;

        /* ChunkProviderGenerate.populate, in vanilla order. The tick's World.rand
         * stream is published for the block-callback draws (the springs' fizz);
         * the spawning stage's own picks run on it through the spawner */
        ticks_set_fall_instantly(1);
        randomtick_tick_rand(&wr);

        populate_seed(&pop, cx, cz);

        int village = 0;

        for (int g = 0; g < 4; ++g)
        {
            int any = populate_structures(&pop, g, cx, cz);
            if (g == 1) village = any;
        }

        populate_lakes(&pop, cx, cz, village);
        populate_dungeons(&pop, cx, cz);
        decorator_decorate(&pop, biome, cx, cz);
        populate_stage_now(&pop, POP_DECORATE);

        spawner_world_gen(&sp, biome, cx * 16 + 8, cz * 16 + 8);
        populate_stage_now(&pop, POP_SPAWNING);

        populate_freeze(&pop, cx, cz);

        ticks_set_fall_instantly(0);

        /* the stage marks: the populate Random's state at each vanilla
         * marker, in fire order */
        {
            int want_n = 0;
            const char *mp = strstr(line, "\"marks\":[");

            if (mp != NULL)
            {
                /* the marks array runs to the next key (wra) */
                const char *mend = strstr(mp, "\"wra\"");

                if (mend == NULL) mend = line + strlen(line);

                for (const char *q = mp + 9; q + 1 < mend; ++q)
                    if (q[0] == '[' && q[1] == '"') ++want_n;
            }

            if (want_n != ps_nmarks)
            {
                fprintf(stderr, "NATMARKS:");
                for (int i = 0; i < ps_nmarks; ++i) fprintf(stderr, " %s", ps_stage_names[ps_marks[i].stage]);
                fprintf(stderr, "\nORAMARKS:");
                const char *q2 = strstr(line, "\"marks\":[");
                while (q2 && (q2 = strchr(q2, '[')) != NULL && q2[1] == '"')
                {
                    const char *e3 = strchr(q2 + 2, '"');
                    fprintf(stderr, " %.*s", (int)(e3 - q2 - 2), q2 + 2);
                    q2 = e3;
                }
                fprintf(stderr, "\n");
                fail("%s: call (%d,%d): %d stage marks, the oracle has %d", dir, cx, cz,
                     ps_nmarks, want_n);
                rc = 1;
                break;
            }

            mp = strstr(line, "\"marks\":[");

            for (int i = 0; i < want_n && !rc; ++i)
            {
                mp = strchr(mp, '[');

                while (*mp == '[') ++mp;

                if (*mp == '"') ++mp;   /* into the name */
                int stage = -1;

                for (int t = 0; t < POP_STAGES; ++t)
                {
                    size_t ln = strlen(ps_stage_names[t]);

                    if (strncmp(mp, ps_stage_names[t], ln) == 0 && mp[ln] == '"')
                    {
                        stage = t;
                        break;
                    }
                }

                if (stage < 0)
                {
                    fprintf(stderr, "UNK %d at %.28s\n", i, mp);
                    fail("%s: call (%d,%d) mark %d: unknown stage name", dir, cx, cz, i);
                    rc = 1;
                    break;
                }

                const char *comma = strchr(mp, ',');

                if (comma == NULL)
                {
                    fail("%s: call (%d,%d) mark %d: no seed in the record", dir, cx, cz, i);
                    rc = 1;
                    break;
                }

                uint64_t want = (uint64_t)strtoull(comma + 1, NULL, 10);

                if (ps_marks[i].stage != stage || ps_marks[i].seed != want)
                {
                    fail("%s: call (%d,%d) mark %d: want %s at %llu, got %s at %llu", dir, cx, cz, i,
                         ps_stage_names[stage], (unsigned long long)want,
                         ps_stage_names[ps_marks[i].stage], (unsigned long long)ps_marks[i].seed);
                    rc = 1;
                    break;
                }

                mp = strchr(mp, ']');
                if (mp == NULL) break;
            }
        }

        if (rc) break;

        /* the leaving World.rand state */
        if ((uint64_t)wr.seed != wra)
        {
            fail("%s: call (%d,%d): World.rand want %012llx got %012llx", dir, cx, cz,
                 (unsigned long long)wra, (unsigned long long)wr.seed);
            rc = 1;
            break;
        }

        /* the call's spawn records, in spawn order */
        int want_n = 0;
        {
            const char *sp = strstr(line, "\"spawns\":[");
            if (sp != NULL)
            {
                const char *q = strchr(sp + 10, ']');
                /* count the top-level { } groups between the brackets */
                const char *p = sp + 10;
                int depth = 0;
                while (p != q && *p)
                {
                    if (*p == '{') { if (depth == 0) ++want_n; ++depth; }
                    else if (*p == '}') --depth;
                    ++p;
                }
            }
        }

        if (want_n != sp.nout)
        {
            fail("%s: call (%d,%d): %d spawn records, the oracle has %d", dir, cx, cz,
                 sp.nout, want_n);
            rc = 1;
            break;
        }

        int bad = 0;

        for (int i = 0; i < sp.nout && !bad; ++i)
        {
            /* each record's json object, as "{...}" between the brackets */
            char rec[4096];
            const char *p = strstr(line, "\"spawns\":[");
            int depth = 0, seen = 0;
            const char *s = NULL, *e2 = NULL;

            while (p && *p && *p != ']')
            {
                if (*p == '{')
                {
                    if (depth == 0) { if (seen == i) s = p; ++seen; }
                    ++depth;
                }
                else if (*p == '}')
                {
                    --depth;
                    if (depth == 0 && s != NULL) { e2 = p + 1; break; }
                }
                ++p;
            }

            if (s == NULL || e2 == NULL) { fail("%s: call (%d,%d): record %d not found", dir, cx, cz, i); bad = 1; break; }

            size_t n = (size_t)(e2 - s);
            if (n >= sizeof rec) n = sizeof rec - 1;
            memcpy(rec, s, n);
            rec[n] = 0;

            const struct sp_rec *r = sp.out[i];
            ++nspawns;

            const char *cls = value_of(rec, "cls");

            if (cls == NULL || strncmp(cls, kind_class(r->kind), strlen(kind_class(r->kind))) != 0)
            { fail("%s: call (%d,%d) spawn %d: want %s got %s", dir, cx, cz, i,
                   cls == NULL ? "?" : short_value(cls), kind_class(r->kind)); bad = 1; break; }

            if (as_double(value_of(rec, "x")) != r->x || as_double(value_of(rec, "y")) != r->y ||
                as_double(value_of(rec, "z")) != r->z)
            { fail("%s: call (%d,%d) spawn %d (%s): want (%.17g,%.17g,%.17g) got (%.17g,%.17g,%.17g)",
                   dir, cx, cz, i, kind_class(r->kind), r->x, r->y, r->z,
                   as_double(value_of(rec, "x")), as_double(value_of(rec, "y")), as_double(value_of(rec, "z")));
              bad = 1; break; }

            if (as_float(value_of(rec, "yaw")) != r->yaw)
            { fail("%s: call (%d,%d) spawn %d (%s): yaw want %s got %08x", dir, cx, cz, i,
                   kind_class(r->kind), short_value(value_of(rec, "yaw")), fbits(r->yaw)); bad = 1; break; }

            if (as_hex48(value_of(rec, "rnd")) != r->rnd)
            { fail("%s: call (%d,%d) spawn %d (%s): rnd want %s got %012llx", dir, cx, cz, i,
                   kind_class(r->kind), short_value(value_of(rec, "rnd")),
                   (unsigned long long)r->rnd); bad = 1; break; }

            if (as_long(value_of(rec, "uuidM")) != r->uuid_msb || as_long(value_of(rec, "uuidL")) != r->uuid_lsb)
            { fail("%s: call (%d,%d) spawn %d (%s): uuid want (%s,%s) got (%lld,%lld)", dir, cx, cz, i,
                   kind_class(r->kind), short_value(value_of(rec, "uuidM")), short_value(value_of(rec, "uuidL")),
                   (long long)r->uuid_msb, (long long)r->uuid_lsb); bad = 1; break; }

            const char *v = value_of(rec, "fleece");

            if (v != NULL && (int)as_long(v) != r->fleece)
            { fail("%s: call (%d,%d) spawn %d: fleece want %s got %d", dir, cx, cz, i, v, r->fleece);
              bad = 1; break; }
        }

        if (!bad) ++ncalls_exact;
        else rc = 1;
    }

    gzclose(cg);

    if (calls != calls_want)
        fail("%s: the manifest says %lld calls, calls.jsonl.gz holds %lld", dir,
             (long long)calls_want, calls);

    if (nspawns != spawns_want)
        fail("%s: the manifest says %lld spawns, the calls hold %lld", dir,
             (long long)spawns_want, nspawns);

    if (rc == 0 && calls == calls_want)
        printf("SPAWNING %s: %lld populate calls exact, %lld worldgen spawns, pass\n", dir,
               calls, nspawns);
    else
        printf("SPAWNING %s: %lld of %lld populate calls exact\n", dir, ncalls_exact, calls);

    spawner_free(&sp);
    populate_free(&pop);
    return rc;
}

/* --------------------------------------------------------- the scene kind */

/* The squid and bat scene (SpawnProbe's "scene" kind): the ORACLE's water
 * arena and bat chamber over recorded raw chunks, the scene's squids and bats
 * spawned from spawns.bin and ticked on the probe's list, their 272-byte
 * state records compared byte for byte against ticks.bin.gz's. */
static int64_t scene_int(const char *manifest, const char *key)
{
    return nested_int(manifest, "probe", key);
}

static int scene_run(const char *dir, const char *manifest)
{
    int64_t ticks = scene_int(manifest, "ticks");
    double px = as_double(value_of(manifest, "px"));
    double pz = as_double(value_of(manifest, "pz"));
    int ground_y = (int)scene_int(manifest, "groundY");
    int bx = (int)scene_int(manifest, "bx");
    int bz = (int)scene_int(manifest, "bz");
    int cx0 = (int)scene_int(manifest, "cx0");
    int cz0 = (int)scene_int(manifest, "cz0");
    int cy0 = (int)scene_int(manifest, "cy0");
    int64_t seed = manifest_int(manifest, "seed");

    /* the world and the 7x7 raw ring */
    struct world w;
    world_init(&w, seed);
    w.dim = 0;

    ps_raw_init(dir);

    int pcx = (int)mh_floor(px / 16.0), pcz = (int)mh_floor(pz / 16.0);

    for (int lx = pcx - 3; lx <= pcx + 3; ++lx)
        for (int lz = pcz - 3; lz <= pcz + 3; ++lz)
        {
            world_load_chunk(&w, lx, lz);
            ps_raw_patch(&w, lx, lz);
        }

    /* Det as start.txt holds it */
    struct ps_ref ref;
    memset(&ref, 0, sizeof ref);
    ps_read_ref(&ref, dir);

    if (fails) return 1;

    det_state det;
    det_init(&det);

    uint64_t seeders[4] = { ref.seeder[0], ref.seeder[1], ref.seeder[2], ref.seeder[3] };
    uint64_t maths[4] = { ref.math[0], ref.math[1], ref.math[2], ref.math[3] };
    int nexts[4] = { ref.next_id[0], ref.next_id[1], ref.next_id[2], ref.next_id[3] };
    det_load(&det, seed, seeders, maths, nexts);

    /* start.txt's worldRand line */
    uint64_t world_rand_state = 0;
    int world_rand_gauss = 0;
    {
        snprintf(path, sizeof path, "%s/start.txt", dir);
        FILE *f = fopen(path, "r");
        char line[512];

        while (f && fgets(line, sizeof line, f))
        {
            unsigned long long a;

            if (sscanf(line, "worldRand %llx %d", &a, &world_rand_gauss) == 2)
            {
                world_rand_state = a;
                break;
            }
        }

        if (f) fclose(f);
    }

    struct an_world an;
    an_init(&an, &w, &det);
    an.skylight = 0;

    /* ------------------------------------------------------------ the arena */
    int gy = ground_y;

    for (int x = 0; x < 48; ++x)
        for (int z = 0; z < 32; ++z)
        {
            for (int y = gy; y < gy + 1; ++y) world_set_block(&w, bx + x, y, bz + z, 3, 0, 3);
            for (int y = gy + 1; y < gy + 12; ++y) world_set_block(&w, bx + x, y, bz + z, 0, 0, 3);
        }

    for (int x = 8; x < 24; ++x)
        for (int z = 0; z < 32; ++z)
        {
            int depth = x < 16 ? 7 : 4;
            for (int y = gy + 1; y <= gy + 1 + depth; ++y) world_set_block(&w, bx + x, y, bz + z, 9, 0, 3);
        }

    for (int x = 0; x < 48; ++x)
    {
        world_set_block(&w, bx + x, gy + 8, bz, 4, 0, 3);
        world_set_block(&w, bx + x, gy + 8, bz + 31, 4, 0, 3);
    }

    for (int z = 0; z < 32; ++z)
    {
        world_set_block(&w, bx, gy + 8, bz + z, 4, 0, 3);
        world_set_block(&w, bx + 47, gy + 8, bz + z, 4, 0, 3);
    }

    for (int x = 0; x < 9; ++x)
        for (int z = 0; z < 5; ++z)
            for (int y = 0; y < 4; ++y)
                world_set_block(&w, cx0 + x, cy0 + y, cz0 + z, 0, 0, 3);

    for (int x = -1; x <= 9; ++x)
        for (int z = -1; z <= 5; ++z)
        {
            world_set_block(&w, cx0 + x, cy0 - 1, cz0 + z, 4, 0, 3);
            world_set_block(&w, cx0 + x, cy0 + 4, cz0 + z, 4, 0, 3);
        }

    for (int y = 0; y < 4; ++y)
    {
        for (int z = -1; z <= 5; ++z)
        {
            world_set_block(&w, cx0 - 1, cy0 + y, cz0 + z, 4, 0, 3);
            world_set_block(&w, cx0 + 9, cy0 + y, cz0 + z, 4, 0, 3);
        }

        for (int x = -1; x <= 9; ++x)
        {
            world_set_block(&w, cx0 + x, cy0 + y, cz0 - 1, 4, 0, 3);
            world_set_block(&w, cx0 + x, cy0 + y, cz0 + 5, 4, 0, 3);
        }
    }

    world_set_block(&w, cx0 - 1, cy0, cz0 + 2, 0, 0, 3);

    /* the scene's parked player on the dry ledge */
    an.has_player = 1;
    an.player_x = px;
    an.player_y = (double)cy0 + 1.0;
    an.player_z = pz + 2.5;

    /* ------------------------------------------------------------- the spawns */
    snprintf(path, sizeof path, "%s/spawns.bin", dir);
    size_t spnlen;
    unsigned char *spawns = (unsigned char *)probe_read_file(path, &spnlen);

    if (spawns == NULL || spnlen % 100 != 0)
    {
        printf("FAIL %s: spawns.bin is missing or not a 100-byte multiple\n", dir);
        return 1;
    }

    int nents = (int)(spnlen / 100);

    for (int i = 0; i < nents; ++i)
    {
        const unsigned char *s = spawns + (size_t)i * 100;
        int index = (int)le32(s);
        int kind = (int)le32(s + 8) == 0 ? AK_SQUID : AK_BAT;
        double x = le_double(s + 12), y = le_double(s + 20), z = le_double(s + 28);
        float yaw = le_float(s + 36);

        struct living *l = an_spawn_living(&an, kind, index, x, y, z, yaw, 0.0F, 0, 0, 0, 0, 0, 1);

        if (!l)
        {
            printf("FAIL %s: could not spawn scene entity %d\n", dir, i);
            return 1;
        }
    }

    free(spawns);

    /* ------------------------------------------------------------- the ticks */
    snprintf(path, sizeof path, "%s/ticks.bin.gz", dir);
    gzFile tf = gzopen(path, "rb");
    snprintf(path, sizeof path, "%s/digest.txt.gz", dir);
    gzFile df = gzopen(path, "rb");

    if (!tf || !df)
    {
        printf("FAIL %s: the tick streams will not open\n", dir);
        return 1;
    }

    unsigned char rec[272], mine[272];
    char line[65536];
    int rc = 0;
    int exact = 0;

    for (int64_t t = 0; t < ticks && !rc; ++t)
    {
        an_tick(&an, (int)t);

        unsigned char head[8];

        if (gzread(tf, head, 8) != 8)
        {
            fail("%s: no tick header at tick %lld", dir, (long long)t);
            rc = 1;
            break;
        }

        int want_tick = (int)le32(head);
        int want_count = (int)le32(head + 4);

        if (want_tick != (int)t || want_count != an.n)
        {
            fail("%s: tick %lld: the oracle recorded tick %d with %d live entities, the replay has %d",
                 dir, (long long)t, want_tick, want_count, an.n);
            rc = 1;
            break;
        }

        for (int i = 0; i < want_count && !rc; ++i)
        {
            if (gzread(tf, rec, 272) != 272)
            {
                fail("%s: tick %lld: the records end inside the tick", dir, (long long)t);
                rc = 1;
                break;
            }

            if (i >= an.n) break;

            an_write_state(&an, an_ent_at(an.slot[i]), mine);

            if (memcmp(rec, mine, 272) != 0)
            {
                int off = -1;

                for (int k = 0; k < 272; ++k)
                    if (rec[k] != mine[k]) { off = k; break; }

                const char *field = off < 4 ? "spawn index"
                    : (off < 8 ? "entity id" : off < 12 ? "kind"
                       : off < 20 ? "nbt hash" : off < 28 ? "age/ticks"
                       : off < 76 ? "ai"
                       : off < 136 ? "nav"
                       : off < 172 ? "moveHelper"
                       : off < 208 ? "lookHelper"
                       : off < 212 ? "jump"
                       : off < 228 ? "move/body"
                       : off < 240 ? "sound/body"
                       : off < 256 ? "timers"
                       : off < 264 ? "flags"
                       : "rand state");


                fail("%s: tick %lld entity %d: the state differs at byte %d (%s):"
                     " oracle %02x, replay %02x (word at %d: oracle %08x replay %08x)",
                     dir, (long long)t, i, off, field, (unsigned)rec[off], (unsigned)mine[off],
                     off & ~3,
                     (unsigned)((uint32_t)rec[off & ~3] | (uint32_t)rec[(off & ~3) + 1] << 8 |
                                (uint32_t)rec[(off & ~3) + 2] << 16 | (uint32_t)rec[(off & ~3) + 3] << 24),
                     (unsigned)((uint32_t)mine[off & ~3] | (uint32_t)mine[(off & ~3) + 1] << 8 |
                                (uint32_t)mine[(off & ~3) + 2] << 16 | (uint32_t)mine[(off & ~3) + 3] << 24));
                rc = 1;
                break;
            }
        }

        if (rc) break;

        /* the digest line: t <tick>, the four roles' states, the OTHER next id,
         * World.rand and its pending-gaussian flag */
        if (gzgets(df, line, sizeof line))
        {
            int dt = -1;
            unsigned long long sr0, ma0, sp0, sr1, ma1, sp1, sr2, ma2, sp2, sr3, ma3, sp3, wrd;
            long long nid = -1;
            int wg = -1;

            if (sscanf(line,
                    "t %d role 0 %llx %llx %llx role 1 %llx %llx %llx role 2 %llx %llx %llx role 3 %llx %llx %llx nextId %lld worldRand %llx worldRandGauss %d",
                    &dt, &sr0, &ma0, &sp0, &sr1, &ma1, &sp1, &sr2, &ma2, &sp2,
                    &sr3, &ma3, &sp3, &nid, &wrd, &wg) == 16)
            {
                if ((uint64_t)det_seeder_state(&det, 0) != sr0 || (uint64_t)det_math_state(&det, 0) != ma0 ||
                    (uint64_t)det_seeder_state(&det, 1) != sr1 || (uint64_t)det_math_state(&det, 1) != ma1 ||
                    (uint64_t)det_seeder_state(&det, 2) != sr2 || (uint64_t)det_math_state(&det, 2) != ma2 ||
                    (uint64_t)det_seeder_state(&det, 3) != sr3 || (uint64_t)det_math_state(&det, 3) != ma3 ||
                    (uint64_t)det.next_id[2] != (uint64_t)nid)
                {
                    fail("%s: tick %lld: the digest's Det states differ", dir, (long long)t);
                    rc = 1;
                    break;
                }

                if ((uint64_t)world_rand_state != wrd ||
                    (world_rand_gauss ? 1 : 0) != wg)
                {
                    fail("%s: tick %lld: the digest's World.rand want %012llx got %012llx", dir,
                         (long long)t, (unsigned long long)wrd,
                         (unsigned long long)world_rand_state);
                    rc = 1;
                    break;
                }
            }
            else
            {
                fail("%s: tick %lld: the digest line does not parse", dir, (long long)t);
                rc = 1;
                break;
            }
        }

        if (!rc) ++exact;
    }

    gzclose(tf);
    gzclose(df);

    an_free(&an);
    world_free(&w);
    free(ps_rawbin);

    if (rc == 0)
        printf("SPAWNING %s: %lld scene ticks exact, %d entities, pass\n", dir,
               (long long)ticks, nents);
    else
        printf("SPAWNING %s: %d of %lld scene ticks exact\n", dir, exact, (long long)ticks);

    return rc;
}

/* --------------------------------------------------------- the spawner kind */

/* The tick spawner (SpawnProbe's "spawner" kind): the snapshot's world, the
 * parked player, the active chunk set in its recorded order and the
 * chunkstate's inhabitedTime, then SpawnerAnimals.findChunksForSpawning run
 * where WorldServer.tick runs it (the servertick hook), tick after tick, and
 * every spawn the tick made (the spawner's mob records, then the tick body's
 * own falling blocks and items) against spawns.jsonl.gz, plus the write rows
 * against tickwrites.bin and the scalars against tickrows.jsonl.gz. */

/* A "d:...." value out of the manifest's probe block. */
static double probe_double(const char *manifest, const char *key)
{
    const char *p = strstr(manifest, "\"probe\":{");
    char kpat[48];

    if (p == NULL) return 0.0;

    snprintf(kpat, sizeof kpat, "\"%s\":\"", key);
    p = strstr(p, kpat);

    if (p == NULL) return 0.0;

    return as_double(p + strlen(kpat));
}

/* The probe's active chunk set, [cx,cz] per entry, in the recorded iteration
 * order. Returns a malloc'd array of 2*n ints. */
static int *probe_active(const char *manifest, int *n_out)
{
    const char *p = strstr(manifest, "\"active\":[");
    int cap = 1024, n = 0;
    int *out = malloc((size_t)cap * 2 * sizeof *out);

    *n_out = 0;

    if (p == NULL) return out;

    p += strlen("\"active\":[");

    while (*p == '[')
    {
        int cx, cz;

        if (sscanf(p, "[%d,%d]", &cx, &cz) != 2) break;

        if (n == cap) { cap *= 2; out = realloc(out, (size_t)cap * 2 * sizeof *out); }

        out[n * 2] = cx;
        out[n * 2 + 1] = cz;
        ++n;

        const char *q = strchr(p, ']');

        if (q == NULL) break;

        p = q + 1;
        if (*p == ',') ++p;
    }

    *n_out = n;
    return out;
}

/* One slot of a spawn line's "eq" array: the item id, damage, count and the
 * tag text, unescaped (empty when the slot carries none). 0 when the array
 * does not parse. */
static int sp_eq_slot(const char *line, int slot, int *item, int *dmg, int *cnt, char *tag, size_t tcap)
{
    const char *p = strstr(line, "\"eq\":[");

    if (p == NULL) return 0;

    p += strlen("\"eq\":[");

    for (int k = 0; k <= slot; ++k)
    {
        while (*p == ',' || *p == ' ') ++p;

        if (*p != '[') return 0;
        ++p;

        int a = 0, b = 0, c = 0, adv = 0;

        if (sscanf(p, "%d,%d,%d,%n", &a, &b, &c, &adv) < 3 || adv <= 0) return 0;

        p += adv;

        if (*p != '"') return 0;
        ++p;

        size_t w = 0;

        while (*p != 0 && *p != '"')
        {
            char ch = *p;

            if (ch == '\\' && p[1] != 0)
            {
                ++p;
                ch = *p;

                if (ch == 'n') ch = '\n';
                else if (ch == 't') ch = '\t';
                else if (ch == 'r') ch = '\r';
                else if (ch == 'b') ch = '\b';
                else if (ch == 'f') ch = '\f';
            }

            if (k == slot && w + 1 < tcap) tag[w++] = ch;

            ++p;
        }

        if (*p != '"') return 0;
        ++p;

        if (k == slot) { tag[w] = 0; *item = a; *dmg = b; *cnt = c; }

        while (*p == ',' || *p == ' ') ++p;

        if (*p != ']') return 0;
        ++p;
    }

    return 1;
}

/* One mob record against the oracle's spawn line. 1 on the first difference,
 * with the tick, the spawn and both values named. */
static int sp_cmp_mob(const char *dir, int t, int i, const char *line, const struct sp_rec *r)
{
    const char *cls = value_of(line, "cls");
    const char *k = kind_class(r->kind);

    if (cls == NULL || strncmp(cls, k, strlen(k)) != 0)
    {
        fail("%s: tick %d spawn %d: cls want %s got %s", dir, t, i, short_value(cls), k);
        return 1;
    }

    if (as_long(value_of(line, "id")) != r->id)
    {
        fail("%s: tick %d spawn %d (%s): id want %s got %d", dir, t, i, k,
             short_value(value_of(line, "id")), r->id);
        return 1;
    }

    if (as_double(value_of(line, "x")) != r->x || as_double(value_of(line, "y")) != r->y ||
        as_double(value_of(line, "z")) != r->z)
    {
        fail("%s: tick %d spawn %d (%s): pos want (%.17g,%.17g,%.17g) got (%.17g,%.17g,%.17g)",
             dir, t, i, k, as_double(value_of(line, "x")), as_double(value_of(line, "y")),
             as_double(value_of(line, "z")), r->x, r->y, r->z);
        return 1;
    }

    if (as_float(value_of(line, "yaw")) != r->yaw)
    {
        fail("%s: tick %d spawn %d (%s): yaw want %s got %08x", dir, t, i, k,
             short_value(value_of(line, "yaw")), fbits(r->yaw));
        return 1;
    }

    if (as_hex48(value_of(line, "rnd")) != r->rnd)
    {
        fail("%s: tick %d spawn %d (%s): rnd want %s got %012llx", dir, t, i, k,
             short_value(value_of(line, "rnd")), (unsigned long long)r->rnd);
        return 1;
    }

    if (as_long(value_of(line, "uuidM")) != r->uuid_msb || as_long(value_of(line, "uuidL")) != r->uuid_lsb)
    {
        fail("%s: tick %d spawn %d (%s): uuid want (%s,%s) got (%lld,%lld)", dir, t, i, k,
             short_value(value_of(line, "uuidM")), short_value(value_of(line, "uuidL")),
             (long long)r->uuid_msb, (long long)r->uuid_lsb);
        return 1;
    }

    if (as_float(value_of(line, "ap")) != r->field_70770_ap || as_float(value_of(line, "ao")) != r->field_70769_ao)
    {
        fail("%s: tick %d spawn %d (%s): ap/ao want (%s,%s) got (%08x,%08x)", dir, t, i, k,
             short_value(value_of(line, "ap")), short_value(value_of(line, "ao")),
             fbits(r->field_70770_ap), fbits(r->field_70769_ao));
        return 1;
    }

    const char *v;

    v = value_of(line, "villager");

    if (v != NULL && (int)as_long(v) != r->villager)
    {
        fail("%s: tick %d spawn %d (%s): villager want %s got %d", dir, t, i, k, v, r->villager);
        return 1;
    }

    v = value_of(line, "pickup");

    if (v != NULL && (int)as_long(v) != r->pickup)
    {
        fail("%s: tick %d spawn %d (%s): pickup want %s got %d", dir, t, i, k, v, r->pickup);
        return 1;
    }

    v = value_of(line, "fleece");

    if (v != NULL && (int)as_long(v) != r->fleece)
    {
        fail("%s: tick %d spawn %d (%s): fleece want %s got %d", dir, t, i, k, v, r->fleece);
        return 1;
    }

    v = value_of(line, "slime");

    if (v != NULL && (int)as_long(v) != r->slime_size)
    {
        fail("%s: tick %d spawn %d (%s): slime want %s got %d", dir, t, i, k, v, r->slime_size);
        return 1;
    }

    v = value_of(line, "skel");

    if (v != NULL && (int)as_long(v) != r->skel_type)
    {
        fail("%s: tick %d spawn %d (%s): skel want %s got %d", dir, t, i, k, v, r->skel_type);
        return 1;
    }

    v = value_of(line, "riding");

    if (v != NULL && (int)as_long(v) != r->riding)
    {
        fail("%s: tick %d spawn %d (%s): riding want %s got %d", dir, t, i, k, v, r->riding);
        return 1;
    }

    v = value_of(line, "riddenBy");

    if (v != NULL && (int)as_long(v) != r->ridden_by)
    {
        fail("%s: tick %d spawn %d (%s): riddenBy want %s got %d", dir, t, i, k, v, r->ridden_by);
        return 1;
    }

    for (int s = 0; s < 5; ++s)
    {
        int item = 0, dmg = 0, cnt = 0;
        char tag[1024];

        if (!sp_eq_slot(line, s, &item, &dmg, &cnt, tag, sizeof tag))
        {
            fail("%s: tick %d spawn %d (%s): the eq array does not parse", dir, t, i, k);
            return 1;
        }

        if (item != r->eq_item[s] || dmg != r->eq_dmg[s] || cnt != r->eq_cnt[s])
        {
            fail("%s: tick %d spawn %d (%s): eq %d want (%s,%s,%s) got (%d,%d,%d)", dir, t, i, k, s,
                 short_value(value_of(line, "eq")), "?", "?", r->eq_item[s], r->eq_dmg[s], r->eq_cnt[s]);
            return 1;
        }

        const char *want = r->eq_tag[s] ? itag_text(r->eq_tag[s]) : "";

        if (strcmp(tag, want) != 0)
        {
            fail("%s: tick %d spawn %d (%s): eq %d tag want %.100s got %.100s", dir, t, i, k, s, want, tag);
            return 1;
        }
    }

    return 0;
}

/* One tick-body spawn (falling block, item, lightning) against its line: the
 * fields the two share at the line's top level. */
static int sp_cmp_tick_spawn(const char *dir, int t, int i, const char *line, const struct st_spawn *sp)
{
    const char *cls = value_of(line, "cls");

    if (cls == NULL || strncmp(cls, sp->cls, strlen(sp->cls)) != 0)
    {
        fail("%s: tick %d spawn %d: cls want %s got %s", dir, t, i, short_value(cls), sp->cls);
        return 1;
    }

    if (as_long(value_of(line, "id")) != sp->id)
    {
        fail("%s: tick %d spawn %d (%s): id want %s got %d", dir, t, i, sp->cls,
             short_value(value_of(line, "id")), sp->id);
        return 1;
    }

    if (as_double(value_of(line, "x")) != sp->x || as_double(value_of(line, "y")) != sp->y ||
        as_double(value_of(line, "z")) != sp->z)
    {
        fail("%s: tick %d spawn %d (%s): pos want (%.17g,%.17g,%.17g) got (%.17g,%.17g,%.17g)", dir, t, i,
             sp->cls, as_double(value_of(line, "x")), as_double(value_of(line, "y")),
             as_double(value_of(line, "z")), sp->x, sp->y, sp->z);
        return 1;
    }

    if (as_float(value_of(line, "yaw")) != sp->yaw)
    {
        fail("%s: tick %d spawn %d (%s): yaw want %s got %08x", dir, t, i, sp->cls,
             short_value(value_of(line, "yaw")), fbits(sp->yaw));
        return 1;
    }

    if (as_long(value_of(line, "uuidM")) != sp->uuid_msb || as_long(value_of(line, "uuidL")) != sp->uuid_lsb)
    {
        fail("%s: tick %d spawn %d (%s): uuid want (%s,%s) got (%lld,%lld)", dir, t, i, sp->cls,
             short_value(value_of(line, "uuidM")), short_value(value_of(line, "uuidL")),
             (long long)sp->uuid_msb, (long long)sp->uuid_lsb);
        return 1;
    }

    return 0;
}

static int64_t sp_fold(int64_t h, int64_t v)
{
    return (h ^ v) * 0x100000001b3LL;
}

/* A heap copy of a line out of a lines_ buffer. */
static char *sp_dup(const char *s)
{
    size_t n = strlen(s);
    char *out = malloc(n + 1);

    memcpy(out, s, n + 1);
    return out;
}

static int spawner_run(const char *dir, const char *manifest)
{
    int64_t seed = manifest_int(manifest, "seed");
    int64_t ticks = nested_int(manifest, "probe", "ticks");
    int64_t cal_month = nested_int(manifest, "probe", "calMonth");
    int64_t cal_day = nested_int(manifest, "probe", "calDay");
    int64_t difficulty = nested_int(manifest, "probe", "difficulty");
    int64_t ambient = nested_int(manifest, "probe", "ambientTickCountdown");
    int64_t spawns_want = nested_int(manifest, "probe", "spawns");
    double px = probe_double(manifest, "px");
    double py = probe_double(manifest, "py");
    double pz = probe_double(manifest, "pz");

    struct snapshot s;

    if (!snapshot_load(&s, dir))
    {
        printf("FAIL %s: snapshot_load\n", dir);
        return 1;
    }

    struct servertick st;

    if (!servertick_load(&st, &s, dir))
    {
        printf("FAIL %s: servertick_load\n", dir);
        snapshot_free(&s);
        return 1;
    }

    if (nested_int(manifest, "probe", "noChunkGeneration") == 1) st.w->no_generate = 1;

    if (ambient == INT64_MIN) fail("%s: the manifest carries no ambientTickCountdown", dir);
    else servertick_set_ambient(&st, (int)ambient, (int)difficulty);

    servertick_set_player(&st, px, py, pz);

    {
        int na = 0;
        int *pairs = probe_active(manifest, &na);

        servertick_set_active(&st, pairs, na);
        free(pairs);
    }

    /* Det's streams, the snapshot's */
    det_state det;
    det_init(&det);

    if (s.det == NULL) fail("%s: the snapshot holds no det.nbt", dir);
    else servertick_load_det(&st, &det, s.det);

    /* the pending set, in the tree set's order */
    ticks_set_immediate(0);
    ticks_reset(st.next_tick_entry);

    for (int i = 0; i < s.nticks; ++i)
    {
        struct tick_entry e;

        e.x = s.ticks[i].x;
        e.y = s.ticks[i].y;
        e.z = s.ticks[i].z;
        e.block = s.ticks[i].id;
        e.time = s.ticks[i].scheduled;
        e.priority = s.ticks[i].priority;
        e.entry = s.ticks[i].entry;
        ticks_load_entry(&e);
    }

    if (ticks_pending_count() != s.nticks)
        fail("%s: %d pending entries loaded, the snapshot holds %d", dir, ticks_pending_count(), s.nticks);

    /* ---------------------------------------------------------- the spawner */
    struct spawner sp;
    spawner_init(&sp, st.w, &det, &ST_RAND(&st));

    int spx = (int)nbt_int_value(nbt_get(s.worldinfo, "SpawnX"));
    int spy = (int)nbt_int_value(nbt_get(s.worldinfo, "SpawnY"));
    int spz = (int)nbt_int_value(nbt_get(s.worldinfo, "SpawnZ"));
    spawner_set_scene(&sp, seed, (int)difficulty, (int)cal_month, (int)cal_day, spx, spy, spz);
    spawner_set_player(&sp, px, py, pz);

    if (neg_cap) { sp.neg_cap_type = CT_MONSTER; sp.neg_cap_add = 40; }
    if (neg_pack) sp.neg_pack = 1;

    st.on_spawner = spawner_tick;
    st.on_spawner_ctx = &sp;

    /* ------------------------------------------------- the oracle's streams */
    char **rows = NULL;
    int nrows = 0;
    char **slines = NULL;
    int nslines = 0;
    unsigned char *wbytes = NULL;
    size_t wlen = 0;

    snprintf(path, sizeof path, "%s/tickrows.jsonl.gz", dir);
    gzFile rg = gzopen(path, "rb");

    if (rg == NULL)
    {
        printf("FAIL %s: cannot read tickrows.jsonl.gz\n", dir);
        snapshot_free(&s);
        return 1;
    }

    rows = malloc(sizeof *rows * 8192);
    {
        const char *line;
        struct lines in;
        lines_init(&in);
        lines_gz(&in, rg);

        while ((line = lines_next(&in)) != NULL)
        {
            if (!line[0]) continue;

            rows[nrows++] = sp_dup(line);
        }
    }

    gzclose(rg);

    snprintf(path, sizeof path, "%s/spawns.jsonl.gz", dir);
    gzFile sg = gzopen(path, "rb");

    slines = malloc(sizeof *slines * 4096);
    {
        const char *line;
        struct lines in;
        lines_init(&in);

        if (sg != NULL) lines_gz(&in, sg);

        while (sg != NULL && (line = lines_next(&in)) != NULL)
        {
            if (!line[0]) continue;

            slines[nslines++] = sp_dup(line);
        }
    }

    if (sg != NULL) gzclose(sg);

    snprintf(path, sizeof path, "%s/tickwrites.bin", dir);
    wbytes = (unsigned char *)probe_read_file(path, &wlen);

    if (wbytes == NULL)
    {
        printf("FAIL %s: cannot read tickwrites.bin\n", dir);
        snapshot_free(&s);
        return 1;
    }

    /* ------------------------------------------------------------- the ticks */
    printf("     spawner: seed %lld dayTime start %lld, player (%.3f, %.3f, %.3f), spawn (%d,%d,%d),"
           " %d active chunks, %d pending, difficulty %lld\n",
           (long long)seed, (long long)st.world_time, px, py, pz, spx, spy, spz, s.nchunks,
           ticks_pending_count(), (long long)difficulty);

    int si = 0, exact = 0, stopped = 0;
    size_t woff = 0;
    long long mobs_done = 0, bodies_done = 0;

    for (int r = 0; r < nrows && !stopped; ++r)
    {
        const char *row = rows[r];
        int t = (int)as_long(value_of(row, "t"));

        if (t != r + 1)
        {
            fail("%s: tickrows row %d carries t=%d", dir, r, t);
            stopped = 1;
            break;
        }

        servertick_clear(&st);
        servertick_tick(&st, t);

        /* 1. the spawns: the spawner's mob records first (created at the tick
         * head), then the tick body's own */
        int ns = 0;
        const struct st_spawn *bodys = servertick_spawns(&st, &ns);
        int want_spawn = (int)as_long(value_of(row, "spawns"));
        int total = sp.nout + ns;

        if (total != want_spawn)
        {
            fail("%s: tick %d spawns want %d got %d (%d mobs + %d tick-body)", dir, t, want_spawn, total,
                 sp.nout, ns);
            stopped = 1;
            break;
        }

        for (int i = 0; i < sp.nout && !stopped; ++i, ++si)
        {
            if (si >= nslines)
            {
                fail("%s: tick %d spawn %d: spawns.jsonl.gz is short", dir, t, i);
                stopped = 1;
                break;
            }

            if (sp_cmp_mob(dir, t, i, slines[si], sp.out[i])) stopped = 1;
        }

        for (int i = 0; i < ns && !stopped; ++i, ++si)
        {
            if (si >= nslines)
            {
                fail("%s: tick %d tick spawn %d: spawns.jsonl.gz is short", dir, t, i);
                stopped = 1;
                break;
            }

            if (sp_cmp_tick_spawn(dir, t, sp.nout + i, slines[si], &bodys[i])) stopped = 1;
        }

        mobs_done += sp.nout;
        bodies_done += ns;

        /* 2. the write rows, byte for byte */
        int nw = 0;
        const struct st_write *w = servertick_writes(&st, &nw);
        int64_t want_writes = as_long(value_of(row, "writes"));

        if (!stopped && woff + (size_t)want_writes * 16 > wlen)
        {
            fail("%s: tick %d: tickwrites.bin is short", dir, t);
            stopped = 1;
        }
        else if (!stopped)
        {
            int common = nw < (int)want_writes ? nw : (int)want_writes;

            for (int i = 0; i < common && !stopped; ++i)
            {
                const unsigned char *b = wbytes + woff + (size_t)i * 16;
                int32_t x, y, z;
                int16_t id;
                uint8_t meta;

                memcpy(&x, b, 4);
                memcpy(&y, b + 4, 4);
                memcpy(&z, b + 8, 4);
                memcpy(&id, b + 12, 2);
                meta = b[14];

                if (x != w[i].x || y != w[i].y || z != w[i].z || id != w[i].id || meta != w[i].meta)
                {
                    fail("%s: tick %d write %d of %lld want (%d,%d,%d) id %d meta %d got (%d,%d,%d) id %d meta %d",
                         dir, t, i, (long long)want_writes, x, y, z, id, meta, w[i].x, w[i].y, w[i].z,
                         w[i].id, w[i].meta);
                    stopped = 1;
                }
            }

            if (!stopped && nw != (int)want_writes)
            {
                fail("%s: tick %d writes want %lld got %d", dir, t, (long long)want_writes, nw);
                stopped = 1;
            }

            if (!stopped) woff += (size_t)nw * 16;
        }

        /* 3. the scalars */
        if (!stopped)
        {
            struct { const char *name; const char *want; } f[18] = {
                {"time", value_of(row, "time")},
                {"total", value_of(row, "total")},
                {"rain", value_of(row, "rain")},
                {"thunder", value_of(row, "thunder")},
                {"rainTime", value_of(row, "rainTime")},
                {"thunderTime", value_of(row, "thunderTime")},
                {"rainS", value_of(row, "rainS")},
                {"thunderS", value_of(row, "thunderS")},
                {"prevRainS", value_of(row, "prevRainS")},
                {"prevThunderS", value_of(row, "prevThunderS")},
                {"sky", value_of(row, "sky")},
                {"amb", value_of(row, "amb")},
                {"rand", value_of(row, "rand")},
                {"lcg", value_of(row, "lcg")},
                {"sseed", value_of(row, "sseed")},
                {"smath", value_of(row, "smath")},
                {"sstat", value_of(row, "sstat")},
                {"sid", value_of(row, "sid")},
            };
            char buf[18][48];

            snprintf(buf[0], sizeof buf[0], "l:%lld", (long long)st.world_time);
            snprintf(buf[1], sizeof buf[1], "l:%lld", (long long)st.total_time);
            snprintf(buf[2], sizeof buf[2], "%d", st.raining);
            snprintf(buf[3], sizeof buf[3], "%d", st.thundering);
            snprintf(buf[4], sizeof buf[4], "%d", st.rain_time);
            snprintf(buf[5], sizeof buf[5], "%d", st.thunder_time);
            snprintf(buf[6], sizeof buf[6], "f:%08x", fbits(st.raining_strength));
            snprintf(buf[7], sizeof buf[7], "f:%08x", fbits(st.thundering_strength));
            snprintf(buf[8], sizeof buf[8], "f:%08x", fbits(st.prev_raining_strength));
            snprintf(buf[9], sizeof buf[9], "f:%08x", fbits(st.prev_thundering_strength));
            snprintf(buf[10], sizeof buf[10], "%d", st.skylight_subtracted);
            snprintf(buf[11], sizeof buf[11], "%d", st.ambient_tick_countdown);
            snprintf(buf[12], sizeof buf[12], "l:%llu", (unsigned long long)ST_RAND(&st).seed);
            snprintf(buf[13], sizeof buf[13], "%d", ST_LCG(&st));
            snprintf(buf[14], sizeof buf[14], "l:%lld", (long long)(int64_t)det_seeder_state(&det, DET_SERVER));
            snprintf(buf[15], sizeof buf[15], "l:%lld", (long long)(int64_t)det_math_state(&det, DET_SERVER));
            snprintf(buf[16], sizeof buf[16], "l:%lld", (long long)(int64_t)det_split_state(&det, DET_SERVER));
            snprintf(buf[17], sizeof buf[17], "%d", det.next_id[DET_SERVER]);

            for (int i = 0; i < 18 && !stopped; ++i)
            {
                int same;

                switch (i)
                {
                case 6: case 7: case 8: case 9:
                    same = as_float(buf[i]) == as_float(f[i].want);
                    break;
                default:
                    same = as_long(buf[i]) == as_long(f[i].want);
                }

                if (!same)
                {
                    fail("%s: tick %d %s want %s got %s", dir, t, f[i].name, short_value(f[i].want), buf[i]);
                    stopped = 1;
                }
            }
        }

        /* 4. the pending set: size, head and the digest */
        if (!stopped)
        {
            int64_t want_pending = as_long(value_of(row, "pending"));
            int got_pending = ticks_pending_count();

            if (got_pending != (int)want_pending)
            {
                fail("%s: tick %d pending want %lld got %d", dir, t, (long long)want_pending, got_pending);
                stopped = 1;
            }

            struct tick_entry first[16];
            int nfirst = ticks_peek(16, first);
            int64_t h = 0xcbf29ce484222325LL;

            for (int i = 0; i < nfirst; ++i)
            {
                h = sp_fold(h, first[i].x);
                h = sp_fold(h, first[i].y);
                h = sp_fold(h, first[i].z);
                h = sp_fold(h, first[i].block);
                h = sp_fold(h, first[i].time);
                h = sp_fold(h, first[i].priority);
                h = sp_fold(h, first[i].entry);
            }

            h = sp_fold(h, nfirst);

            int64_t want_pdig = as_long(value_of(row, "pdig"));

            if (h != want_pdig)
            {
                fail("%s: tick %d pdig want %lld got %lld (%d entries: first (%d,%d,%d) id %d t %lld)", dir, t,
                     (long long)want_pdig, (long long)h, nfirst, nfirst ? first[0].x : 0,
                     nfirst ? first[0].y : 0, nfirst ? first[0].z : 0, nfirst ? first[0].block : 0,
                     nfirst ? (long long)first[0].time : 0);
                stopped = 1;
            }

            const char *pf = value_of(row, "pfirst");

            if (!stopped && pf != NULL && nfirst > 0)
            {
                char want[160];
                snprintf(want, sizeof want, "[%d,%d,%d,%d,\"l:%lld\",%d,\"l:%lld\"]", first[0].x, first[0].y,
                         first[0].z, first[0].block, (long long)first[0].time, first[0].priority,
                         (long long)first[0].entry);

                if (strncmp(pf, want, strlen(want)) != 0)
                {
                    fail("%s: tick %d pfirst want %.80s got %s", dir, t, pf, want);
                    stopped = 1;
                }
            }
        }

        if (!stopped) ++exact;
    }

    if (nrows != (int)ticks)
        fail("%s: the manifest says %lld ticks, tickrows.jsonl.gz holds %d rows", dir, (long long)ticks, nrows);

    if (!stopped && spawns_want != INT64_MIN && mobs_done + bodies_done != spawns_want)
        fail("%s: the manifest says %lld spawns, the replay made %lld (%lld mobs + %lld tick-body)", dir,
             (long long)spawns_want, mobs_done + bodies_done, mobs_done, bodies_done);

    if (exact == nrows && nrows > 0 && fails == 0)
        printf("SPAWNING %s: %d ticks exact, %lld mob spawns, %lld tick-body spawns, pass\n", dir, exact,
               mobs_done, bodies_done);
    else
        printf("SPAWNING %s: %d of %d ticks exact, %lld mob spawns, %lld tick-body spawns\n", dir, exact,
               nrows, mobs_done, bodies_done);

    for (int i = 0; i < nrows; ++i) free(rows[i]);
    for (int i = 0; i < nslines; ++i) free(slines[i]);
    free(rows);
    free(slines);
    free(wbytes);
    spawner_free(&sp);
    snapshot_free(&s);

    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);

    /* --negative=cap plants the monster cap's +40 (70 -> 110, over the
     * recording's peak) and --negative=pack moves the pack walk's
     * four-attempt cap to three; each expects the failure that names the tick
     * and the field the mutation moves. */
    for (int i = 1; i < argc && strcmp(argv[i], "--") != 0; ++i)
    {
        if (!strcmp(argv[i], "--negative=cap")) neg_cap = 1;
        else if (!strcmp(argv[i], "--negative=pack")) neg_pack = 1;
    }

    if (argc != 2 && argc != 3)
    {
        fprintf(stderr, "usage: test_spawning RECORDING_DIR [--negative=cap|pack]\n");
        return 2;
    }

    const char *dir = argv[1];

    trace_open(argc >= 3 && argv[2][0] != '-' ? argv[2] : NULL);

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    if (manifest == NULL)
    {
        printf("FAIL %s: no manifest.json\n", dir);
        return 2;
    }

    char kind[64];
    manifest_str(manifest, "kind", kind, sizeof kind);

    if (strcmp(kind, "populate-spawn") == 0)
    {
        int rc = populate_spawn_run(dir, manifest);
        free(manifest);
        return rc;
    }

    if (strcmp(kind, "scene") == 0)
    {
        int rc = scene_run(dir, manifest);
        free(manifest);
        return rc;
    }

    int64_t ticks = nested_int(manifest, "probe", "ticks");

    if (strcmp(kind, "netherite-snapshot") != 0 || ticks == INT64_MIN ||
        nested_int(manifest, "probe", "dig") == INT64_MIN)
    {
        printf("FAIL %s: not a spawner recording\n", dir);
        free(manifest);
        return 2;
    }

    int rc = spawner_run(dir, manifest);
    free(manifest);
    return rc;
}
