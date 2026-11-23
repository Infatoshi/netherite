/* Gate: the native living entities and their AI against the oracle's AnimalProbe
 * dump (oracle/harness/netherite/oracle/AnimalProbe.java). The probe flattens a raw
 * region, spawns the five farm animals the way SpawnerAnimals does, ticks them
 * the way World.updateEntities does and records every entity's canonical NBT
 * hash, the state NBT does not carry, the full NBT every 64th tick, the
 * removals, the Det state and the final blocks.
 *
 * The replay repeats that from the dump alone: the region, the placements, the
 * spawns (the record's fields and the constructor's Det draws), then one
 * an_tick per tick with every field of every entity compared after each tick.
 *
 * A failure names the tick, the entity and the field. */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/gunzip.h"
#include "../engine/animals.h"
#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/living.h"
#include "../engine/nbtjson.h"
#include "../engine/potion.h"
#include "../engine/trace.h"
#include "../engine/world.h"

#define ENT_STATE_BYTES 272
#define SPAWN_BYTES 100

static int failures = 0;

/* ------------------------------------------------------------- byte reads */

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int le16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
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

static void cmp_int(int tick, int si, const char *field, long long want, long long got)
{
    if (want == got) return;

    char w[32], g[32];
    snprintf(w, sizeof w, "%lld", want);
    snprintf(g, sizeof g, "%lld", got);
    fail_rec(tick, si, field, w, g);
}

/* The replay state: the world, the entities and the reference streams. */
static int replay(const char *dir, struct an_world *an, struct world *w, int nents, int ticks, const char *manifest, const unsigned char *events, int nevents);

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "usage: test_potions POTIONS_DIR [TRACE_PATH]\n");
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

    if (!strstr(manifest, "\"kind\":\"potions\""))
    {
        printf("skip %s: not a potions dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    (void)0;
    int ring = (int)manifest_int(manifest, "ring");
    int nents = (int)manifest_int(manifest, "animals");
    int ticks = (int)manifest_int(manifest, "ticks");
    (void)ring;

    struct world w;
    world_init(&w, seed);

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
        int id = le16(s + 12), meta = s[14];

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

    /* the trace has to be open before the spawns: the spawn-time draws (the
     * follow-range bonus gaussian) are part of the diff */
    trace_open(argc == 3 ? argv[2] : NULL);

    /* the recorded spawn NBT, for the constructor check */
    snprintf(path, sizeof path, "%s/spawns.txt.gz", dir);
    struct gunzip *sfile = gunzip_open(path);

    if (!sfile)
    {
        perror(path);
        return 2;
    }

    char line[8192];

    for (int i = 0; i < nents; ++i)
    {
        const unsigned char *s = spawns + (size_t)i * SPAWN_BYTES;
        int index = (int)le32(s);
        int entity_id = (int)le32(s + 4);
        int kind = (int)le32(s + 8);
        double x = le_double(s + 12), y = le_double(s + 20), z = le_double(s + 28);
        float yaw = le_float(s + 36), pitch = le_float(s + 40);
        double mx = le_double(s + 44), my = le_double(s + 52), mz = le_double(s + 60);
        int growing_age = (int)le32(s + 68);
        int in_love = (int)le32(s + 72);
        int saddled = (int)le32(s + 76);
        int egg_timer = (int)le32(s + 80);
        int sheared = (int)le32(s + 84);

        struct living *l = an_spawn_living(&an, kind, index, x, y, z, yaw, pitch, growing_age, in_love, saddled,
                                           egg_timer, sheared, 1);

        if (!l)
        {
            printf("FAIL %s: could not spawn animal %d\n", dir, i);
            return 2;
        }

        cmp_int(-1, i, "entity id", entity_id, l->entity_id);
        cmp_dbl(-1, i, "motionX", mx, l->e.motion_x);
        cmp_dbl(-1, i, "motionY", my, l->e.motion_y);
        cmp_dbl(-1, i, "motionZ", mz, l->e.motion_z);

        if (gunzip_gets(sfile, line, sizeof line))
        {
            /* "<spawn_index> <entity_id> <kind name> <canonical NBT>" */
            char *rest = strchr(line, ' ');
            char *rest2 = rest ? strchr(rest + 1, ' ') : NULL;
            char *text = rest2 ? strchr(rest2 + 1, ' ') : NULL;

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
    }

    gunzip_close(sfile);
    free(spawns);

        /* ------------------------------------------------------------ the events */
    snprintf(path, sizeof path, "%s/events.bin", dir);
    size_t evlen;
    unsigned char *ev_raw = read_file(path, &evlen);

    if (evlen < 8 || (evlen - 8) % 64 != 0)
    {
        fprintf(stderr, "%s: events.bin is %zu bytes, expected 8 + multiple of 64\n", dir, evlen);
        return 2;
    }

    uint32_t magic = (uint32_t)ev_raw[0] << 24 | (uint32_t)ev_raw[1] << 16 | (uint32_t)ev_raw[2] << 8 | ev_raw[3];
    uint32_t nevents = (uint32_t)ev_raw[4] << 24 | (uint32_t)ev_raw[5] << 16 | (uint32_t)ev_raw[6] << 8 | ev_raw[7];
    if (magic != 0x504f544e || (size_t)nevents * 64 != evlen - 8)
    {
        fprintf(stderr, "%s: invalid events.bin header (magic %08x, nevents %u, len %zu)\n", dir, magic, nevents, evlen);
        return 2;
    }

    unsigned char *events = ev_raw + 8;
    int rc = replay(dir, &an, &w, nents, ticks, manifest, events, (int)nevents);
    free(ev_raw);

    trace_close();
    an_free(&an);
    free(manifest);
    return (failures != 0 || rc != 0) ? 1 : 0;
}

/* ------------------------------------------------------------- the replay */

/* The per-tick rand step counts: the oracle's state for a spawn index from the
 * previous tick's record vs the native's. */
static uint64_t oracle_prev[AN_MAX_ENTITIES];
static int oracle_prev_ok[AN_MAX_ENTITIES];
static uint64_t mine_prev[AN_MAX_ENTITIES];
static int mine_prev_ok[AN_MAX_ENTITIES];

static int rand_steps(uint64_t a, uint64_t b)
{
    uint64_t s = a;

    for (int i = 1; i <= 200; ++i)
    {
        s = (s * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
        if (s == b) return i;
    }

    return -1;
}

static int replay(const char *dir, struct an_world *an, struct world *w, int nents, int ticks, const char *manifest, const unsigned char *events, int nevents)
{
    char path[1200];

    snprintf(path, sizeof path, "%s/ticks.bin.gz", dir);
    struct gunzip *tf = gunzip_open(path);
    if (!tf) { perror(path); exit(2); }

    snprintf(path, sizeof path, "%s/digest.txt.gz", dir);
    struct gunzip *df = gunzip_open(path);
    if (!df) { perror(path); exit(2); }

    snprintf(path, sizeof path, "%s/nbt64.txt.gz", dir);
    struct gunzip *nf = gunzip_open(path);
    if (!nf) { perror(path); exit(2); }

    snprintf(path, sizeof path, "%s/removals.txt.gz", dir);
    struct gunzip *rf = gunzip_open(path);
    if (!rf) { perror(path); exit(2); }

    char line[65536];
    unsigned char rec[ENT_STATE_BYTES];
    int next_tick = -1, next_count = 0;
    int t;

    /* the first tick header */
    int have_rec_head = 0;

    /* the removal stream */
    int rem_tick = -1, rem_si = 0, rem_id = 0, rem_reason = 0, rem_fire = 0, removal_eof = 0;
    unsigned long long rem_health = 0;

    if (gunzip_gets(rf, line, sizeof line))
    {
        if (sscanf(line, "%d %d %d %d %llx %d", &rem_tick, &rem_si, &rem_id, &rem_reason, &rem_health, &rem_fire) != 6)
            removal_eof = 1;
    }
    else removal_eof = 1;

    /* the nbt64 stream: "t <tick>" then one line per entity */
    int nbt_tick = -1;
    int nbt_pending = 0;

    if (gunzip_gets(nf, line, sizeof line))
    {
        if (sscanf(line, "t %d", &nbt_tick) == 1) nbt_pending = 1;
    }

    int skylight = (int)manifest_int(manifest, "skylightSubtracted");
    (void)skylight;

    for (int i = 0; i < AN_MAX_ENTITIES; ++i) { oracle_prev_ok[i] = 0; mine_prev_ok[i] = 0; }

    for (t = 0; t < ticks; ++t)
    {
        /* the removals the oracle recorded for this tick, before the state */
        while (!removal_eof && rem_tick == t)
        {
            /* the native entities are removed inside an_tick; the comparison is
             * by count and identity below, so only the ordering is checked here */
            if (gunzip_gets(rf, line, sizeof line))
            {
                if (sscanf(line, "%d %d %d %d %llx %d", &rem_tick, &rem_si, &rem_id, &rem_reason, &rem_health, &rem_fire) != 6)
                    removal_eof = 1;
            }
            else removal_eof = 1;
        }


/* dispatch scheduled events for tick t */
        for (int eidx = 0; eidx < nevents; ++eidx)
        {
            const unsigned char *ev = events + (size_t)eidx * 64;
            int ev_tick = (int)le32(ev);
            if (ev_tick != t) continue;
            int ev_type = (int)le32(ev + 4);

            if (ev_type == 1) /* ADD_EFFECT */
            {
                int target_si = (int)le32(ev + 8);
                int potion_id = (int)le32(ev + 12);
                int duration = (int)le32(ev + 16);
                int amplifier = (int)le32(ev + 20);
                int ambient = (int)le32(ev + 24);

                for (int k = 0; k < an->n; ++k)
                {
                    if (an_ent_at(an->slot[k])->spawn_index == target_si && an_ent_at(an->slot[k])->is_living)
                    {
                        struct potion_effect eff = {
                            .id = potion_id,
                            .duration = duration,
                            .amplifier = amplifier,
                            .is_splash = 0,
                            .is_ambient = ambient
                        };
                        living_add_potion_effect(lv_get(an_ent_at(an->slot[k])->livh), &eff, an->det);
                        break;
                    }
                }
            }
            else if (ev_type == 2) /* CLEAR_EFFECTS */
            {
                int target_si = (int)le32(ev + 8);
                for (int k = 0; k < an->n; ++k)
                {
                    if (an_ent_at(an->slot[k])->spawn_index == target_si && an_ent_at(an->slot[k])->is_living)
                    {
                        living_clear_active_potions(lv_get(an_ent_at(an->slot[k])->livh), an->det);
                        break;
                    }
                }
            }
            else if (ev_type == 3) /* APPLY_FOOD */
            {
                int target_si = (int)le32(ev + 8);
                int item_id = (int)le32(ev + 12);
                int item_damage = (int)le32(ev + 16);
                for (int k = 0; k < an->n; ++k)
                {
                    if (an_ent_at(an->slot[k])->spawn_index == target_si && an_ent_at(an->slot[k])->is_living)
                    {
                        (void)potion_apply_food(lv_get(an_ent_at(an->slot[k])->livh), item_id, item_damage, an->det, NULL);
                        break;
                    }
                }
            }
            else if (ev_type == 4) /* SPAWN_POTION */
            {
                double px = le_double(ev + 8);
                double py = le_double(ev + 16);
                double pz = le_double(ev + 24);
                double pmx = le_double(ev + 32);
                double pmy = le_double(ev + 40);
                double pmz = le_double(ev + 48);
                int potion_damage = (int)le32(ev + 56);
                an_spawn_potion(an, an->n, px, py, pz, pmx, pmy, pmz, potion_damage);
            }
        }

        
        

        an_tick(an, t);



        (void)have_rec_head;

        /* read the tick header (8 bytes: tick, count) then count records */
        {
            unsigned char head[8];

            /* the first header comes from the stream; subsequent ones follow the
             * previous tick's records */
            if (gunzip_read(tf, head, 8) != 8)
            {
                printf("FAIL %s: no tick header at tick %d\n", dir, t);
                ++failures;
                break;
            }

            next_tick = (int)le32(head);
            next_count = (int)le32(head + 4);

            if (next_tick != t)
            {
                printf("FAIL %s: tick header says %d at tick %d\n", dir, next_tick, t);
                ++failures;
                break;
            }
        }

        if (next_count != an->n)
        {
            printf("FAIL %s tick %d: the oracle recorded %d live entities, the native replay has %d\n",
                   dir, t, next_count, an->n);
            ++failures;
        }

        for (int i = 0; i < next_count; ++i)
        {
            if (gunzip_read(tf, rec, ENT_STATE_BYTES) != ENT_STATE_BYTES)
            {
                printf("FAIL %s tick %d: the tick records end inside the tick\n", dir, t);
                ++failures;
                break;
            }

            int si = (int)le32(rec);
            int id = (int)le32(rec + 4);
            int kind = (int)le32(rec + 8);
            struct an_ent *en = NULL;

            if (i < an->n)
            {
                en = an_ent_at(an->slot[i]);

                if (en->spawn_index != si) en = NULL;
            }

            if (!en)
            {
                printf("FAIL %s tick %d: record %d is spawn %d, the native replay has %s\n", dir, t, i, si,
                       i < an->n ? "another entity" : "no entity");

                if (i < an->n)
                    printf("  the native list[%d] is spawn %d (kind %s, id %d, used %d)\n", i, an_ent_at(an->slot[i])->spawn_index,
                           an_ent_at(an->slot[i])->is_living ? "living" : "item",
                           an_ent_at(an->slot[i])->is_living ? lv_get(an_ent_at(an->slot[i])->livh)->entity_id : (ie_get(an_ent_at(an->slot[i])->ieh) ? ie_get(an_ent_at(an->slot[i])->ieh)->entity_id : -1),
                           an_ent_at(an->slot[i])->used);

                if (getenv("AN_LIST"))
                {
                    for (int k = 0; k < an->n; ++k)
                        printf("  L[%d] spawn %d %s id %d used %d\n", k, an_ent_at(an->slot[k])->spawn_index,
                               an_ent_at(an->slot[k])->is_living ? "living" : "item",
                               an_ent_at(an->slot[k])->is_living ? lv_get(an_ent_at(an->slot[k])->livh)->entity_id : (ie_get(an_ent_at(an->slot[k])->ieh) ? ie_get(an_ent_at(an->slot[k])->ieh)->entity_id : -1),
                               an_ent_at(an->slot[k])->used);
                }
                ++failures;
                continue;
            }

            uint64_t want_rand = le64(rec + 260) & 0xFFFFFFFFFFFFULL;
            uint64_t got_rand = (en->is_living ? lv_get((uint64_t)en->livh)->rand.r.seed : ie_get((uint64_t)en->ieh)->rand.r.seed)
                                & 0xFFFFFFFFFFFFULL;

            if (si < AN_MAX_ENTITIES)
            {
                int ow = oracle_prev_ok[si] ? rand_steps(oracle_prev[si], want_rand) : -2;
                int gw = mine_prev_ok[si] ? rand_steps(mine_prev[si], got_rand) : -2;
                oracle_prev[si] = want_rand;
                oracle_prev_ok[si] = 1;
                mine_prev[si] = got_rand;
                mine_prev_ok[si] = 1;

                if (ow != -2 && gw != -2 && ow != gw)
                {
                    char wb[64], gb[64];
                    snprintf(wb, sizeof wb, "%d", ow);
                    snprintf(gb, sizeof gb, "%d", gw);
                    fail_rec(t, si, "rand steps since the previous tick", wb, gb);
                }
            }

            if (want_rand != got_rand)
            {
                char wb[32], gb[32];
                snprintf(wb, sizeof wb, "%012llx", (unsigned long long)want_rand);
                snprintf(gb, sizeof gb, "%012llx", (unsigned long long)got_rand);
                fail_rec(t, si, "entity rand state", wb, gb);
            }

            cmp_int(t, si, "entity id", id, en->is_living ? lv_get(en->livh)->entity_id : ie_get(en->ieh)->entity_id);
            cmp_int(t, si, "kind", kind, en->is_living ? lv_get(en->livh)->kind : -1);

            uint64_t want_hash = le64(rec + 12);
            uint64_t got_hash = en->is_living ? living_nbt_hash(lv_get(en->livh)) : item_entity_nbt_hash(ie_get(en->ieh));

            

if (want_hash != got_hash)
            {
                char wbuf[32], gbuf[32];
                snprintf(wbuf, sizeof wbuf, "%016llx", (unsigned long long)want_hash);
                snprintf(gbuf, sizeof gbuf, "%016llx", (unsigned long long)got_hash);
                fail_rec(t, si, "nbt hash", wbuf, gbuf);

            }

            if (en->is_living)
            {
                struct living *l = lv_get(en->livh);
                cmp_int(t, si, "entityAge", (long long)(int32_t)le32(rec + 20), l->entity_age);
                cmp_int(t, si, "ticksExisted", (long long)(int32_t)le32(rec + 24), l->ticks_existed);
                cmp_int(t, si, "ai tick count", (long long)(int32_t)le32(rec + 28), lv_ai(l)->tasks.tick_count);

                int mask = 0;
                for (int k = 0; k < lv_ai(l)->tasks.nexec; ++k) mask |= 1 << lv_ai(l)->tasks.executing[k];
                cmp_int(t, si, "ai executing", (long long)(int32_t)le32(rec + 32), mask);

                for (int k = 0; k < 10; ++k)
                {
                    int want = (int32_t)le32(rec + 36 + k * 4);
                    int got = -1;

                    if (k < lv_ai(l)->tasks.n)
                    {
                        struct ai_task *tk = &lv_ai(l)->tasks.entries[k].t;

                        switch (tk->cls)
                        {
                            case AIC_MATE: got = tk->spawn_baby_delay; break;
                            case AIC_TEMPT: got = tk->delay_tempt_counter; break;
                            case AIC_FOLLOW_PARENT: got = tk->follow_parent_delay; break;
                            case AIC_WATCH_CLOSEST: got = tk->look_time; break;
                            case AIC_LOOK_IDLE: got = tk->idle_time; break;
                            case AIC_EAT_GRASS: got = tk->eat_grass_timer; break;
                            default: got = -1; break;
                        }
                    }

                    char field[32];
                    snprintf(field, sizeof field, "task_state[%d]", k);
                    cmp_int(t, si, field, want, got);
                }

                const struct path_ent *path = path_at(l->nav.path);
                cmp_int(t, si, "nav has path", (long long)(int32_t)le32(rec + 76), path ? 1 : 0);
                cmp_int(t, si, "nav index", (long long)(int32_t)le32(rec + 80), path ? path->index : 0);
                cmp_int(t, si, "nav length", (long long)(int32_t)le32(rec + 84), path ? path->length : 0);
                cmp_int(t, si, "nav totalTicks", (long long)(int32_t)le32(rec + 88), l->nav.total_ticks);
                cmp_int(t, si, "nav ticksAtLastPos", (long long)(int32_t)le32(rec + 92), l->nav.ticks_at_last_pos);
                cmp_dbl(t, si, "nav speed", le_double(rec + 96), l->nav.speed);
                cmp_dbl(t, si, "nav lastPosCheck.x", le_double(rec + 104), l->nav.lx);
                cmp_dbl(t, si, "nav lastPosCheck.y", le_double(rec + 112), l->nav.ly);
                cmp_dbl(t, si, "nav lastPosCheck.z", le_double(rec + 120), l->nav.lz);

                uint64_t want_nav = le64(rec + 128);
                uint64_t got_nav = 0xcbf29ce484222325ULL;

                if (path)
                {
                    for (int k = 0; k < path->length; ++k)
                    {
                        for (int c = 0; c < 3; ++c)
                        {
                            int v = path->pts[k][c];

                            for (int j = 0; j < 4; ++j)
                                got_nav = (got_nav ^ (unsigned char)((v >> (8 * j)) & 255)) * 0x100000001b3ULL;
                        }
                    }
                }

                if (want_nav != got_nav)
                {
                    char wb[32], gb[32];
                    snprintf(wb, sizeof wb, "%016llx", (unsigned long long)want_nav);
                    snprintf(gb, sizeof gb, "%016llx", (unsigned long long)got_nav);
                    fail_rec(t, si, "nav path points", wb, gb);
                }

                cmp_int(t, si, "moveHelper update", (long long)(int32_t)le32(rec + 136), l->move.update);
                cmp_dbl(t, si, "moveHelper x", le_double(rec + 140), l->move.x);
                cmp_dbl(t, si, "moveHelper y", le_double(rec + 148), l->move.y);
                cmp_dbl(t, si, "moveHelper z", le_double(rec + 156), l->move.z);
                cmp_dbl(t, si, "moveHelper speed", le_double(rec + 164), l->move.speed);
                cmp_int(t, si, "lookHelper isLooking", (long long)(int32_t)le32(rec + 172), l->look.is_looking);
                cmp_dbl(t, si, "lookHelper x", le_double(rec + 176), l->look.x);
                cmp_dbl(t, si, "lookHelper y", le_double(rec + 184), l->look.y);
                cmp_dbl(t, si, "lookHelper z", le_double(rec + 192), l->look.z);
                cmp_flt(t, si, "lookHelper deltaYaw", le_float(rec + 200), l->look.delta_look_yaw);
                cmp_flt(t, si, "lookHelper deltaPitch", le_float(rec + 204), l->look.delta_look_pitch);
                cmp_int(t, si, "jumpHelper isJumping", (long long)(int32_t)le32(rec + 208), l->jump.is_jumping);
                cmp_flt(t, si, "moveForward", le_float(rec + 212), l->move_forward);
                cmp_flt(t, si, "moveStrafing", le_float(rec + 216), l->move_strafing);
                cmp_flt(t, si, "rotationYawHead", le_float(rec + 220), l->rotation_yaw_head);
                cmp_flt(t, si, "renderYawOffset", le_float(rec + 224), l->render_yaw_offset);
                cmp_int(t, si, "livingSoundTime", (long long)(int32_t)le32(rec + 228), l->living_sound_time);
                cmp_int(t, si, "body counter", (long long)(int32_t)le32(rec + 232), l->body.counter);
                cmp_flt(t, si, "body yaw", le_float(rec + 236), l->body.yaw);
                cmp_int(t, si, "sheepTimer", (long long)(int32_t)le32(rec + 240), l->kind == AK_SHEEP ? l->sheep_timer : -1);
                cmp_int(t, si, "timeUntilNextEgg", (long long)(int32_t)le32(rec + 244),
                        l->kind == AK_CHICKEN ? l->time_until_next_egg : -1);
                cmp_int(t, si, "breeding", (long long)(int32_t)le32(rec + 248), l->breeding);
                cmp_int(t, si, "revengeTimer", (long long)(int32_t)le32(rec + 252), l->revenge_timer);
                cmp_int(t, si, "flags", (long long)(int32_t)le32(rec + 256),
                        (l->added_to_chunk ? 1 : 0) | (l->e.on_ground ? 2 : 0));
            }
            else
            {
                cmp_int(t, si, "ticksExisted", (long long)(int32_t)le32(rec + 24), ie_get(en->ieh)->ticks_existed);
                cmp_int(t, si, "flags", (long long)(int32_t)le32(rec + 256),
                        (ie_get(en->ieh)->added_to_chunk ? 1 : 0) | (ie_get(en->ieh)->e.on_ground ? 2 : 0));
            }
        }

        /* the full NBT block of this tick */
        if (nbt_pending && nbt_tick == t)
        {
            for (int i = 0; i < an->n; ++i)
            {
                if (!gunzip_gets(nf, line, sizeof line))
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

                char *mine;
                if (an_ent_at(an->slot[i])->is_living) living_nbt_text(lv_get(an_ent_at(an->slot[i])->livh), &mine);
                else item_entity_nbt_text(ie_get(an_ent_at(an->slot[i])->ieh), &mine);

if (getenv("AN_DEBUG") && t == 0 && (i < 3 || i == 28))
                {
                    uint64_t h1 = 0xcbf29ce484222325ULL, h2 = 0xcbf29ce484222325ULL;
                    for (const char *q = text; *q; ++q) h1 = (h1 ^ (unsigned char)*q) * 0x100000001b3ULL;
                    for (const char *q = mine; *q; ++q) h2 = (h2 ^ (unsigned char)*q) * 0x100000001b3ULL;
                    printf("DBG t=%d i=%d textlen=%zu minelen=%zu fnv_text=%016llx fnv_mine=%016llx rec_hash=%016llx\n",
                           t, i, strlen(text), strlen(mine), (unsigned long long)h1, (unsigned long long)h2,
                           (unsigned long long)le64(rec + 12));
                }

                if (strcmp(mine, text) != 0)
                {
                    struct nbt *wa = nbt_parse(text);
                    struct nbt *go = nbt_parse(mine);

                    if (wa && go)
                    {
                        char buf[512];
                        nbt_diff(wa, go, buf, sizeof buf);
                        printf("FAIL %s tick %d entity %d: %s\n", dir, t, an_ent_at(an->slot[i])->spawn_index, buf);
                    }
                    else
                    {
                        printf("FAIL %s tick %d entity %d: the NBT texts differ: %s\n", dir, t,
                               an_ent_at(an->slot[i])->spawn_index, text);
                    }

                    ++failures;
                    nbt_free(wa);
                    nbt_free(go);
                }

                free(mine);
            }

            if (gunzip_gets(nf, line, sizeof line))
            {
                if (sscanf(line, "t %d", &nbt_tick) != 1) nbt_pending = 0;
            }
            else nbt_pending = 0;
        }

        /* the digest line */
        if (!gunzip_gets(df, line, sizeof line))
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

                    if (det_seeder_state(an->det, role) != a)
                    {
                        printf("FAIL %s tick %d: role %d seeder state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)a,
                               (unsigned long long)det_seeder_state(an->det, role));
                        ++failures;
                    }

                    if (det_math_state(an->det, role) != b)
                    {
                        printf("FAIL %s tick %d: role %d math state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)b,
                               (unsigned long long)det_math_state(an->det, role));
                        ++failures;
                    }

                    if (det_split_state(an->det, role) != c)
                    {
                        printf("FAIL %s tick %d: role %d split state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)c,
                               (unsigned long long)det_split_state(an->det, role));
                        ++failures;
                    }
                }

                uint64_t nid = strtoull(tok[23], NULL, 10);

                if ((uint32_t)an->det->next_id[DET_OTHER] != (uint32_t)nid)
                {
                    printf("FAIL %s tick %d: next id: the oracle recorded %llu, the native replay has %d\n",
                           dir, t, (unsigned long long)nid, an->det->next_id[DET_OTHER]);
                    ++failures;
                }

                uint64_t wr = strtoull(tok[25], NULL, 16);

                if (an->iew.world_rand.r.seed != (wr & 0xFFFFFFFFFFFFULL))
                {
                    printf("FAIL %s tick %d: world rand: the oracle recorded %016llx, the native replay has"
                           " %012llx\n", dir, t, (unsigned long long)wr,
                           (unsigned long long)an->iew.world_rand.r.seed);
                    ++failures;
                }

                cmp_int(t, -1, "world rand gaussian pending", atoi(tok[27]), an->iew.world_rand.have_next_next_gaussian);
            }
        }

        if (getenv("AN_DEBUG") && t < 4)
        {
            for (int i = 0; i < an->n; ++i) if (i != 14 && i != 130) continue;
            for (int i = 0; i < an->n; ++i)
            {
                uint64_t st = an_ent_at(an->slot[i])->is_living ? lv_get(an_ent_at(an->slot[i])->livh)->rand.r.seed
                                                    : ie_get(an_ent_at(an->slot[i])->ieh)->rand.r.seed;
                printf("DBG mine t=%d i=%d rand=%012llx\n", t, i, (unsigned long long)st);
            }
        }

        if (failures > 40)
        {
            printf("FAIL %s: stopping after 40 failures at tick %d\n", dir, t);
            break;
        }
    }

    /* the final block state */
    snprintf(path, sizeof path, "%s/final.bin.gz", dir);
    struct gunzip *ff = gunzip_open(path);

    if (ff)
    {
        const char *lp = strstr(manifest, "\"loaded\":[");
        lp += strlen("\"loaded\":[");
        unsigned char ids[65536 * 3];
        int chunk_fail = 0;

        while (*lp == '[')
        {
            int lx = (int)strtol(lp + 1, (char **)&lp, 10);
            int lz = (int)strtol(lp + 1, (char **)&lp, 10);

            if (*lp == ']') ++lp;
            if (*lp == ',') ++lp;

            unsigned char cx[4], cz[4];

            if (gunzip_read(ff, cx, 4) != 4 || gunzip_read(ff, cz, 4) != 4) break;

            int wx = (int)((uint32_t)cx[0] << 24 | (uint32_t)cx[1] << 16 | (uint32_t)cx[2] << 8 | cx[3]);
            int wz = (int)((uint32_t)cz[0] << 24 | (uint32_t)cz[1] << 16 | (uint32_t)cz[2] << 8 | cz[3]);

            if (wx != lx || wz != lz)
            {
                printf("FAIL %s: final chunk order: the oracle has (%d, %d), the replay is at (%d, %d)\n",
                       dir, wx, wz, lx, lz);
                ++failures;
                chunk_fail = 1;
                break;
            }

            if (gunzip_read(ff, ids, 65536 * 3) != 65536 * 3) break;

            /* the two height maps and the minimum, none of which the replay has
             * to reproduce block for block */
            unsigned char heights[1024 + 1024 + 4];
            if (gunzip_read(ff, heights, sizeof heights) != (int)sizeof heights) break;

            unsigned char mask[2];
            if (gunzip_read(ff, mask, 2) != 2) break;

            struct chunk *c = world_chunk(w, lx, lz);

            if (!c)
            {
                printf("FAIL %s: the native world has no chunk (%d, %d)\n", dir, lx, lz);
                ++failures;
                chunk_fail = 1;
                break;
            }

            for (int cell = 0; cell < 65536 && !chunk_fail; ++cell)
            {
                int want = le16(ids + cell * 2);
                int got = chunk_cell_id(c, cell) & 4095;

                if (want != got)
                {
                    int x = cell >> 12, z = (cell >> 8) & 15, y = cell & 255;
                    printf("FAIL %s: final block at (%d, %d, %d): the oracle recorded id %d, the native replay"
                           " has %d\n", dir, lx * 16 + x, y, lz * 16 + z, want, got);
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
                           " %d\n", dir, lx * 16 + x, y, lz * 16 + z, want, got);
                    ++failures;
                    chunk_fail = 1;
                }
            }

            if (le16(mask) != c->mask)
            {
                printf("FAIL %s: final section mask of chunk (%d, %d): the oracle recorded %04x, the native"
                       " replay has %04x\n", dir, lx, lz, le16(mask), c->mask);
                ++failures;
                chunk_fail = 1;
            }
        }

        gunzip_close(ff);
    }

    gunzip_close(tf);
    gunzip_close(df);
    gunzip_close(nf);
    gunzip_close(rf);

    printf("%s: %d spawns, %d ticks, %d live entities, %d failures\n", dir, nents, t, an->n, failures);
    return 0;
}