/* Gate: the server tick against the oracle's ServerTickProbe recording
 * (oracle/harness/netherite/oracle/ServerTickProbe.java, recorded with
 * `make -C oracle run SEED=N CLASS=ServerTickProbe NAME=x CMD='{"ticks":2400,...}'`).
 *
 *   test_servertick RECORDING_DIR
 *
 * DIR is a snapshot directory (Snapshot.java's files) with the probe's own
 * keys in the manifest and the probe's own files next to them. The checks:
 *
 *   1. the manifest is a probe recording, the snapshot loads (chunks, entities,
 *      pending block ticks, worldinfo, worldstate) and the pending list loads
 *      into ticks.c's set in the recording's own tree order (the first sixteen
 *      entries and the count against ticks.jsonl.gz);
 *   2. the state the tick body reads but the snapshot files do not carry comes
 *      from the manifest: World.ambientTickCountdown, the difficulty, the parked
 *      player and the active chunk iteration order;
 *   3. Det's streams load from det.nbt;
 *   4. tick by tick, WorldServer.tick runs and every recorded field is
 *      compared: the write rows byte for byte against tickwrites.bin, the
 *      spawned entities against spawns.jsonl.gz, the scalars (World.rand,
 *      updateLCG, the two clocks, the weather flags, timers and strengths,
 *      skylightSubtracted, ambientTickCountdown), the pending set's size, its
 *      first entry and its 16-entry digest, and Det's SERVER digests. The first
 *      difference names the tick, the field and both values, and the run stops
 *      there; the exact-tick count is reported.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/det.h"
#include "../engine/servertick.h"
#include "../engine/env.h"
#include "../engine/snapshot.h"
#include "../engine/tape.h"
#include "../engine/ticks.h"
#include "probe.h"

static int fails;
static char path[1200];

/* A number out of a Gson line or manifest, bare (123, -4) or as one of the
 * probe's typed canonical strings ("l:5", "f:3f800000", "i:7"). Returns NULL
 * when the key is absent. */
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

/* A value as it stands in the line, up to its closing quote, for a message. */
static const char *short_value(const char *v)
{
    static char ring[8][24];
    static int slot;
    char *out = ring[slot++ % 8];

    if (v == NULL) return "(absent)";

    int n = 0;

    while (v[n] != 0 && v[n] != '"' && n < 23) ++n;

    memcpy(out, v, (size_t)n);
    out[n] = 0;
    return out;
}

static int64_t as_long(const char *v)
{
    if (v == NULL) return INT64_MIN;

    /* the canonical scalars carry a one-letter type prefix: l:, i:, s:, b: */
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

/* A "d:" plus 16 hex digits of the raw bits. */
static double as_double(const char *v)
{
    if (v == NULL || v[0] != 'd' || v[1] != ':') return 0.0;

    uint64_t bits = strtoull(v + 2, NULL, 16);
    double d;

    memcpy(&d, &bits, sizeof d);
    return d;
}



static uint32_t fbits(float f)
{
    uint32_t u;

    memcpy(&u, &f, sizeof u);
    return u;
}

/* The tick body's stream comparison: any difference stops the run with the
 * tick and both values, and fails the gate. The write rows, the spawns, the
 * scalars (World.rand, updateLCG, the clocks, the weather, the pending set's
 * size and digest) and Det's digests all go through here. */
static void diverge(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("FAIL ", stdout);
    vprintf(fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
    ++fails;
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

/* A nested integer: "obj":{"key":123}. */
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

static int64_t fold(int64_t h, int64_t v)
{
    return (h ^ v) * 0x100000001b3LL;
}

/* The recording's tickrows.jsonl.gz, held in memory (2400 short lines). */
static char **rows;
static int nrows;

/* The probe's tickwrites.bin. */
static unsigned char *wbytes;

/* spawns.jsonl.gz, one line per spawn in tick order, hoisted into rows too. */
static char **slines;
static int nslines;

/* ---------------------------------------------------------------- helpers */

/* A heap copy of a line out of a lines_ buffer. */
static char *dup_line(const char *s)
{
    size_t n = strlen(s) + 1;
    char *out = malloc(n);
    memcpy(out, s, n);
    return out;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);

    if (argc != 2)
    {
        fprintf(stderr, "usage: test_servertick RECORDING_DIR\n");
        return 2;
    }

    const char *dir = argv[1];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    if (manifest == NULL)
    {
        printf("SKIP %s: no manifest.json\n", dir);
        return 0;
    }

    char kind[64];
    manifest_str(manifest, "kind", kind, sizeof kind);

    int64_t ticks = nested_int(manifest, "servertick", "ticks");

    if (strcmp(kind, "netherite-snapshot") != 0 || ticks == INT64_MIN)
    {
        printf("SKIP %s: kind %s is not a server tick recording\n", dir, kind);
        free(manifest);
        return 0;
    }

    int64_t rain = nested_int(manifest, "servertick", "rain");
    int64_t thunder = nested_int(manifest, "servertick", "thunder");
    int64_t view = nested_int(manifest, "servertick", "viewDistance");
    int64_t ambient = nested_int(manifest, "servertick", "ambientTickCountdown");
    int64_t difficulty = nested_int(manifest, "servertick", "difficulty");
    int64_t chunks_expected = manifest_int(manifest, "chunks");
    int64_t pending_expected = manifest_int(manifest, "pendingTicks");
    int64_t writes_expected = nested_int(manifest, "servertick", "writes");
    int64_t spawns_expected = nested_int(manifest, "servertick", "spawns");
    double player[3] = {0.0, 0.0, 0.0};

    {
        static const char *keys[3] = {"playerX", "playerY", "playerZ"};

        for (int i = 0; i < 3; ++i)
        {
            const char *v = value_of(manifest, keys[i]);

            if (v != NULL) player[i] = as_double(v);
        }
    }

    struct snapshot snap;

    if (!snapshot_load(&snap, dir))
    {
        printf("FAIL %s: snapshot_load\n", dir);
        free(manifest);
        return 1;
    }

    if (chunks_expected >= 0 && snap.nchunks != (int)chunks_expected)
        fail("%s: manifest says %d chunks, the file holds %d", dir, (int)chunks_expected, snap.nchunks);

    if (pending_expected >= 0 && snap.nticks != (int)pending_expected)
        fail("%s: manifest says %d pending ticks, ticks.jsonl.gz holds %d", dir, (int)pending_expected, snap.nticks);

    struct servertick st;

    if (!servertick_load(&st, &snap, dir))
    {
        printf("FAIL %s: servertick_load\n", dir);
        snapshot_free(&snap);
        free(manifest);
        return 1;
    }

    /* the probe turns chunk loading off (ChunkProviderServer's
     * loadChunkOnProvideRequest = false, so a chunk that is not loaded reads
     * as the shared EmptyChunk); the oracle records it in the manifest */
    if (nested_int(manifest, "servertick", "noChunkGeneration") == 1) st.w->no_generate = 1;

    /* ----------------------------------- the state the manifest carries */

    if (ambient == INT64_MIN)
        fail("%s: the manifest carries no ambientTickCountdown", dir);
    else
        servertick_set_ambient(&st, (int)ambient, (int)difficulty);

    servertick_set_player(&st, player[0], player[1], player[2]);

    {
        /* World.activeChunkSet, [cx,cz] per entry, in the recorded order */
        const char *p = strstr(manifest, "\"activeChunkOrder\":[");
        int cap = 4096, n = 0;
        int *pairs = malloc((size_t)cap * 2 * sizeof *pairs);

        if (p == NULL)
        {
            fail("%s: the manifest carries no activeChunkOrder", dir);
        }
        else
        {
            p += strlen("\"activeChunkOrder\":[");

            while (*p == '[')
            {
                int cx, cz;

                if (sscanf(p, "[%d,%d]", &cx, &cz) != 2) break;

                if (n == cap)
                {
                    cap *= 2;
                    pairs = realloc(pairs, (size_t)cap * 2 * sizeof *pairs);
                }

                pairs[n * 2] = cx;
                pairs[n * 2 + 1] = cz;
                ++n;

                const char *q = strchr(p, ']');

                if (q == NULL) break;

                p = q + 1;
                if (*p == ',') ++p;
            }

            servertick_set_active(&st, pairs, n);
        }

        free(pairs);

        if (view == 8 && n != 289)
            fail("%s: the active chunk set holds %d entries, a view distance of 8 gives 289", dir, n);
    }

    /* Det's streams */
    det_state det;
    det_init(&det);

    if (snap.det == NULL)
        fail("%s: the snapshot holds no det.nbt", dir);
    else
        servertick_load_det(&st, &det, snap.det);

    /* ------------------------------------------- the pending set, in order */

    ticks_set_immediate(0);
    ticks_reset(st.next_tick_entry);

    for (int i = 0; i < snap.nticks; ++i)
    {
        struct tick_entry e;

        e.x = snap.ticks[i].x;
        e.y = snap.ticks[i].y;
        e.z = snap.ticks[i].z;
        e.block = snap.ticks[i].id;
        e.time = snap.ticks[i].scheduled;
        e.priority = snap.ticks[i].priority;
        e.entry = snap.ticks[i].entry;
        ticks_load_entry(&e);
    }

    printf("     loaded start: Time(total)=%lld DayTime(world)=%lld rainTime=%lld thunderTime=%lld raining=%d thundering=%d"
           " ambient=%lld difficulty=%lld player=(%.3f, %.3f, %.3f)\n",
           (long long)st.total_time, (long long)st.world_time, (long long)st.rain_time, (long long)st.thunder_time,
           st.raining, st.thundering, (long long)ambient, (long long)difficulty, player[0], player[1], player[2]);
    int loaded = ticks_pending_count();

    if (loaded != snap.nticks)
        fail("%s: %d pending entries loaded, the snapshot holds %d", dir, loaded, snap.nticks);

    /* ----------------------------------------------- the probe's own files */

    snprintf(path, sizeof path, "%s/tickrows.jsonl.gz", dir);
    gzFile rg = gzopen(path, "rb");

    if (rg == NULL)
    {
        printf("FAIL %s: cannot read tickrows.jsonl.gz\n", dir);
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

            rows[nrows++] = dup_line(line);
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

            slines[nslines++] = dup_line(line);
        }
    }

    if (sg != NULL) gzclose(sg);

    snprintf(path, sizeof path, "%s/tickwrites.bin", dir);
    size_t wlen = 0;
    wbytes = (unsigned char *)probe_read_file(path, &wlen);

    if (wbytes == NULL)
    {
        printf("FAIL %s: cannot read tickwrites.bin\n", dir);
        return 1;
    }

    /* ---------------------------------------------------------- the ticks */

    int si = 0, exact = 0, stopped = 0;
    size_t woff = 0;

    for (int r = 0; r < nrows && !stopped; ++r)
    {
        const char *row = rows[r];
        int t = (int)as_long(value_of(row, "t"));
        int64_t want_writes = as_long(value_of(row, "writes"));

        servertick_clear(&st);
        servertick_tick(&st, t);

        int nw = 0;
        const struct st_write *w = servertick_writes(&st, &nw);

        /* 1. the write rows, byte for byte: element-wise over the common
         * prefix first, so a count difference still names where the streams
         * part */
        if (woff + (size_t)want_writes * 16 > wlen)
        {
            diverge("%s: tick %d: tickwrites.bin is short", dir, t);
            stopped = 1;
        }
        else
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
                    diverge("%s: tick %d write %d of %lld want (%d,%d,%d) id %d meta %d got (%d,%d,%d) id %d meta %d",
                         dir, t, i, (long long)want_writes, x, y, z, id, meta, w[i].x, w[i].y, w[i].z, w[i].id,
                         w[i].meta);
                    stopped = 1;
                }
            }

            if (!stopped && nw != want_writes)
            {
                for (int i = (int)want_writes; i < nw; ++i)
                    printf("     extra write %d: (%d,%d,%d) id %d meta %d\n", i, w[i].x, w[i].y, w[i].z, w[i].id,
                           w[i].meta);

                diverge("%s: tick %d writes want %lld got %d", dir, t, (long long)want_writes, nw);
                stopped = 1;
            }

            if (!stopped) woff += (size_t)nw * 16;

        }

        /* 2. the spawned entities, in order */
        int ns = 0;
        const struct st_spawn *sp = servertick_spawns(&st, &ns);
        int want_spawn = (int)as_long(value_of(row, "spawns"));

        if (ns != want_spawn)
        {
            diverge("%s: tick %d spawns want %d got %d", dir, t, want_spawn, ns);
            stopped = 1;
        }

        for (int i = 0; i < ns && !stopped; ++i, ++si)
        {
            if (si >= nslines)
            {
                diverge("%s: tick %d spawn %d: spawns.jsonl.gz is short", dir, t, i);
                stopped = 1;
                break;
            }

            const char *line = slines[si];
            char got[64];
            const char *v;

            v = value_of(line, "t");
            if (as_long(v) != t || as_long(value_of(line, "id")) != sp[i].id)
            {
                diverge("%s: tick %d spawn %d: id want %s got %d", dir, t, i, short_value(value_of(line, "id")), sp[i].id);
                stopped = 1;
                break;
            }

            v = value_of(line, "class");
            if (v == NULL || strncmp(v, sp[i].cls, strlen(sp[i].cls)) != 0)
            {
                diverge("%s: tick %d spawn %d: class want %s got %s", dir, t, i, short_value(v), sp[i].cls);
                stopped = 1;
                break;
            }

            if (as_double(value_of(line, "x")) != sp[i].x || as_double(value_of(line, "y")) != sp[i].y ||
                as_double(value_of(line, "z")) != sp[i].z)
            {
                diverge("%s: tick %d spawn %d pos want (%s,%s,%s) got (%.17g,%.17g,%.17g)", dir, t, i,
                     short_value(value_of(line, "x")), short_value(value_of(line, "y")), short_value(value_of(line, "z")),
                     sp[i].x, sp[i].y, sp[i].z);
                stopped = 1;
                break;
            }

            if (as_double(value_of(line, "mx")) != sp[i].mx || as_double(value_of(line, "my")) != sp[i].my ||
                as_double(value_of(line, "mz")) != sp[i].mz || as_float(value_of(line, "yaw")) != sp[i].yaw)
            {
                diverge("%s: tick %d spawn %d motion/yaw want (%s,%s,%s,%s) got (%.17g,%.17g,%.17g,%08x)", dir, t, i,
                     short_value(value_of(line, "mx")), short_value(value_of(line, "my")),
                     short_value(value_of(line, "mz")), short_value(value_of(line, "yaw")), sp[i].mx, sp[i].my, sp[i].mz,
                     fbits(sp[i].yaw));
                stopped = 1;
                break;
            }

            if (strncmp(sp[i].cls, "EntityItem", 10) == 0)
            {
                snprintf(got, sizeof got, "l:%lld", (long long)sp[i].uuid_msb);
                if (as_long(value_of(line, "item")) != sp[i].item || as_long(value_of(line, "dmg")) != sp[i].damage ||
                    as_long(value_of(line, "cnt")) != sp[i].count || as_long(value_of(line, "age")) != sp[i].age ||
                    as_long(value_of(line, "dly")) != sp[i].delay || as_float(value_of(line, "hover")) != sp[i].hover)
                {
                    diverge("%s: tick %d spawn %d item want item %s dmg %s cnt %s dly %s got (item %d dmg %d cnt %d dly %d)",
                         dir, t, i, short_value(value_of(line, "item")), short_value(value_of(line, "dmg")),
                         short_value(value_of(line, "cnt")), short_value(value_of(line, "dly")), sp[i].item, sp[i].damage,
                         sp[i].count, sp[i].delay);
                    stopped = 1;
                    break;
                }

                (void)got;
            }
            else if (strncmp(sp[i].cls, "EntityFallingBlock", 10) == 0)
            {
                if (as_long(value_of(line, "TileID")) != sp[i].tile_id || as_long(value_of(line, "Data")) != sp[i].data ||
                    as_long(value_of(line, "Time")) != sp[i].time || as_long(value_of(line, "Air")) != sp[i].air ||
                    as_long(value_of(line, "DropItem")) != sp[i].drop_item ||
                    as_long(value_of(line, "HurtEntities")) != sp[i].hurt_entities ||
                    as_long(value_of(line, "FallHurtMax")) != sp[i].fall_hurt_max ||
                    as_float(value_of(line, "FallHurtAmount")) != sp[i].fall_hurt_amount ||
                    as_long(value_of(line, "UUIDMost")) != sp[i].uuid_msb ||
                    as_long(value_of(line, "UUIDLeast")) != sp[i].uuid_lsb)
                {
                    diverge("%s: tick %d spawn %d falling want TileID %s Data %s Time %s Air %s Drop %s Hurt %s"
                         " Max %s Amount %s UUIDMost %s got (TileID %d Data %d Time %d Air %d drop %d hurt %d max %d"
                         " amount %08x %lld %lld)",
                         dir, t, i, short_value(value_of(line, "TileID")), short_value(value_of(line, "Data")),
                         short_value(value_of(line, "Time")), short_value(value_of(line, "Air")),
                         short_value(value_of(line, "DropItem")), short_value(value_of(line, "HurtEntities")),
                         short_value(value_of(line, "FallHurtMax")), short_value(value_of(line, "FallHurtAmount")),
                         short_value(value_of(line, "UUIDMost")), sp[i].tile_id, sp[i].data, sp[i].time, sp[i].air,
                         sp[i].drop_item, sp[i].hurt_entities, sp[i].fall_hurt_max, fbits(sp[i].fall_hurt_amount),
                         (long long)sp[i].uuid_msb, (long long)sp[i].uuid_lsb);
                    stopped = 1;
                    break;
                }
            }
            else if (strncmp(sp[i].cls, "EntityLightningBolt", 10) == 0)
            {
                if (as_long(value_of(line, "boltVertex")) != sp[i].bolt_vertex ||
                    as_long(value_of(line, "boltLivingTime")) != sp[i].bolt_living_time)
                {
                    diverge("%s: tick %d spawn %d bolt want vertex %s living %s got (%lld, %d)", dir, t, i,
                         short_value(value_of(line, "boltVertex")), short_value(value_of(line, "boltLivingTime")),
                         (long long)sp[i].bolt_vertex, sp[i].bolt_living_time);
                    stopped = 1;
                    break;
                }
            }
        }


        /* 3. the scalars */
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
            char buf[18][40];

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
            snprintf(buf[14], sizeof buf[14], "l:%llu", (unsigned long long)det_seeder_state(&det, DET_SERVER));
            snprintf(buf[15], sizeof buf[15], "l:%llu", (unsigned long long)det_math_state(&det, DET_SERVER));
            snprintf(buf[16], sizeof buf[16], "l:%llu", (unsigned long long)det_split_state(&det, DET_SERVER));
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
                    diverge("%s: tick %d %s want %s got %s", dir, t, f[i].name, short_value(f[i].want), buf[i]);
                    stopped = 1;
                }
            }
        }

        /* 4. the pending set: size, head and the 16-entry digest */
        {
            int64_t want_pending = as_long(value_of(row, "pending"));
            int got_pending = ticks_pending_count();

            if (got_pending != want_pending)
            {
                diverge("%s: tick %d pending want %lld got %d", dir, t, (long long)want_pending, got_pending);
                stopped = 1;
            }

            struct tick_entry first[16];
            int nfirst = ticks_peek(16, first);
            int64_t h = 0xcbf29ce484222325LL;

            for (int i = 0; i < nfirst; ++i)
            {
                h = fold(h, first[i].x);
                h = fold(h, first[i].y);
                h = fold(h, first[i].z);
                h = fold(h, first[i].block);
                h = fold(h, first[i].time);
                h = fold(h, first[i].priority);
                h = fold(h, first[i].entry);
            }

            h = fold(h, nfirst);

            int64_t want_pdig = as_long(value_of(row, "pdig"));

            if (h != want_pdig)
            {
                diverge("%s: tick %d pdig want %lld got %lld (%d entries: first (%d,%d,%d) id %d t %lld)", dir, t,
                     (long long)want_pdig, (long long)h, nfirst, nfirst ? first[0].x : 0, nfirst ? first[0].y : 0,
                     nfirst ? first[0].z : 0, nfirst ? first[0].block : 0, nfirst ? (long long)first[0].time : 0);
                stopped = 1;
            }

            const char *pf = value_of(row, "pfirst");

            if (!stopped && pf != NULL && nfirst > 0)
            {
                char want[128];
                snprintf(want, sizeof want, "[%d,%d,%d,%d,\"l:%lld\",%d,\"l:%lld\"]", first[0].x, first[0].y, first[0].z,
                         first[0].block, (long long)first[0].time, first[0].priority, (long long)first[0].entry);

                if (strncmp(pf, want, strlen(want)) != 0)
                {
                    diverge("%s: tick %d pfirst want %.60s got %s", dir, t, pf, want);
                    stopped = 1;
                }
            }
        }

        if (!stopped) ++exact;
    }

    if (nrows != (int)ticks)
        fail("%s: the manifest says %lld ticks, tickrows.jsonl.gz holds %d rows", dir, (long long)ticks, nrows);

    (void)writes_expected;
    (void)spawns_expected;

    if (exact == nrows && nrows > 0)
        printf("%s: %d ticks, exact tick after tick (the head, the write rows, the spawns, World.rand, updateLCG,"
               " the pending set and Det's digests)\n", dir, exact);
    else
        printf("%s: %d of %d ticks exact, the first difference at tick %d\n", dir, exact, nrows, exact + 1);

    printf("SERVERTICK %s: seed %lld, %d ticks, rain %lld thunder %lld viewDistance %lld, %s\n", dir,
           (long long)snap.seed, (int)ticks, (long long)rain, (long long)thunder, (long long)view,
           fails ? "FAILED" : "pass");

    snapshot_free(&snap);
    free(manifest);
    return fails ? 1 : 0;
}