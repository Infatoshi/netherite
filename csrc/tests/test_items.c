/* Gate: native item entities and XP orbs against the oracle's EntityProbe
 * dump (oracle/harness/netherite/oracle/EntityProbe.java). The probe loads a raw
 * region, scatters shapes, spawns entities from Random(opseed) and Det's OTHER
 * streams, ticks them 1200 times the way World.updateEntities does and records
 * every live entity's state after each tick.
 *
 * The replay repeats that from the dump alone: the manifest's region and
 * seeds, the shapes, the spawns (positions, stacks, motion, the Det state from
 * start.txt), then one ie_tick per tick with the recorded state compared
 * after every tick: position, motion, onGround, age, delay, fire, the stack,
 * the removals and the Det digests.
 *
 * A failure names the tick, the spawn index and the field. */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/gunzip.h"
#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/items.h"
#include "../engine/entity.h"
#include "../engine/item_entity.h"
#include "../engine/world.h"

#define SPAWN_BYTES 90
#define TICK_BYTES 95

static int failures = 0;
static int spawned_entities = 0;

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

/* The manifest is one JSON line; read a single integer or string field. */
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

/* The DetProbe snapshot format: the counters, the per-role states and the
 * registered split streams. */
struct splitref {
    char name[DET_NAME_MAX];
    uint64_t state[DET_ROLES];
    uint8_t used[DET_ROLES];
};

struct ref {
    int64_t reset_seed, world_seed;
    int32_t next_id[DET_ROLES];
    uint64_t seeder[DET_ROLES], math[DET_ROLES], split[DET_ROLES];
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
        unsigned long long a, b, c;

        if (sscanf(line, "resetSeed %lld", &v) == 1) { r->reset_seed = v; continue; }
        if (sscanf(line, "worldSeed %lld", &v) == 1) { r->world_seed = v; continue; }
        if (sscanf(line, "nextId %d %d %d %d", &r->next_id[0], &r->next_id[1], &r->next_id[2], &r->next_id[3]) == 4) continue;
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

static void fail_rec(int tick, int spawn_index, const char *field, const char *want, const char *got)
{
    printf("FAIL tick %d entity %d: %s: the oracle recorded %s, the native replay has %s\n",
           tick, spawn_index, field, want, got);
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

    char w[64], g[64];
    uint32_t wb, gb;
    memcpy(&wb, &want, 4);
    memcpy(&gb, &got, sizeof gb);
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

/* One spawn record, the fixed 90 bytes. */
struct spawn_rec {
    int index, entity_id, kind;
    double x, y, z, mx, my, mz;
    float yaw, hover;
    int item, damage, count, xp, age, delay;
};

/* One recorded tick state, the fixed 95 bytes. */
struct tick_rec {
    int tick, spawn_index, entity_id;
    double x, y, z, mx, my, mz;
    float yaw;
    int age, delay, fire, ticks_existed;
    uint8_t on_ground, collided_h, collided_v, no_clip, water_bb, lava_bb, velocity_changed;
    int short1, short2;
};

/* The probe's item id list: every id with a registered item, ascending. The
 * group stack's subtype draw depends on the picked item's flag, so the replay
 * rebuilds the same list from the registry. */
/* forward declarations, the tick pass sits behind main */
static int replay_ticks(const char *dir, ie_world *iew, det_state *det, int nents, int ticks);

static int probe_item_id(int rank)
{
    int seen = 0;

    for (int id = 0; id <= 32000; ++id)
    {
        if (!ITEMS[id].exists) continue;
        if (seen == rank) return id;
        ++seen;
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_items ITEMS_DIR\n");
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

    if (!strstr(manifest, "\"kind\":\"items\""))
    {
        printf("skip %s: not an items dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int radius = (int)manifest_int(manifest, "radius");
    int ring = (int)manifest_int(manifest, "ring");
    int nents = (int)manifest_int(manifest, "entities");
    int nshapes = (int)manifest_int(manifest, "shapes");
    int ticks = (int)manifest_int(manifest, "ticks");
    long long opseed = manifest_int(manifest, "opseed");

    /* the loaded chunks, in the oracle's cx-major order */
    const char *lp = strstr(manifest, "\"loaded\":[");
    if (!lp) { fprintf(stderr, "%s: no loaded list\n", dir); return 2; }
    lp += strlen("\"loaded\":[");

    struct world w;
    world_init(&w, seed);

    while (*lp == '[')
    {
        int lx = (int)strtol(lp + 1, (char **)&lp, 10);
        int lz = (int)strtol(lp + 1, (char **)&lp, 10);
        world_load_chunk(&w, lx, lz);

        if (*lp == ']') ++lp;
        if (*lp == ',') ++lp;
    }

    int width = (2 * radius + 1) * 16;
    int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;

    /* the shapes, placed as the probe placed them: the y and the block come
     * from the record, the draws replayed only to keep the stream aligned
     * with the spawn draws that follow it */
    snprintf(path, sizeof path, "%s/shapes.bin", dir);
    size_t slen;
    unsigned char *shapes = read_file(path, &slen);

    if (slen % 16 != 0)
    {
        fprintf(stderr, "%s: shapes.bin is %zu bytes\n", dir, slen);
        return 2;
    }

    /* the probe's draw stream, from the manifest's documented order */
    jrand r;
    jr_seed(&r, opseed);

    static const int SHAPES[32][3] = {
        {44, 0, 1}, {44, 8, 1}, {53, 0, 8}, {85, 0, 0}, {102, 0, 0}, {139, 0, 0},
        {78, 0, 8}, {81, 0, 0}, {88, 0, 0}, {60, 0, 0}, {65, 3, 1}, {65, 4, 1},
        {106, 0, 0}, {30, 0, 0}, {171, 0, 0}, {96, 0, 16}, {107, 0, 8}, {26, 0, 12},
        {92, 0, 0}, {20, 0, 0}, {1, 0, 0}, {9, 0, 0}, {11, 0, 0}, {106, 7, 1},
        {106, 8, 1}, {19, 0, 0}, {111, 0, 0}, {11, 2, 1}, {0, 0, 0}, {78, 0, 1},
        {92, 0, 0}, {79, 0, 0},
    };

    /* the item count is inside the item_ids text: "... ascending, N ids; ..."
     * (the manifest is one line, so the count is the last number before "ids") */
    int nItems = 0;
    {
        const char *ip = strstr(manifest, "ascending, ");
        if (ip) nItems = atoi(ip + strlen("ascending, "));
    }
    if (nItems <= 0) { fprintf(stderr, "%s: no item count in the manifest\n", dir); return 2; }
    (void)ring;

    for (int i = 0; i < nshapes; ++i)
    {
        const unsigned char *s = shapes + i * 16;
        int x = (int)le32(s), y = (int)le32(s + 4), z = (int)le32(s + 8);
        int id = le16(s + 12), meta = s[14];

        /* the documented shape draws, replayed to keep the stream aligned */
        int dx = bx0 + jr_int_n(&r, width);
        int dz = bz0 + jr_int_n(&r, width);
        (void)jr_int_n(&r, 46); /* yDrawn */
        int pick = jr_int_n(&r, 32);
        int row_meta = SHAPES[pick][1];
        if (SHAPES[pick][2] > 0) row_meta += jr_int_n(&r, SHAPES[pick][2]);

        if (dx != x || dz != z || SHAPES[pick][0] != id || row_meta != meta)
        {
            printf("FAIL %s shape %d: the draw stream gives (%d,%d) id %d meta %d, shapes.bin has (%d,%d)"
                   " id %d meta %d\n", dir, i, dx, dz, SHAPES[pick][0], row_meta, x, z, id, meta);
            ++failures;
            break;
        }

        world_set_block(&w, x, y, z, id, meta, 2);
    }

    free(shapes);

    /* ------------------------------------------------------------- Det state */
    struct ref start;
    read_ref(&start, dir, "start.txt");

    det_state det;
    det_init(&det);
    det_load(&det, start.world_seed, start.seeder, start.math, start.next_id);

    for (int i = 0; i < start.nsplits; ++i)
    {
        det_split_add(&det, start.splits[i].name, start.splits[i].state, start.splits[i].used);
    }

    /* ------------------------------------------------------------ the spawns */
    snprintf(path, sizeof path, "%s/spawns.bin", dir);
    size_t spnlen;
    unsigned char *spawns = read_file(path, &spnlen);

    if (slen % 16 != 0 || spnlen % SPAWN_BYTES != 0)
    {
        fprintf(stderr, "%s: spawns.bin is %zu bytes\n", dir, spnlen);
        return 2;
    }

    ie_world iew;
    ie_init(&iew, &w, &det);

    /* the liquid column lists, scanned the way the probe scans them after the
     * shapes are placed (x-major over the ops area, the height-map cell's
     * block id) */
    int *water_cols = malloc(sizeof(int) * (size_t)(width * width / 2 + 2));
    int *lava_cols = malloc(sizeof(int) * (size_t)(width * width / 2 + 2));
    int nwater = 0, nlava = 0;

    for (int x = bx0; x < bx0 + width; ++x)
    {
        for (int z = bz0; z < bz0 + width; ++z)
        {
            int y = world_get_height_value(&w, x, z);
            int id = world_get_block(&w, x, y, z) & 4095;

            if (id == 9 || id == 8)
            {
                water_cols[nwater++] = x;
                water_cols[nwater++] = z;
            }
            else if (id == 11 || id == 10)
            {
                lava_cols[nlava++] = x;
                lava_cols[nlava++] = z;
            }
        }
    }

    struct spawn_rec *spawn_recs = calloc((size_t)nents, sizeof *spawn_recs);

    for (int i = 0; i < nents; ++i)
    {
        const unsigned char *s = spawns + (size_t)i * SPAWN_BYTES;
        struct spawn_rec *sr = &spawn_recs[i];
        sr->index = (int)le32(s);
        sr->entity_id = (int)le32(s + 4);
        sr->kind = s[8];
        sr->x = le_double(s + 9);
        sr->y = le_double(s + 17);
        sr->z = le_double(s + 25);
        sr->mx = le_double(s + 33);
        sr->my = le_double(s + 41);
        sr->mz = le_double(s + 49);
        sr->yaw = le_float(s + 57);
        sr->hover = le_float(s + 61);
        sr->item = (int)le32(s + 65);
        sr->damage = (int)le32(s + 69);
        sr->count = (int)le32(s + 73);
        sr->xp = (int)le32(s + 77);
        sr->age = (int)le32(s + 81);
        sr->delay = (int)le32(s + 85);

        /* the probe's draws for this entity, in the manifest's documented
         * order; the values are not needed here, only spent */
        if (i % 6 == 0)
        {
            jr_int_n(&r, width); /* ax */
            jr_int_n(&r, width); /* az */
            int gpick = jr_int_n(&r, nItems);
            /* the group stack's subtype draw, by the picked item's flag; the
             * probe picks from its own ascending id list, and the native
             * registry's existing items are the same ids */
            int gid = probe_item_id(gpick);
            if (ITEMS[gid].has_subtypes) jr_int_n(&r, 16);
        }

        int liquid_drop = (i % 17 == 8 && i >= 6);
        int want_water = (i % 34 == 8);

        if (liquid_drop)
        {
            int n = (want_water ? nwater : nlava) / 2;

            if (n > 0)
            {
                jr_int_n(&r, n);
            }
            else
            {
                liquid_drop = 0;
                jr_int_n(&r, 3);
                jr_int_n(&r, 3);
                jr_int_n(&r, 7);
            }
        }
        else
        {
            jr_int_n(&r, 3);
            jr_int_n(&r, 3);
            jr_int_n(&r, 7);
        }

        if (sr->kind == 1)
        {
            jr_int_n(&r, 5); /* count */
            if (i % 5 == 3) jr_int_n(&r, 10); /* delayBeforeCanPickup */
            if (i % 13 == 5) jr_int_n(&r, 1200); /* age */
        }
        else
        {
            jr_int_n(&r, 250); /* xp */
            if (i % 13 == 5) jr_int_n(&r, 1200); /* xpOrbAge */
        }

        /* the probe's motion override, drawn from the shared opseed stream */
        double mx, my, mz;

        if (sr->kind == 1 && i % 6 != 0 && !liquid_drop)
        {
            mx = (jr_double(&r) - 0.5) * 0.05;
            my = jr_double(&r) * 0.02;
            mz = (jr_double(&r) - 0.5) * 0.05;
        }
        else
        {
            mx = (jr_double(&r) - 0.5) * 0.3;
            my = jr_double(&r) * 0.2;
            mz = (jr_double(&r) - 0.5) * 0.3;
        }

        if (mx != sr->mx || my != sr->my || mz != sr->mz)
        {
            printf("FAIL %s spawn %d: motion override: the draw stream gives (%a, %a, %a), the record has (%a, %a, %a)\n",
                   dir, i, mx, my, mz, sr->mx, sr->my, sr->mz);
            ++failures;
            break;
        }

        ie_ent *en = sr->kind == 2
            ? ie_spawn_orb(&iew, sr->x, sr->y, sr->z, sr->xp)
            : ie_spawn_item(&iew, sr->x, sr->y, sr->z, sr->item, sr->damage, sr->count);

        if (!en)
        {
            printf("FAIL %s: entity pool full at %d\n", dir, i);
            return 2;
        }

        en->spawn_index = sr->index;

        if (en->entity_id != sr->entity_id)
        {
            printf("FAIL %s spawn %d: entity id: the oracle recorded %d, the native replay has %d\n",
                   dir, i, sr->entity_id, en->entity_id);
            ++failures;
        }

        cmp_flt(-1, i, "rotationYaw", sr->yaw, en->rotation_yaw);
        if (sr->kind == 1) cmp_flt(-1, i, "hoverStart", sr->hover, en->hover_start);

        en->e.motion_x = sr->mx;
        en->e.motion_y = sr->my;
        en->e.motion_z = sr->mz;
        en->age = sr->age;
        en->delay = sr->delay;

        ie_added_to_world(&iew, en);
        ++spawned_entities;
    }

    free(water_cols);
    free(lava_cols);
    free(spawn_recs);
    free(spawns);

    replay_ticks(dir, &iew, &det, nents, ticks);
    ie_free(&iew);

    printf("%s: %d spawns, %d ticks, %d failures\n", dir, nents, ticks, failures);
    return failures != 0;
}

/* World.isMaterialInBB for one material, the read-only scan the probe records
 * after the tick (the water flag's box, the lava flag's shrunk box). */
static int bb_material(struct world *w, struct aabb box, int material)
{
    int x0 = (int)floor(box.min_x);
    int x1 = (int)floor(box.max_x + 1.0);
    int y0 = (int)floor(box.min_y);
    int y1 = (int)floor(box.max_y + 1.0);
    int z0 = (int)floor(box.min_z);
    int z1 = (int)floor(box.max_z + 1.0);

    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (BLOCKS[world_get_block(w, x, y, z) & 4095].material == material) return 1;

    return 0;
}


/* The tick pass: one ie_tick per recorded tick, the removals, the live
 * entities' recorded states and the Det digest line compared after each. */
static int replay_ticks(const char *dir, ie_world *iew, det_state *det, int nents, int ticks)
{
    char path[1200];

    snprintf(path, sizeof path, "%s/ticks.bin.gz", dir);
    struct gunzip *tf = gunzip_open(path);
    if (!tf)
    {
        perror(path);
        exit(2);
    }

    snprintf(path, sizeof path, "%s/removals.txt.gz", dir);
    struct gunzip *rf = gunzip_open(path);
    if (!rf)
    {
        perror(path);
        exit(2);
    }

    snprintf(path, sizeof path, "%s/digest.txt.gz", dir);
    struct gunzip *df = gunzip_open(path);
    if (!df)
    {
        perror(path);
        exit(2);
    }

    char line[1024];
    int next_tick = -1, next_si = 0, next_id = 0, next_reason = 0, next_health = 0, next_fire = 0, next_age = 0;
    double next_y = 0.0;
    int removal_eof = 0;

    if (gunzip_gets(rf, line, sizeof line))
    {
        char yb[32];
        if (sscanf(line, "%d %d %d %d %d %d %d %31s", &next_tick, &next_si, &next_id,
                   &next_reason, &next_health, &next_fire, &next_age, yb) == 8)
        {
            uint64_t bits = strtoull(yb, NULL, 16);
            memcpy(&next_y, &bits, 8);
        }
        else removal_eof = 1;
    }
    else removal_eof = 1;

    unsigned char recbuf[TICK_BYTES];
    unsigned char pending_rec[TICK_BYTES];
    int pending = 0;
    int t;

    for (t = 0; t < ticks; ++t)
    {
        ie_removal removals[IE_MAX_ENTITIES];
        int n_removals = 0;
        ie_tick(iew, t, removals, IE_MAX_ENTITIES, &n_removals);

        for (int i = 0; i < n_removals; ++i)
        {
            const ie_removal *rv = &removals[i];

            if (removal_eof || rv->tick != next_tick || rv->spawn_index != next_si
                || rv->entity_id != next_id)
            {
                printf("FAIL %s tick %d: removal: the oracle has (tick %d entity %d), the native replay has"
                       " (tick %d entity %d spawn %d)\n", dir, t, removal_eof ? -1 : next_tick,
                       removal_eof ? -1 : next_id, rv->tick, rv->entity_id, rv->spawn_index);
                ++failures;
            }
            else
            {
                cmp_int(t, rv->spawn_index, "removal reason", next_reason, rv->reason);
                cmp_int(t, rv->spawn_index, "removal health", next_health, rv->health);
                cmp_int(t, rv->spawn_index, "removal fire", next_fire, rv->fire);
                cmp_int(t, rv->spawn_index, "removal age", next_age, rv->age);
                cmp_dbl(t, rv->spawn_index, "removal posY", next_y, rv->pos_y);
            }

            if (gunzip_gets(rf, line, sizeof line))
            {
                char yb[32];
                if (sscanf(line, "%d %d %d %d %d %d %d %31s", &next_tick, &next_si, &next_id,
                           &next_reason, &next_health, &next_fire, &next_age, yb) == 8)
                {
                    uint64_t bits = strtoull(yb, NULL, 16);
                    memcpy(&next_y, &bits, 8);
                }
                else removal_eof = 1;
            }
            else removal_eof = 1;
        }

        int done = 0;

        while (!done)
        {
            int got;

            if (pending)
            {
                memcpy(recbuf, pending_rec, TICK_BYTES);
                got = TICK_BYTES;
                pending = 0;
            }
            else
            {
                got = gunzip_read(tf, recbuf, TICK_BYTES);
            }

            if (got == 0)
            {
                /* the stream ends with the last tick's last record */
                if (t != ticks - 1)
                {
                    printf("FAIL %s: tick records end before tick %d\n", dir, t);
                    ++failures;
                }

                break;
            }

            if (got != TICK_BYTES)
            {
                printf("FAIL %s: tick record %d bytes inside tick %d\n", dir, got, t);
                ++failures;
                break;
            }

            int rt = (int)le32(recbuf);

            if (rt > t)
            {
                /* the next tick's first record: hold it and end this tick */
                memcpy(pending_rec, recbuf, TICK_BYTES);
                pending = 1;
                break;
            }
            int rsi = (int)le32(recbuf + 4);
            int rid = (int)le32(recbuf + 8);

            if (rt < t)
            {
                printf("FAIL %s: tick records out of order: %d inside tick %d\n", dir, rt, t);
                ++failures;
                continue;
            }

            ie_ent *en = NULL;

            for (int i = 0; i < iew->n; ++i)
            {
                if (ie_ent_at(iew->slot[i])->spawn_index == rsi)
                {
                    en = ie_ent_at(iew->slot[i]);
                    break;
                }
            }

            if (!en)
            {
                printf("FAIL %s tick %d: record for spawn %d the replay does not hold\n", dir, t, rsi);
                ++failures;
                continue;
            }

            cmp_int(t, rsi, "entity id", rid, en->entity_id);
            cmp_dbl(t, rsi, "x", le_double(recbuf + 12), en->e.pos_x);
            cmp_dbl(t, rsi, "y", le_double(recbuf + 20), en->e.pos_y);
            cmp_dbl(t, rsi, "z", le_double(recbuf + 28), en->e.pos_z);
            cmp_dbl(t, rsi, "motionX", le_double(recbuf + 36), en->e.motion_x);
            cmp_dbl(t, rsi, "motionY", le_double(recbuf + 44), en->e.motion_y);
            cmp_dbl(t, rsi, "motionZ", le_double(recbuf + 52), en->e.motion_z);
            cmp_flt(t, rsi, "yaw", le_float(recbuf + 60), en->rotation_yaw);
            cmp_int(t, rsi, "age", (long long)(int32_t)le32(recbuf + 64), en->age);
            cmp_int(t, rsi, "delay", (long long)(int32_t)le32(recbuf + 68), en->delay);
            cmp_int(t, rsi, "fire", (long long)(int32_t)le32(recbuf + 72), en->e.fire);
            cmp_int(t, rsi, "ticksExisted", (long long)(int32_t)le32(recbuf + 76), en->ticks_existed);
            cmp_int(t, rsi, "onGround", (long long)recbuf[80], en->e.on_ground);
            cmp_int(t, rsi, "isCollidedHorizontally", (long long)recbuf[81], en->e.is_collided_horizontally);
            cmp_int(t, rsi, "isCollidedVertically", (long long)recbuf[82], en->e.is_collided_vertically);
            cmp_int(t, rsi, "noClip", (long long)recbuf[83], en->e.no_clip);

            struct aabb lava_box = aabb_expand(en->e.bounding_box,
                                               -0.10000000149011612, -0.4000000059604645, -0.10000000149011612);
            cmp_int(t, rsi, "waterBB", (long long)recbuf[84],
                    bb_material(iew->w, en->e.bounding_box, 6));
            cmp_int(t, rsi, "lavaBB", (long long)recbuf[85], bb_material(iew->w, lava_box, 7));
            cmp_int(t, rsi, "velocityChanged", (long long)recbuf[86], en->e.velocity_changed);
            cmp_int(t, rsi, "short1", (long long)le16(recbuf + 87),
                    en->kind == IE_ITEM ? en->stack_damage : en->xp_color);
            cmp_int(t, rsi, "short2", (long long)le16(recbuf + 89),
                    en->kind == IE_ITEM ? en->stack_count : 0);
        }

        /* the digest line: t <t>, four role groups, nextId <n> */
        if (!gunzip_gets(df, line, sizeof line))
        {
            printf("FAIL %s: digest line missing at tick %d\n", dir, t);
            ++failures;
            break;
        }

        {
            char *tok[32];
            int n = 0;
            char *p = strtok(line, " ");

            while (p && n < 32)
            {
                tok[n++] = p;
                p = strtok(NULL, " ");
            }

            /* t <t> then per role: role <r> <seeder> <math> <split>, then nextId <n>: 2 + 4*5 + 2 tokens */
            if (n != 24 || strcmp(tok[0], "t") != 0 || atoi(tok[1]) != t)
            {
                printf("FAIL %s tick %d: digest line has %d tokens, expected 24 for tick %d\n", dir, t, n, t);
                ++failures;
            }
            else
            {
                for (int role_i = 0; role_i < DET_ROLES; ++role_i)
                {
                    uint64_t a = strtoull(tok[4 + role_i * 5], NULL, 16);
                    uint64_t b = strtoull(tok[5 + role_i * 5], NULL, 16);
                    uint64_t c = strtoull(tok[6 + role_i * 5], NULL, 16);

                    if (det_seeder_state(det, role_i) != a)
                    {
                        printf("FAIL %s tick %d: role %d seeder state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role_i,
                               (unsigned long long)a, (unsigned long long)det_seeder_state(det, role_i));
                        ++failures;
                    }

                    if (det_math_state(det, role_i) != b)
                    {
                        printf("FAIL %s tick %d: role %d math state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role_i,
                               (unsigned long long)b, (unsigned long long)det_math_state(det, role_i));
                        ++failures;
                    }

                    if (det_split_state(det, role_i) != c)
                    {
                        printf("FAIL %s tick %d: role %d split state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role_i,
                               (unsigned long long)c, (unsigned long long)det_split_state(det, role_i));
                        ++failures;
                    }
                }

                uint64_t nid = strtoull(tok[23], NULL, 10);
                int32_t native_next = iew->det->next_id[DET_OTHER];

                if ((uint32_t)native_next != (uint32_t)nid)
                {
                    printf("FAIL %s tick %d: next id: the oracle recorded %llu, the native replay has %d\n",
                           dir, t, (unsigned long long)nid, native_next);
                    ++failures;
                }
            }
        }
    }

    gunzip_close(tf);
    gunzip_close(rf);
    gunzip_close(df);
    return t;
}
