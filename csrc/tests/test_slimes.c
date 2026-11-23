/* Gate: the native EntitySlime, EntityMagmaCube and the probe's player against
 * the oracle's SlimeProbe dump (oracle/harness/netherite/oracle/SlimeProbe.java).
 * The probe flattens a raw region (the overworld for slimes, the Nether for
 * magma cubes), spawns the mobs the way SpawnerAnimals does, adds a probe
 * player, ticks them the way World.updateEntities does and records every
 * entity's canonical NBT hash, the state NBT does not carry, the player's own
 * record, the removals, the probe's damage events, the Det state and the final
 * blocks.
 *
 * The replay repeats that from the dump alone: the region, the placements, the
 * spawns (the record's fields and the constructor's Det draws), then one an_tick
 * per tick with every field of every entity compared after each tick.
 *
 * A failure names the tick, the entity and the field. */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/living.h"
#include "../engine/nbtjson.h"
#include "../engine/slimes.h"
#include "../engine/trace.h"
#include "../engine/world.h"

#define REC_BYTES 112
#define PLAYER_BYTES 144
#define SPAWN_BYTES 100

static int failures = 0;

/* ------------------------------------------------------------- byte reads */

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const unsigned char *p)
{
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
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
    uint32_t b = le32(p);
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
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

/* The DetProbe snapshot format. */
struct splitref {
    char name[DET_NAME_MAX];
    uint64_t state[DET_ROLES];
    uint8_t used[DET_ROLES];
};

struct ref {
    int64_t reset_seed, world_seed;
    int32_t next_id[DET_ROLES];
    uint64_t seeder[DET_ROLES], math[DET_ROLES], split[DET_ROLES];
    uint64_t world_rand;
    int world_rand_gauss;
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
        unsigned long long a, b, c, d;

        if (sscanf(line, "resetSeed %lld", &v) == 1) { r->reset_seed = v; continue; }
        if (sscanf(line, "worldSeed %lld", &v) == 1) { r->world_seed = v; continue; }
        if (sscanf(line, "nextId %d %d %d %d", &r->next_id[0], &r->next_id[1], &r->next_id[2], &r->next_id[3]) == 4) continue;
        if (sscanf(line, "worldRand %llx %d", &d, &r->world_rand_gauss) == 2) { r->world_rand = d; continue; }

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

static void fail_rec(int tick, int si, const char *field, const char *want, const char *got)
{
    printf("FAIL tick %d entity %d: %s: the oracle recorded %s, the native replay has %s\n",
           tick, si, field, want, got);
    ++failures;
}

static void cmp_ll(int tick, int si, const char *field, long long want, long long got)
{
    if (want == got) return;

    char w[32], g[32];
    snprintf(w, sizeof w, "%lld", want);
    snprintf(g, sizeof g, "%lld", got);
    fail_rec(tick, si, field, w, g);
}

static void cmp_dbl(int tick, int si, const char *field, double want, double got)
{
    if (want == want && want == got) return;

    char w[64], g[64];
    snprintf(w, sizeof w, "%a", want);
    snprintf(g, sizeof g, "%a", got);
    fail_rec(tick, si, field, w, g);
}

static void cmp_flt(int tick, int si, const char *field, float want, float got)
{
    if (want == got) return;

    char w[80], g[80];
    uint32_t wb, gb;
    memcpy(&wb, &want, 4);
    memcpy(&gb, &got, 4);
    snprintf(w, sizeof w, "%a (raw %08x)", (double)want, wb);
    snprintf(g, sizeof g, "%a (raw %08x)", (double)got, gb);
    fail_rec(tick, si, field, w, g);
}

/* ---------------------------------------------------------------- events */

struct event {
    int tick;
    int spawn_index;
};

static int n_events;
static struct event events[4096];

static void read_events(const char *dir)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/events.txt.gz", dir);
    gzFile f = gzopen(path, "rb");

    if (!f)
    {
        perror(path);
        exit(2);
    }

    char line[4096];

    while (gzgets(f, line, sizeof line))
    {
        int tick, si, id, a, b;

        /* the probe writes "<tick> damage <spawn index> <entity id>" */
        if (sscanf(line, "%d damage %d %d", &tick, &si, &id) == 3)
        {
            if (n_events < 4096)
            {
                events[n_events].tick = tick;
                events[n_events].spawn_index = si;
                ++n_events;
            }
        }
        else if (sscanf(line, "%d playerhit %d %x %x", &tick, &si, &a, &b) == 4)
        {
            /* the player's health history rides in the player record */
        }
    }

    gzclose(f);
}

/* --------------------------------------------------------------- removals */

/* The native's own removal stream, filled by an_world.on_remove_cb. The tick is
 * appended by the replay loop, not by the hook. */
struct nremoval { int tick; int spawn_index; int entity_id; };

static struct nremoval nremovals[8192];
static int n_nremovals;

static void record_removal(int spawn_index, int entity_id, void *ctx)
{
    (void)ctx;

    if (n_nremovals < (int)(sizeof nremovals / sizeof nremovals[0]))
    {
        nremovals[n_nremovals].tick = -1;
        nremovals[n_nremovals].spawn_index = spawn_index;
        nremovals[n_nremovals].entity_id = entity_id;
        ++n_nremovals;
    }
}

/* ------------------------------------------------------------- the replay */

static struct living *find_spawn(struct an_world *an, int si)
{
    for (int i = 0; i < an->n; ++i)
        if (an_ent_at(an->slot[i])->spawn_index == si) return an_ent_at(an->slot[i])->is_living ? lv_get(an_ent_at(an->slot[i])->livh) : NULL;

    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "usage: test_slimes SLIMES_DIR [TRACE_PATH]\n");
        return 2;
    }

    const char *dir = argv[1];
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

    if (!strstr(manifest, "\"kind\":\"slimes\""))
    {
        printf("skip %s: not a slimes dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int ticks = (int)manifest_int(manifest, "ticks");
    int mobs = (int)manifest_int(manifest, "mobs");

    struct world w;
    world_init(&w, seed);
    /* the probe turns ChunkProviderServer.loadChunkOnProvideRequest off: an
     * unloaded chunk reads as air and is never generated */
    w.no_generate = 1;
    w.dim = (int)manifest_int(manifest, "dim");   /* the Nether run generates nether chunks */

    /* the loaded region, in the probe's load order */
    const char *lp = strstr(manifest, "\"loaded\":[");
    if (!lp) { fprintf(stderr, "%s: no loaded list\n", dir); return 2; }
    lp += strlen("\"loaded\":[");

    while (*lp == '[')
    {
        int lx = (int)strtol(lp + 1, (char **)&lp, 10);
        int lz = (int)strtol(lp + 1, (char **)&lp, 10);
        world_load_chunk(&w, lx, lz);

        if (*lp == ']') ++lp;
        if (*lp == ',') ++lp;
    }

    /* ------------------------------------------------------------ the shapes */
    snprintf(path, sizeof path, "%s/shapes.bin", dir);
    size_t slen;
    unsigned char *shapes = read_file(path, &slen);

    if (slen % 16 != 0)
    {
        fprintf(stderr, "%s: shapes.bin is %zu bytes\n", dir, slen);
        return 2;
    }

    int nshapes = (int)(slen / 16);

    for (int i = 0; i < nshapes; ++i)
    {
        const unsigned char *s = shapes + i * 16;
        int x = (int)le32(s), y = (int)le32(s + 4), z = (int)le32(s + 8);
        int id = s[12] | s[13] << 8, meta = s[14];

        world_set_block(&w, x, y, z, id, meta, 2);
    }

    free(shapes);

    /* ------------------------------------------------------------- the Det */
    struct ref start;
    read_ref(&start, dir, "start.txt");

    det_state det;
    det_init(&det);
    det_load(&det, start.world_seed, start.seeder, start.math, start.next_id);

    for (int i = 0; i < start.nsplits; ++i)
        det_split_add(&det, start.splits[i].name, start.splits[i].state, start.splits[i].used);

    struct an_world an;
    an_init(&an, &w, &det);
    an.on_remove_cb = record_removal;
    an.on_remove_ctx = NULL;
    an.dimension = (int)manifest_int(manifest, "dim");
    an.iew.world_rand.r.seed = start.world_rand & 0xFFFFFFFFFFFFULL;
    an.iew.world_rand.have_next_next_gaussian = start.world_rand_gauss;
    an.iew.world_rand.next_next_gaussian = 0.0;
    an.skylight = (int)manifest_int(manifest, "skylightSubtracted");

    /* ------------------------------------------------------------ the spawns */
    snprintf(path, sizeof path, "%s/spawns.bin", dir);
    size_t spnlen;
    unsigned char *spawns = read_file(path, &spnlen);

    if (spnlen % SPAWN_BYTES != 0)
    {
        fprintf(stderr, "%s: spawns.bin is %zu bytes\n", dir, spnlen);
        return 2;
    }

    int nspawn = (int)(spnlen / SPAWN_BYTES);

    if (nspawn != mobs + 1)
    {
        printf("FAIL %s: spawns.bin holds %d spawns, the manifest says %d mobs plus the player\n", dir, nspawn, mobs);
        ++failures;
    }

    trace_open(argc == 3 ? argv[2] : NULL);

    snprintf(path, sizeof path, "%s/spawns.txt.gz", dir);
    gzFile sfile = gzopen(path, "rb");

    if (!sfile)
    {
        perror(path);
        return 2;
    }

    char line[8192];

    for (int i = 0; i < nspawn; ++i)
    {
        const unsigned char *s = spawns + (size_t)i * SPAWN_BYTES;
        int index = (int)le32(s);
        int entity_id = (int)le32(s + 4);
        int kind = (int)le32(s + 8);
        int size = (int)le32(s + 12);
        double x = le_double(s + 16), y = le_double(s + 24), z = le_double(s + 32);
        float yaw = le_float(s + 40), pitch = le_float(s + 44);
        double mx = le_double(s + 48), my = le_double(s + 56), mz = le_double(s + 64);

        struct living *l;

        if (kind == 2)
        {
            l = an_spawn_player(&an, index, x, y, z, yaw, pitch);
        }
        else
        {
            l = an_spawn_slime(&an, kind == 1 ? SK_MAGMA_CUBE : SK_SLIME, index, x, y, z, yaw, pitch, size, 1);
        }

        if (!l)
        {
            printf("FAIL %s: could not spawn entity %d\n", dir, i);
            return 2;
        }

        cmp_ll(-1, i, "entity id", entity_id, l->entity_id);
        cmp_dbl(-1, i, "motionX", mx, l->e.motion_x);
        cmp_dbl(-1, i, "motionY", my, l->e.motion_y);
        cmp_dbl(-1, i, "motionZ", mz, l->e.motion_z);

        if (kind != 2 && gzgets(sfile, line, sizeof line))
        {
            /* "<spawn_index> <entity_id> <kind name> <size> <canonical NBT>" */
            char *rest = strchr(line, ' ');
            char *rest2 = rest ? strchr(rest + 1, ' ') : NULL;
            char *rest3 = rest2 ? strchr(rest2 + 1, ' ') : NULL;
            char *rest4 = rest3 ? strchr(rest3 + 1, ' ') : NULL;
            char *text = rest4 ? rest4 + 1 : NULL;

            if (text)
            {
                while (*text == ' ') ++text;
                size_t n = strlen(text);

                while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = 0;

                char *mine;
                living_nbt_text(l, &mine);

                if (strcmp(mine, text) != 0)
                {
                    struct nbt *want = nbt_parse(text);
                    struct nbt *got = nbt_parse(mine);

                    if (want && got)
                    {
                        char buf[512];
                        nbt_diff(want, got, buf, sizeof buf);
                        printf("FAIL %s spawn %d: NBT at spawn: %s\n", dir, i, buf);
                    }
                    else
                    {
                        printf("FAIL %s spawn %d: NBT at spawn differs and does not parse\n", dir, i);
                    }

                    ++failures;
                    nbt_free(want);
                    nbt_free(got);
                }

                free(mine);
            }
        }
        else if (kind == 2 && gzgets(sfile, line, sizeof line))
        {
            /* the player's own NBT (inventory, food, xp) is not modelled; only
             * the record's fields are compared */
        }
    }

    gzclose(sfile);
    free(spawns);

    read_events(dir);

    /* -------------------------------------------------------------- ticks */
    snprintf(path, sizeof path, "%s/ticks.bin.gz", dir);
    gzFile tf = gzopen(path, "rb");
    if (!tf) { perror(path); exit(2); }

    snprintf(path, sizeof path, "%s/digest.txt.gz", dir);
    gzFile df = gzopen(path, "rb");
    if (!df) { perror(path); exit(2); }

    snprintf(path, sizeof path, "%s/nbt64.txt.gz", dir);
    gzFile nf = gzopen(path, "rb");
    if (!nf) { perror(path); exit(2); }

    snprintf(path, sizeof path, "%s/removals.txt.gz", dir);
    gzFile rf = gzopen(path, "rb");
    if (!rf) { perror(path); exit(2); }

    unsigned char rec[REC_BYTES];
    unsigned char prec[PLAYER_BYTES];

    /* the removal stream */
    int rem_tick = -1, rem_si = 0, rem_id = 0, removal_eof = 0;
    unsigned long long rem_health = 0;

    if (gzgets(rf, line, sizeof line))
    {
        if (sscanf(line, "%d %d %d %llx", &rem_tick, &rem_si, &rem_id, &rem_health) != 4) removal_eof = 1;
    }
    else removal_eof = 1;

    /* the nbt64 stream: "t <tick>" then one line per entity */
    int nbt_tick = -1;
    int nbt_pending = 0;

    if (gzgets(nf, line, sizeof line))
    {
        if (sscanf(line, "t %d", &nbt_tick) == 1) nbt_pending = 1;
    }

    int skylight = (int)manifest_int(manifest, "skylightSubtracted");
    (void)skylight;

    int t;
    int ev = 0;
    int first_removal_tick = -1;

    for (t = 0; t < ticks; ++t)
    {
        /* the probe's damage events, before the entity pass */
        while (ev < n_events && events[ev].tick == t)
        {
            struct living *target = find_spawn(&an, events[ev].spawn_index);

            if (target) player_attack_entity_from(target, NULL, DMG_GENERIC, 1000.0F, 0, &det);

            ++ev;
        }

        int nrem_before = n_nremovals;

        an_tick(&an, t);

        /* the removals the oracle recorded for this tick */
        int nremoved = 0;
        int removed_id[64];
        int removed_si[64];

        while (!removal_eof && rem_tick == t)
        {
            if (first_removal_tick < 0) first_removal_tick = t;

            if (nremoved < 64)
            {
                removed_id[nremoved] = rem_id;
                removed_si[nremoved] = rem_si;
            }

            ++nremoved;

            if (gzgets(rf, line, sizeof line))
            {
                if (sscanf(line, "%d %d %d %llx", &rem_tick, &rem_si, &rem_id, &rem_health) != 4) removal_eof = 1;
            }
            else removal_eof = 1;
        }

        /* the native's own removals during the tick, from the hook. The oracle's
         * stream for the same tick was consumed above; entity ids are unique, so
         * that is the identity to compare (spawn indices repeat when the list
         * shrinks and a new entity takes the freed position). */
        int nrem_native = n_nremovals - nrem_before;

        if (nrem_native != nremoved)
        {
            printf("FAIL %s tick %d: the oracle removed %d entities, the native replay removed %d\n", dir, t, nremoved,
                   nrem_native);
            ++failures;
        }

        for (int k = nrem_before; k < n_nremovals; ++k)
        {
            int found = 0;

            for (int j = 0; j < nremoved; ++j)
                if (removed_id[j] == nremovals[k].entity_id && removed_si[j] == nremovals[k].spawn_index) found = 1;

            if (!found)
            {
                printf("FAIL %s tick %d: the native replay removed entity %d (spawn %d), which the oracle kept\n",
                       dir, t, nremovals[k].entity_id, nremovals[k].spawn_index);
                ++failures;
            }
        }

        for (int j = 0; j < nremoved; ++j)
        {
            int found = 0;

            for (int k = nrem_before; k < n_nremovals; ++k)
                if (nremovals[k].entity_id == removed_id[j] && nremovals[k].spawn_index == removed_si[j]) found = 1;

            if (!found)
            {
                printf("FAIL %s tick %d: the oracle removed entity %d (spawn %d), which the native replay kept\n",
                       dir, t, removed_id[j], removed_si[j]);
                ++failures;
            }
        }

        /* the tick header */
        unsigned char head[8];
        int hr = gzread(tf, head, 8);

        if (hr != 8)
        {
            printf("FAIL %s: no tick header at tick %d\n", dir, t);
            ++failures;
            break;
        }

        int rec_tick = (int)le32(head);
        int count = (int)le32(head + 4);

        if (rec_tick != t)
        {
            printf("FAIL %s: tick header says %d at tick %d\n", dir, rec_tick, t);
            ++failures;
            break;
        }

        if (count != an.n)
        {
            printf("FAIL %s tick %d: the oracle recorded %d live entities, the native replay has %d\n", dir, t, count, an.n);
            ++failures;
        }

        for (int i = 0; i < count; ++i)
        {
            if (gzread(tf, rec, REC_BYTES) != REC_BYTES)
            {
                printf("FAIL %s tick %d: the tick records end inside the tick\n", dir, t);
                ++failures;
                break;
            }

            int si = (int)le32(rec);
            struct an_ent *en = NULL;

            if (i < an.n && an_ent_at(an.slot[i])->spawn_index == si) en = an_ent_at(an.slot[i]);

            if (!en)
            {
                printf("FAIL %s tick %d: record %d is spawn %d, the native replay has %s\n", dir, t, i, si,
                       i < an.n ? "another entity" : "no entity");

                if (i < an.n)
                    printf("  the native list[%d] is spawn %d\n", i, an_ent_at(an.slot[i])->spawn_index);

                ++failures;
                continue;
            }

            cmp_ll(t, si, "entity id", (int)le32(rec + 4), en->is_living ? lv_get(en->livh)->entity_id : ie_get(en->ieh)->entity_id);
            cmp_ll(t, si, "kind", (int)le32(rec + 8), en->is_living ? (lv_get(en->livh)->kind == SK_SLIME ? 0 : (lv_get(en->livh)->kind == SK_MAGMA_CUBE ? 1 : 2)) : -1);

            unsigned char mine[REC_BYTES];
            slime_write_state(&an, en, mine);

            uint64_t want_hash = le64(rec + 12);
            uint64_t got_hash = le64(mine + 12);

            /* the probe player's own NBT is not modelled (see the nbt64 block) */
            if (want_hash != got_hash && !(en->is_living && lv_get(en->livh)->kind == SK_PLAYER))
            {
                char wb[32], gb[32];
                snprintf(wb, sizeof wb, "%016llx", (unsigned long long)want_hash);
                snprintf(gb, sizeof gb, "%016llx", (unsigned long long)got_hash);
                fail_rec(t, si, "nbt hash", wb, gb);

                /* the full NBT text tells which key moved */
                char *text;
                if (en->is_living) living_nbt_text(lv_get(en->livh), &text);
                else item_entity_nbt_text(ie_get(en->ieh), &text);

                if (nbt_pending && nbt_tick == t && i < an.n && (t < 8 || t == ticks - 1))
                {
                    printf("  native NBT: %s\n", text);
                }

                free(text);
            }

            cmp_ll(t, si, "ticksExisted", (int)le32(rec + 20), (int)le32(mine + 20));
            cmp_ll(t, si, "entityAge", (int)le32(rec + 24), (int)le32(mine + 24));
            cmp_ll(t, si, "slimeJumpDelay", (int)le32(rec + 28), (int)le32(mine + 28));
            cmp_flt(t, si, "moveForward", le_float(rec + 32), le_float(mine + 32));
            cmp_flt(t, si, "moveStrafing", le_float(rec + 36), le_float(mine + 36));
            cmp_flt(t, si, "rotationYawHead", le_float(rec + 40), le_float(mine + 40));
            cmp_flt(t, si, "renderYawOffset", le_float(rec + 44), le_float(mine + 44));
            cmp_flt(t, si, "squishAmount", le_float(rec + 48), le_float(mine + 48));
            cmp_flt(t, si, "squishFactor", le_float(rec + 52), le_float(mine + 52));
            cmp_flt(t, si, "prevSquishFactor", le_float(rec + 56), le_float(mine + 56));
            cmp_ll(t, si, "livingSoundTime", (int)le32(rec + 60), (int)le32(mine + 60));
            cmp_ll(t, si, "jumpTicks", (int)le32(rec + 64), (int)le32(mine + 64));
            cmp_ll(t, si, "hurtResistantTime", (int)le32(rec + 68), (int)le32(mine + 68));
            cmp_ll(t, si, "maxHurtResistantTime", (int)le32(rec + 72), (int)le32(mine + 72));
            cmp_ll(t, si, "recentlyHit", (int)le32(rec + 76), (int)le32(mine + 76));
            cmp_flt(t, si, "lastDamage", le_float(rec + 80), le_float(mine + 80));
            cmp_ll(t, si, "flags", (int)le32(rec + 84), (int)le32(mine + 84));

            uint64_t want_rand = le64(rec + 88) & 0xFFFFFFFFFFFFULL;
            uint64_t got_rand = le64(mine + 88) & 0xFFFFFFFFFFFFULL;

            if (want_rand != got_rand)
            {
                char wb[32], gb[32];
                snprintf(wb, sizeof wb, "%012llx", (unsigned long long)want_rand);
                snprintf(gb, sizeof gb, "%012llx", (unsigned long long)got_rand);
                fail_rec(t, si, "entity rand state", wb, gb);
            }

            cmp_ll(t, si, "slimeSize", (int)le32(rec + 96), (int)le32(mine + 96));
            cmp_ll(t, si, "hurtTime", (int)le32(rec + 100), (int)le32(mine + 100));
            cmp_ll(t, si, "maxHurtTime", (int)le32(rec + 104), (int)le32(mine + 104));
        }

        /* the player record */
        if (gzread(tf, prec, PLAYER_BYTES) != PLAYER_BYTES)
        {
            printf("FAIL %s tick %d: no player record\n", dir, t);
            ++failures;
            break;
        }

        unsigned char pmine[PLAYER_BYTES];
        slime_write_player(&an, pmine);

        int psi = 0;

        for (int i = 0; i < an.n; ++i)
            if (an_ent_at(an.slot[i])->is_living && lv_get(an_ent_at(an.slot[i])->livh) == lv_get(an.playerh)) psi = an_ent_at(an.slot[i])->spawn_index;

        cmp_ll(t, psi, "player entity id", (int)le32(prec + 0), (int)le32(pmine + 0));
        cmp_flt(t, psi, "player health", le_float(prec + 4), le_float(pmine + 4));
        cmp_flt(t, psi, "player prevHealth", le_float(prec + 8), le_float(pmine + 8));
        cmp_flt(t, psi, "player absorption", le_float(prec + 12), le_float(pmine + 12));
        cmp_flt(t, psi, "player lastDamage", le_float(prec + 16), le_float(pmine + 16));
        cmp_ll(t, psi, "player entityAge", (int)le32(prec + 20), (int)le32(pmine + 20));
        cmp_ll(t, psi, "player deathTime", (int)le32(prec + 24), (int)le32(pmine + 24));
        cmp_ll(t, psi, "player hurtTime", (int)le32(prec + 28), (int)le32(pmine + 28));
        cmp_ll(t, psi, "player maxHurtTime", (int)le32(prec + 32), (int)le32(pmine + 32));
        cmp_ll(t, psi, "player hurtResistantTime", (int)le32(prec + 36), (int)le32(pmine + 36));
        cmp_ll(t, psi, "player maxHurtResistantTime", (int)le32(prec + 40), (int)le32(pmine + 40));
        cmp_ll(t, psi, "player recentlyHit", (int)le32(prec + 44), (int)le32(pmine + 44));
        cmp_ll(t, psi, "player foodLevel", (int)le32(prec + 48), (int)le32(pmine + 48));
        cmp_flt(t, psi, "player foodSaturation", le_float(prec + 52), le_float(pmine + 52));
        cmp_flt(t, psi, "player foodExhaustion", le_float(prec + 56), le_float(pmine + 56));
        cmp_ll(t, psi, "player foodTimer", (int)le32(prec + 60), (int)le32(pmine + 60));
        cmp_ll(t, psi, "player prevFoodLevel", (int)le32(prec + 64), (int)le32(pmine + 64));
        cmp_dbl(t, psi, "player posX", le_double(prec + 68), le_double(pmine + 68));
        cmp_dbl(t, psi, "player posY", le_double(prec + 76), le_double(pmine + 76));
        cmp_dbl(t, psi, "player posZ", le_double(prec + 84), le_double(pmine + 84));
        cmp_dbl(t, psi, "player motionX", le_double(prec + 92), le_double(pmine + 92));
        cmp_dbl(t, psi, "player motionY", le_double(prec + 100), le_double(pmine + 100));
        cmp_dbl(t, psi, "player motionZ", le_double(prec + 108), le_double(pmine + 108));
        cmp_flt(t, psi, "player rotationYaw", le_float(prec + 116), le_float(pmine + 116));
        cmp_flt(t, psi, "player rotationPitch", le_float(prec + 120), le_float(pmine + 120));
        cmp_ll(t, psi, "player flags", (int)le32(prec + 124), (int)le32(pmine + 124));

        uint64_t want_prand = le64(prec + 128) & 0xFFFFFFFFFFFFULL;
        uint64_t got_prand = le64(pmine + 128) & 0xFFFFFFFFFFFFULL;

        if (want_prand != got_prand)
        {
            char wb[32], gb[32];
            snprintf(wb, sizeof wb, "%012llx", (unsigned long long)want_prand);
            snprintf(gb, sizeof gb, "%012llx", (unsigned long long)got_prand);
            fail_rec(t, psi, "player rand state", wb, gb);
        }

        /* the full NBT block of this tick */
        if (nbt_pending && nbt_tick == t)
        {
            for (int i = 0; i < an.n; ++i)
            {
                if (!gzgets(nf, line, sizeof line))
                {
                    printf("FAIL %s tick %d: the nbt64 stream ended early\n", dir, t);
                    ++failures;
                    nbt_pending = 0;
                    break;
                }

                char *sp = strchr(line, ' ');
                char *text = sp ? sp + 1 : NULL;

                if (!text) continue;

                size_t n = strlen(text);
                while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = 0;

                /* the probe player's own NBT (inventory, food, xp, abilities,
                 * the attackDamage attribute) is not modelled; its tick is
                 * compared field by field in the player record instead */
                if (an_ent_at(an.slot[i])->is_living && lv_get(an_ent_at(an.slot[i])->livh)->kind == SK_PLAYER) continue;

                char *mine;
                if (an_ent_at(an.slot[i])->is_living) living_nbt_text(lv_get(an_ent_at(an.slot[i])->livh), &mine);
                else item_entity_nbt_text(ie_get(an_ent_at(an.slot[i])->ieh), &mine);

                if (strcmp(mine, text) != 0)
                {
                    struct nbt *wa = nbt_parse(text);
                    struct nbt *go = nbt_parse(mine);

                    if (wa && go)
                    {
                        char buf[512];
                        nbt_diff(wa, go, buf, sizeof buf);
                        printf("FAIL %s tick %d entity %d: %s\n", dir, t, an_ent_at(an.slot[i])->spawn_index, buf);
                    }
                    else
                    {
                        printf("FAIL %s tick %d entity %d: the NBT texts differ\n", dir, t, an_ent_at(an.slot[i])->spawn_index);
                    }

                    ++failures;
                    nbt_free(wa);
                    nbt_free(go);
                }

                free(mine);
            }

            if (gzgets(nf, line, sizeof line))
            {
                if (sscanf(line, "t %d", &nbt_tick) != 1) nbt_pending = 0;
            }
            else nbt_pending = 0;
        }

        /* the digest line */
        if (!gzgets(df, line, sizeof line))
        {
            printf("FAIL %s: the digest line is missing at tick %d\n", dir, t);
            ++failures;
            break;
        }

        {
            char *tok[40];
            int n = 0;
            char *p = strtok(line, " ");

            while (p && n < 40)
            {
                tok[n++] = p;
                p = strtok(NULL, " ");
            }

            if (n != 28 || strcmp(tok[0], "t") != 0 || atoi(tok[1]) != t)
            {
                printf("FAIL %s tick %d: the digest line has %d tokens\n", dir, t, n);
                ++failures;
            }
            else
            {
                for (int role = 0; role < DET_ROLES; ++role)
                {
                    uint64_t a = strtoull(tok[4 + role * 5], NULL, 16);
                    uint64_t b = strtoull(tok[5 + role * 5], NULL, 16);
                    uint64_t c = strtoull(tok[6 + role * 5], NULL, 16);

                    if (det_seeder_state(an.det, role) != a)
                    {
                        printf("FAIL %s tick %d: role %d seeder state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)a,
                               (unsigned long long)det_seeder_state(an.det, role));
                        ++failures;
                    }

                    if (det_math_state(an.det, role) != b)
                    {
                        printf("FAIL %s tick %d: role %d math state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)b,
                               (unsigned long long)det_math_state(an.det, role));
                        ++failures;
                    }

                    if (det_split_state(an.det, role) != c)
                    {
                        printf("FAIL %s tick %d: role %d split state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)c,
                               (unsigned long long)det_split_state(an.det, role));
                        ++failures;
                    }
                }

                uint64_t nid = strtoull(tok[23], NULL, 10);

                if ((uint32_t)an.det->next_id[DET_OTHER] != (uint32_t)nid)
                {
                    printf("FAIL %s tick %d: next id: the oracle recorded %llu, the native replay has %d\n",
                           dir, t, (unsigned long long)nid, an.det->next_id[DET_OTHER]);
                    ++failures;
                }

                uint64_t wr = strtoull(tok[25], NULL, 16);

                if (an.iew.world_rand.r.seed != (wr & 0xFFFFFFFFFFFFULL))
                {
                    printf("FAIL %s tick %d: world rand: the oracle recorded %016llx, the native replay has"
                           " %012llx\n", dir, t, (unsigned long long)wr,
                           (unsigned long long)an.iew.world_rand.r.seed);
                    ++failures;
                }

                cmp_ll(t, -1, "world rand gaussian pending", atoi(tok[27]), an.iew.world_rand.have_next_next_gaussian);
            }
        }


        if (failures > 40)
        {
            printf("FAIL %s: stopping after 40 failures at tick %d\n", dir, t);
            break;
        }
    }

    /* the removal stream must be exhausted */
    if (!removal_eof)
    {
        printf("FAIL %s: the oracle recorded a removal at tick %d, the native replay ran out of ticks\n", dir, rem_tick);
        ++failures;
    }

    /* ------------------------------------------------------- the final blocks */
    snprintf(path, sizeof path, "%s/final.bin.gz", dir);
    gzFile ff = gzopen(path, "rb");

    if (ff)
    {
        const char *cp = strstr(manifest, "\"compare\":[");
        if (!cp) { fprintf(stderr, "%s: no compare list\n", dir); return 2; }
        cp += strlen("\"compare\":[");
        unsigned char ids[65536 * 3];
        int chunk_fail = 0;

        /* final.bin.gz holds every loaded chunk in the probe's load order, which
         * starts at the outer ring; the manifest's compare list holds only the
         * platform's own chunks. Walk the file forward to each compare chunk
         * instead of pairing the two lists position by position. */
        while (*cp == '[')
        {
            int want_cx = (int)strtol(cp + 1, (char **)&cp, 10);
            int want_cz = (int)strtol(cp + 1, (char **)&cp, 10);

            if (*cp == ']') ++cp;
            if (*cp == ',') ++cp;

            int wx = 0, wz = 0;

            for (;;)
            {
                unsigned char cx[4], cz[4];

                if (gzread(ff, cx, 4) != 4 || gzread(ff, cz, 4) != 4) break;

                wx = (int)((uint32_t)cx[0] << 24 | (uint32_t)cx[1] << 16 | (uint32_t)cx[2] << 8 | cx[3]);
                wz = (int)((uint32_t)cz[0] << 24 | (uint32_t)cz[1] << 16 | (uint32_t)cz[2] << 8 | cz[3]);

                if (wx == want_cx && wz == want_cz) break;

                if (gzseek(ff, 65536 * 3 + 1024 + 1024 + 4 + 2, SEEK_CUR) < 0) break;
            }

            if (wx != want_cx || wz != want_cz)
            {
                printf("FAIL %s: final.bin.gz has no chunk (%d, %d)\n", dir, want_cx, want_cz);
                ++failures;
                break;
            }

            if (gzread(ff, ids, 65536 * 3) != 65536 * 3) break;

            unsigned char heights[1024 + 1024 + 4];
            if (gzread(ff, heights, sizeof heights) != (int)sizeof heights) break;

            unsigned char mask[2];
            if (gzread(ff, mask, 2) != 2) break;

            struct chunk *c = world_chunk(&w, wx, wz);

            if (!c)
            {
                printf("FAIL %s: the native world has no chunk (%d, %d)\n", dir, wx, wz);
                ++failures;
                chunk_fail = 1;
                break;
            }

            for (int cell = 0; cell < 65536 && !chunk_fail; ++cell)
            {
                int want = ids[cell * 2] | ids[cell * 2 + 1] << 8;
                int got = chunk_cell_id(c, cell) & 4095;

                if (want != got)
                {
                    int x = cell >> 12, z = (cell >> 8) & 15, y = cell & 255;
                    printf("FAIL %s: final block at (%d, %d, %d): the oracle recorded id %d, the native replay"
                           " has %d\n", dir, wx * 16 + x, y, wz * 16 + z, want, got);
                    ++failures;
                    chunk_fail = 1;
                }
            }

            for (int cell = 0; cell < 65536 && !chunk_fail; ++cell)
            {
                int want = ids[131072 + cell];
                int got = chunk_cell_meta(c, cell);

                if (want != got)
                {
                    int x = cell >> 12, z = (cell >> 8) & 15, y = cell & 255;
                    printf("FAIL %s: final meta at (%d, %d, %d): the oracle recorded %d, the native replay has"
                           " %d\n", dir, wx * 16 + x, y, wz * 16 + z, want, got);
                    ++failures;
                    chunk_fail = 1;
                }
            }

            (void)mask;
        }

        gzclose(ff);
    }

    gzclose(tf);
    gzclose(df);
    gzclose(nf);
    gzclose(rf);

    printf("%s: %d spawns, %d ticks, %d live entities, %d events, first removal tick %d, %d failures\n",
           dir, nspawn, t, an.n, n_events, first_removal_tick, failures);

    trace_close();
    an_free(&an);
    free(manifest);
    return failures != 0 ? 1 : 0;
}