/* Gate: the native seed-world build against the oracle's SpawnDump recording
 * (out/java/seedworld/<name>/; made with
 *
 *   make -C oracle script SCRIPT=tests/seedquit.jsonl \
 *     ARGS="--spawndump <abs>/out/java/seedworld/<name>"
 *
 * where the script is a single {"cmd":"quit"} line: the dump happens at the
 * top of tick 0, before the join).
 *
 * From the manifest's seed alone, csrc/engine/seedworld.c builds the overworld
 * the server holds right after MinecraftServer.initialWorldChunkLoad, and this
 * test compares, in order, stopping at the first difference:
 *
 *   1. the spawn point (level.dat's SpawnX/SpawnY/SpawnZ);
 *   2. the loaded chunk set and its load order (ChunkProviderServer.
 *      loadedChunks: the cascade order of the population calls);
 *   3. every chunk's bytes (the Probe layout), biome array, populated flags,
 *      skylight columns, queued light checks and gap flag;
 *   4. every chunk's tile entities, as canonical NBT text, by position;
 *   5. the pending block tick list in the TreeSet's order;
 *   6. the entities world generation spawned (SpawnDump's animals.jsonl): the
 *      class, the entity id, the UUID, the position and rotation, the item
 *      stack, and the entity's own Det Random state;
 *   7. Det's streams: the per-role entity id counters and Math streams, the
 *      SERVER seeder's 48-bit state and every registered split Random's state
 *      and used flags;
 *   8. the world scalars: World.rand, World.updateLCG, NextTickListEntry's
 *      nextTickEntryID and Rows.blkHash, the chain over every changed block
 *      write the build made.
 *
 * The CLIENT role's seeder is the one Det stream not compared: the Java client
 * advances it by constructing its own objects (MusicTicker, FontRenderer, the
 * sound handler and so on) before the world exists, and this build has no
 * client. The other three roles are untouched on both sides.
 *
 * Negative checks: --negative=fuzz swaps the two draw pairs the spawn fuzz
 * loop's x step makes for the z step's - it bites on the seeds whose spawn
 * search needed the fuzz (seed2024: the search position's column has no grass
 * at y 63, so the loop draws) and is a no-op on the five whose search position
 * already is grass. --negative=earlypop populates the first chunk the 625-chunk
 * loop loads at once, before its three companions exist. Both must fail,
 * naming the chunk: the fuzz check names the first chunk its spawn point moves.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/det.h"
#include "../engine/env.h"
#include "../engine/nbtjson.h"
#include "../engine/populate_nether.h"
#include "../engine/seedworld.h"
#include "../engine/snapshot.h"
#include "../engine/tape.h"
#include "../engine/tileentity.h"
#include "../engine/ticks.h"

static const char *dir = "?";
/* NextTickListEntry.nextTickEntryID when a dimension dump's load started: a
 * JVM-global counter the overworld's own load already advanced, while the
 * native build's ids start at zero. The overworld dumps carry no base (0). */
static long long tick_base;
static long long compared_chunks, compared_tiles, compared_ticks, compared_entities;

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL %s: ", dir);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    exit(1);
}

/* --------------------------------------------------------------- the readers */

/* A canonical integer scalar out of a compound; -1 when the key is missing. */
static long long iof(const nbt *comp, const char *key)
{
    const nbt *v = nbt_get(comp, key);

    return v == NULL ? -1 : (long long)nbt_int_value(v);
}

/* A canonical "d:"/JSON double, as its raw bit pattern. */
static uint64_t dbits(const struct jval *v)
{
    uint64_t bits = 0;

    if (v == NULL || !json_double(v, &bits)) fail("a value is not a double");
    return bits;
}

static uint32_t fbits(const struct jval *v)
{
    uint32_t bits = 0;

    if (v == NULL || !json_float(v, &bits)) fail("a value is not a float");
    return bits;
}

static uint64_t dabs(double d)
{
    uint64_t b;
    memcpy(&b, &d, 8);
    return b;
}

static uint32_t fabs4(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

/* json_parse takes ownership of a malloc'd, NUL-terminated line. */
static char *dup_line(const char *s)
{
    size_t n = strlen(s);
    char *p = malloc(n + 1);

    if (p == NULL) fail("out of memory");
    memcpy(p, s, n + 1);
    return p;
}

static long long jint(const struct jval *v)
{
    int64_t out = 0;

    if (v == NULL || !json_int(v, &out)) fail("a value is not an integer");
    return out;
}

/* ------------------------------------------------------------------- the map */

/* The tile entities of one chunk, sorted by (x, y, z) the way the dump sorts
 * them, as canonical NBT text. */
struct ch_tile { int x, y, z; char *text; };

static int ch_tile_cmp(const void *a, const void *b)
{
    const struct ch_tile *p = a, *q = b;

    if (p->x != q->x) return p->x < q->x ? -1 : 1;
    if (p->y != q->y) return p->y < q->y ? -1 : 1;
    if (p->z != q->z) return p->z < q->z ? -1 : 1;
    return 0;
}

/* The first load-order entry that differs from the dump's chunk list, as
 * "index want got"; 0 when the two lists agree. Used by the spawn-point check,
 * which otherwise fails before any chunk is compared. */
static int first_load_diff(const struct snapshot *s, const struct seedworld *sw, char *out, size_t outn)
{
    int n = (int)sw->p.world.lon;

    for (int i = 0; i < n || i < s->nchunks; ++i)
    {
        int64_t key = i < n ? sw->p.world.load_order[i] : 0;
        int gx = (int)(uint32_t)key, gz = (int)(uint32_t)(key >> 32);
        int wx = i < s->nchunks ? s->chunks[i].cx : 0, wz = i < s->nchunks ? s->chunks[i].cz : 0;

        if (i < n && i < s->nchunks && wx == gx && wz == gz) continue;

        snprintf(out, outn, "first differing chunk at load order %d: want (%d,%d) got (%d,%d)", i, wx, wz, gx, gz);
        return 1;
    }

    return 0;
}

/* --------------------------------------------------------------- the compare */

static void compare_chunks(struct snapshot *s, struct seedworld *sw)
{
    uint8_t *got = malloc(SNAP_CHUNK_BYTES), *want = malloc(SNAP_CHUNK_BYTES);
    char what[256];

    if (got == NULL || want == NULL) fail("out of memory");

    for (int i = 0; i < s->nchunks; ++i)
    {
        struct snap_chunk *sc = &s->chunks[i];
        struct chunk *c = world_chunk(&sw->p.world, sc->cx, sc->cz);

        if (c == NULL) fail("chunk (%d,%d) is not loaded natively", sc->cx, sc->cz);

        snapshot_chunk_bytes(c, got);

        if (snapshot_chunk_diff(snapshot_chunk_want(sc, want), got, what, sizeof what))
            fail("chunk (%d,%d): %s", sc->cx, sc->cz, what);

        if (c->biome[0] != 0 || memcmp(c->biome, sc->biome, SNAP_BIOME) != 0)
        {
            for (int k = 0; k < SNAP_BIOME; ++k)
                if (c->biome[k] != sc->biome[k])
                    fail("chunk (%d,%d): biome (%d,%d) want %d got %d", sc->cx, sc->cz, k & 15, k >> 4, sc->biome[k], c->biome[k]);
        }

        if (c->terrain_populated != sc->terrain_populated)
            fail("chunk (%d,%d): isTerrainPopulated want %d got %d", sc->cx, sc->cz, sc->terrain_populated, c->terrain_populated);

        if (c->light_populated != sc->light_populated)
            fail("chunk (%d,%d): isLightPopulated want %d got %d", sc->cx, sc->cz, sc->light_populated, c->light_populated);

        if (c->populated != sc->populated)
            fail("chunk (%d,%d): field_150815_m want %d got %d", sc->cx, sc->cz, sc->populated, c->populated);

        if (c->gap_lighting_updated != sc->gap)
            fail("chunk (%d,%d): isGapLightingUpdated want %d got %d", sc->cx, sc->cz, sc->gap, c->gap_lighting_updated);

        if (c->queued_light_checks != sc->queued_light_checks)
            fail("chunk (%d,%d): queuedLightChecks want %d got %d", sc->cx, sc->cz, sc->queued_light_checks, c->queued_light_checks);

        for (int k = 0; k < SNAP_COLUMNS; ++k)
        {
            if (c->update_skylight_columns[k] != sc->skylight_columns[k])
                fail("chunk (%d,%d): updateSkylightColumns (%d,%d) want %d got %d",
                     sc->cx, sc->cz, k & 15, k >> 4, sc->skylight_columns[k], c->update_skylight_columns[k]);
        }

        /* the tile entities, by position */
        struct ch_tile *mine = malloc(sizeof *mine * (size_t)(c->tes.n > 0 ? c->tes.n : 1));

        if (mine == NULL) fail("out of memory");

        for (int t = 0; t < c->tes.n; ++t)
        {
            struct tile_entity *te = c->tes.v[t];
            mine[t].x = te->x;
            mine[t].y = te->y;
            mine[t].z = te->z;
            mine[t].text = te_render(te);
        }

        qsort(mine, (size_t)c->tes.n, sizeof *mine, ch_tile_cmp);

        if (c->tes.n != sc->ntiles)
            fail("chunk (%d,%d): %d tile entities natively, the dump holds %d", sc->cx, sc->cz, c->tes.n, sc->ntiles);

        for (int t = 0; t < sc->ntiles; ++t)
        {
            const nbt *tag = sc->tiles[t].tag;

            if (mine[t].x != (int)iof(tag, "x") || mine[t].y != (int)iof(tag, "y") || mine[t].z != (int)iof(tag, "z"))
                fail("chunk (%d,%d) tile %d: position want (%lld,%lld,%lld) got (%d,%d,%d)", sc->cx, sc->cz, t,
                     iof(tag, "x"), iof(tag, "y"), iof(tag, "z"), mine[t].x, mine[t].y, mine[t].z);

            if (strcmp(mine[t].text, sc->tiles[t].text) != 0)
                fail("chunk (%d,%d) tile %d at (%d,%d,%d): want %.200s got %.200s", sc->cx, sc->cz, t,
                     mine[t].x, mine[t].y, mine[t].z, sc->tiles[t].text, mine[t].text);

            ++compared_tiles;
            free(mine[t].text);
        }

        free(mine);
        ++compared_chunks;
    }

    free(got);
    free(want);
}

static void compare_ticks(struct snapshot *s)
{
    int np = ticks_pending_count();

    if (np != s->nticks)
        fail("pending ticks: %d natively, the dump holds %d", np, s->nticks);

    if (s->nthis != 0) fail("the dump has %d pendingTickListEntriesThisTick entries, the world starts with none", s->nthis);

    struct tick_entry *e = malloc(sizeof *e * (size_t)(np > 0 ? np : 1));

    if (e == NULL) fail("out of memory");

    if (np > 0 && ticks_peek(np, e) != np) fail("ticks_peek returned fewer than %d entries", np);

    for (int i = 0; i < np; ++i)
    {
        const struct snap_tick *o = &s->ticks[i];

        if (o->x != e[i].x || o->y != e[i].y || o->z != e[i].z || o->id != e[i].block
            || o->scheduled != e[i].time || o->priority != e[i].priority || o->entry != e[i].entry + tick_base)
            fail("pending tick %d: want (%d,%d,%d) block %d at %lld pri %d id %lld, got (%d,%d,%d) block %d at %lld pri %d id %lld",
                 i, o->x, o->y, o->z, o->id, (long long)o->scheduled, o->priority, (long long)o->entry,
                 e[i].x, e[i].y, e[i].z, e[i].block, (long long)e[i].time, e[i].priority, (long long)e[i].entry);
    }

    free(e);
    compared_ticks = np;
}

static void compare_entities(struct snapshot *s, struct seedworld *sw)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/animals.jsonl", dir);
    FILE *f = fopen(path, "rb");

    if (f == NULL) { perror(path); exit(2); }

    struct lines in;
    lines_init(&in);
    lines_file(&in, f);

    int made = 0, n = 0;
    const struct sw_entity *mine = seedworld_entities(sw, &made);

    for (const char *line = lines_next(&in); line; line = lines_next(&in))
    {
        if (!line[0]) continue;

        struct jval *v = json_parse(dup_line(line));

        if (v == NULL) fail("animals.jsonl line %d is not JSON", n + 1);

        if (n >= made) fail("animals.jsonl line %d: the build made only %d entities [%.80s]", n + 1, made, line);

        const struct sw_entity *e = &mine[n];
        const char *cls = json_str(json_get(v, "class"));

        if (cls == NULL || strcmp(cls, seedworld_class_name(e->cls)) != 0)
        {
            char win[512];
            int o = 0;

            for (int k = n > 3 ? n - 3 : 0; k < made && k <= n + 3; ++k)
                o += snprintf(win + o, sizeof win - (size_t)o, "%s%d:%s", o ? " " : "", mine[k].id, seedworld_class_name(mine[k].cls));

            fail("entity %d class: want %s got %s; native from %d: %s", n, cls ? cls : "?", seedworld_class_name(e->cls),
                 n > 3 ? n - 3 : 0, win);
        }

        if (jint(json_get(v, "id")) != e->id)
            fail("entity %d (%s) id: want %lld got %d", n, cls, jint(json_get(v, "id")), e->id);

        if (jint(json_get(v, "uuidMsb")) != e->uuid_msb || jint(json_get(v, "uuidLsb")) != e->uuid_lsb)
            fail("entity %d (%s) uuid: want %lld:%lld got %lld:%lld", n, cls,
                 jint(json_get(v, "uuidMsb")), jint(json_get(v, "uuidLsb")), (long long)e->uuid_msb, (long long)e->uuid_lsb);

        static const char *dnames[6] = { "x", "y", "z", "motionX", "motionY", "motionZ" };
        uint64_t wants[6] = { dbits(json_get(v, "x")), dbits(json_get(v, "y")), dbits(json_get(v, "z")),
                              dbits(json_get(v, "motionX")), dbits(json_get(v, "motionY")), dbits(json_get(v, "motionZ")) };
        uint64_t gots[6] = { dabs(e->x), dabs(e->y), dabs(e->z), dabs(e->motion_x), dabs(e->motion_y), dabs(e->motion_z) };

        for (int i = 0; i < 6; ++i)
        {
            if (wants[i] == gots[i]) continue;

            double w[6], g[6];

            for (int k = 0; k < 6; ++k) { memcpy(&w[k], &wants[k], 8); memcpy(&g[k], &gots[k], 8); }

            fail("entity %d (%s) %s: want %016llx got %016llx; want (%.9f,%.9f,%.9f) motion (%.9f,%.9f,%.9f), got (%.9f,%.9f,%.9f) motion (%.9f,%.9f,%.9f)",
                 n, cls, dnames[i], (unsigned long long)wants[i], (unsigned long long)gots[i],
                 w[0], w[1], w[2], w[3], w[4], w[5], g[0], g[1], g[2], g[3], g[4], g[5]);
        }

        if (fbits(json_get(v, "yaw")) != fabs4(e->yaw))
            fail("entity %d (%s) yaw: want %08x got %08x", n, cls, fbits(json_get(v, "yaw")), fabs4(e->yaw));

        if (fbits(json_get(v, "pitch")) != fabs4(e->pitch))
            fail("entity %d (%s) pitch: want %08x got %08x", n, cls, fbits(json_get(v, "pitch")), fabs4(e->pitch));

        if ((uint64_t)jint(json_get(v, "rand")) != e->rand_state)
            fail("entity %d (%s) Random state: want %lld got %llu", n, cls,
                 jint(json_get(v, "rand")), (unsigned long long)e->rand_state);

        /* the item an EntityItem carries, out of the full NBT the dump also
         * wrote */
        const struct jval *nbtj = json_get(v, "nbt");

        if (nbtj != NULL && e->cls == SW_ITEM)
        {
            char *raw = json_raw(nbtj);
            nbt *tag = nbt_parse(raw);
            const nbt *item = tag ? nbt_get(tag, "Item") : NULL;

            if (item == NULL) fail("entity %d (EntityItem) has no Item tag", n);

            if ((int)iof(item, "id") != e->item || (int)iof(item, "Damage") != e->damage || (int)iof(item, "Count") != e->count)
                fail("entity %d (EntityItem) stack: want %lld:%lld x%lld got %d:%d x%d", n,
                     iof(item, "id"), iof(item, "Damage"), iof(item, "Count"), e->item, e->damage, e->count);

            nbt_free(tag);
            free(raw);
        }

        json_free(v);
        ++n;
        ++compared_entities;
    }

    fclose(f);
    lines_free(&in);

    if (n != made) fail("the dump holds %d entities, the build made %d", n, made);
}

static void compare_det(struct snapshot *s, struct seedworld *sw)
{
    const nbt *ids = nbt_get(s->det, "nextId");
    const nbt *seeder = nbt_get(s->det, "seeder");
    const nbt *math = nbt_get(s->det, "math");

    if (ids == NULL || seeder == NULL || math == NULL) fail("det.nbt has no nextId/seeder/math");

    for (int r = 0; r < DET_ROLES; ++r)
    {
        long long w = nbt_int_value(nbt_list_get(ids, r));

        if (w != sw->det.next_id[r])
        {
            static const char *roles[4] = { "CLIENT", "SERVER", "OTHER", "RENDER" };
            fail("Det.nextId[%s]: want %lld got %d", roles[r], w, sw->det.next_id[r]);
        }
    }

    for (int r = 0; r < DET_ROLES; ++r)
    {
        uint64_t w = (uint64_t)nbt_int_value(nbt_list_get(math, r));

        if (w != det_math_state(&sw->det, r))
            fail("Det.math[%d]: want %llu got %llu", r, (unsigned long long)w, (unsigned long long)det_math_state(&sw->det, r));
    }

    /* every role but CLIENT: the client thread's seeder belongs to the Java
     * client's own object construction, which this build has no part in */
    for (int r = 0; r < DET_ROLES; ++r)
    {
        uint64_t w = (uint64_t)nbt_int_value(nbt_list_get(seeder, r));

        if (r == DET_CLIENT)
        {
            if (w == det_seeder_state(&sw->det, r)) printf("note: Det.seeder[CLIENT] matches too\n");
            else printf("note: Det.seeder[CLIENT] is the Java client's own draws (not compared)\n");

            continue;
        }

        if (w != det_seeder_state(&sw->det, r))
            fail("Det.seeder[%d]: want %llu got %llu", r, (unsigned long long)w, (unsigned long long)det_seeder_state(&sw->det, r));
    }

    /* the split Randoms: every registered one, by name, state and used flag */
    const nbt *splits = nbt_get(s->det, "splits");
    int nsplits = nbt_list_size(splits);

    for (int i = 0; i < nsplits; ++i)
    {
        const nbt *e = nbt_list_get(splits, i);
        const char *name = nbt_string_value(nbt_get(e, "name"));

        if (name == NULL) fail("det.nbt split %d has no name", i);

        const det_split *sp = det_split_find(&sw->det, name);

        if (sp == NULL) fail("split %s is not registered natively", name);

        const nbt *state = nbt_get(e, "state");
        const nbt *used = nbt_get(e, "used");

        for (int r = 0; r < DET_ROLES; ++r)
        {
            uint64_t w = (uint64_t)nbt_int_value(nbt_list_get(state, r));
            uint64_t g = det_rng_state(&sp->d[r]);

            if (w != g)
                fail("split %s role %d state: want %llu got %llu", name, r, (unsigned long long)w, (unsigned long long)g);

            long long wu = nbt_int_value(nbt_list_get(used, r));

            if ((int)wu != sp->used[r])
                fail("split %s role %d used: want %lld got %d", name, r, wu, sp->used[r]);
        }
    }
}

static void compare_world(struct snapshot *s, struct seedworld *sw)
{
    uint64_t wrand = (uint64_t)iof(s->worldstate, "rand");
    long long wlcg = iof(s->worldstate, "updateLCG");
    uint64_t wblk = (uint64_t)iof(s->worldstate, "blkHash");
    long long weid = iof(s->worldstate, "nextTickEntryID");

    if (wlcg != sw->update_lcg)
        fail("World.updateLCG: want %lld got %d", wlcg, sw->update_lcg);

    if (wrand != sw->world_rand.seed)
        fail("World.rand: want %llu got %llu", (unsigned long long)wrand, (unsigned long long)sw->world_rand.seed);

    if (weid != ticks_next_entry_id() + tick_base)
        fail("nextTickEntryID: want %lld got %lld", weid, (long long)ticks_next_entry_id() + tick_base);

    if (wblk != sw->blk_hash)
        fail("Rows.blkHash over %lld writes: want %016llx got %016llx", sw->blk_count,
             (unsigned long long)wblk, (unsigned long long)sw->blk_hash);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--negative=fuzz")) nw_env->cfg.seedworld_negative_fuzz_order = 1;
        else if (!strcmp(argv[i], "--negative=earlypop")) nw_env->cfg.seedworld_negative_early_pop = 1;
        else if (!strcmp(argv[i], "--negative=dimblock")) nw_env->cfg.seedworld_negative_dim_block = 1;
        else if (argv[i][0] == '-') { fprintf(stderr, "usage: test_seedworld [--negative=fuzz|earlypop|dimblock] DIR\n"); return 2; }
        else dir = argv[i];
    }

    if (strcmp(dir, "?") == 0) { fprintf(stderr, "usage: test_seedworld DIR\n"); return 2; }

    struct snapshot s;

    if (!snapshot_load(&s, dir))
    {
        printf("FAIL %s: the snapshot did not load\n", dir);
        return 1;
    }

    const struct jval *counts = json_get(s.manifest_json, "counts");

    if (counts == NULL) fail("the manifest has no counts");

    int64_t mchunks = jint(json_get(counts, "chunks"));
    int64_t mtiles = jint(json_get(counts, "tiles"));
    int64_t mantimals = jint(json_get(counts, "animals"));
    int64_t mticks = jint(json_get(counts, "pendingTicks"));

    struct seedworld sw;

    /* A dimension dump carries the base of the global tick-entry counter */
    const struct jval *basej = json_get(s.manifest_json, "tickEntryBase");
    tick_base = basej == NULL ? 0 : jint(basej);

    /* The manifest's dim picks the build: absent or 0 is the overworld
     * (SpawnDump), -1 the Nether and 1 the End (SpawnDumpDim). */
    const struct jval *dimj = json_get(s.manifest_json, "dim");
    int dim = dimj == NULL ? 0 : (int)jint(dimj);

    if (dim != 0 && dim != -1 && dim != 1) fail("the manifest's dim %d is neither the overworld, the Nether nor the End", dim);

    if (dim == 0)
    {
        seedworld_init(&sw, s.seed);
        seedworld_build(&sw);
    }
    else
    {
        /* The run's state when the dimension's spawn area loads: the overworld's
         * initialWorldChunkLoad is already done, and Det - one JVM-global object -
         * carries its draws (the seeds' entities, the Math stream, the entity-id
         * counters). Build the overworld area, hand those streams to the
         * dimension's build, then load the dimension. */
        struct seedworld ow;
        seedworld_init(&ow, s.seed);
        seedworld_build(&ow);
        seedworld_init_dim(&sw, s.seed, dim);
        seedworld_move_det(&sw, &ow);
        seedworld_free(&ow);
        seedworld_build_dim(&sw);
    }

    /* 1. the spawn point: the overworld's out of level.dat, a dimension's out
     * of the manifest (its own createSpawnPosition result; the worldInfo the
     * dimension ends up holding is a DerivedWorldInfo over the overworld's) */
    long long sx = 0, sy = 0, sz = 0;

    if (dim == 0)
    {
        sx = iof(s.worldinfo, "SpawnX");
        sy = iof(s.worldinfo, "SpawnY");
        sz = iof(s.worldinfo, "SpawnZ");
    }
    else
    {
        const char *sp = json_str(json_get(s.manifest_json, "spawn"));

        if (sp == NULL || sscanf(sp, "i:%lld,i:%lld,i:%lld", &sx, &sy, &sz) != 3)
            fail("the manifest has no spawn of the form i:x,i:y,i:z");
    }

    if (sx != sw.spawn_x || sy != sw.spawn_y || sz != sw.spawn_z)
    {
        char what[256] = "the chunk list is not compared";

        if (first_load_diff(&s, &sw, what, sizeof what)) { /* the message names the chunk */ }

        fail("spawn point: want (%lld,%lld,%lld) got (%lld,%d,%lld); %s", sx, sy, sz,
             (long long)sw.spawn_x, sw.spawn_y, (long long)sw.spawn_z, what);
    }

    /* 2. the chunk set and the load order */
    if ((int64_t)sw.p.world.lon != mchunks)
        fail("loaded chunks: %lld natively, the manifest says %lld", (long long)sw.p.world.lon, mchunks);

    if ((int64_t)s.nchunks != mchunks)
        fail("the dump holds %d chunks, the manifest says %lld", s.nchunks, mchunks);

    char what[256];

    if (first_load_diff(&s, &sw, what, sizeof what)) fail("%s", what);

    /* 3-4. every chunk */
    compare_chunks(&s, &sw);

    if (compared_tiles != mtiles)
        fail("tile entities: %lld compared, the manifest says %lld", compared_tiles, mtiles);

    /* 5. the pending ticks */
    compare_ticks(&s);

    if (compared_ticks != mticks)
        fail("pending ticks: %lld compared, the manifest says %lld", compared_ticks, mticks);

    /* 6. the entities */
    compare_entities(&s, &sw);

    if (compared_entities != mantimals)
        fail("entities: %lld compared, the manifest says %lld", compared_entities, mantimals);

    /* 7. Det */
    compare_det(&s, &sw);

    /* 8. the world scalars */
    compare_world(&s, &sw);

    printf("PASS %s: seed %lld, %s, spawn (%lld,%lld,%lld), %lld chunks, %lld tiles, %lld entities, %lld pending ticks, blkHash %016llx\n",
           dir, (long long)s.seed, dim == 0 ? "overworld" : (dim == -1 ? "nether" : "end"),
           sx, sy, sz, compared_chunks, compared_tiles, compared_entities, compared_ticks,
           (unsigned long long)sw.blk_hash);

    if (dim == -1) populate_hell_free();
    seedworld_free(&sw);
    snapshot_free(&s);
    return 0;
}