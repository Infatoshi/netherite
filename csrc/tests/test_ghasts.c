/* Gate: the native ghasts and their fireballs against the oracle's GhastProbe
 * dump (oracle/harness/netherite/oracle/GhastProbe.java). The probe builds a raw
 * Nether region, carves an open cavern around the probe player, spawns the
 * ghasts the way natural spawning does and ticks them the way
 * World.updateEntities does, recording every entity's state and Random, the
 * fireballs, the explosions' block writes, the player's health, the Det
 * digests and the final blocks.
 *
 * The replay repeats that from the dump alone: the region, the shapes (the
 * carve and the walls run's netherrack shell), the spawns, then one gh_tick
 * per tick with every field of every entity compared after each tick.
 *
 * A failure names the tick, the entity and the field. Negative checks:
 * --negative=attack runs the attack threshold at 15, --negative=waypoint at
 * 12.0F; each must fail naming the tick.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/gunzip.h"
#include "../engine/env.h"
#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/explosion.h"
#include "../engine/ghasts.h"
#include "../engine/nbtjson.h"
#include "../engine/projectile.h"
#include "../engine/trace.h"
#include "../engine/world.h"

#define ENT_STATE_BYTES 232
#define SPAWN_BYTES 72
#define PLAYER_STATE_BYTES 112

static int failures = 0;
static int neg_attack = 0;
static int neg_waypoint = 0;

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

static const char *g_manifest;

static int gh_replay(struct an_world *an, struct gh_world *gw, struct world *w,
                     det_state *det, int nents, int ticks, const char *dir, const char *manifest);

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "usage: test_ghasts GHASTS_DIR [TRACE_PATH|--negative=...]\n");
        return 2;
    }

    const char *dir = argv[1];
    const char *trace_path = NULL;

    if (argc == 3)
    {
        if (!strcmp(argv[2], "--negative=attack")) neg_attack = 1;
        else if (!strcmp(argv[2], "--negative=waypoint")) neg_waypoint = 1;
        else trace_path = argv[2];
    }
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

    if (!strstr(manifest, "\"kind\":\"ghasts\""))
    {
        printf("skip %s: not a ghasts dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int nents = (int)manifest_int(manifest, "ghasts");
    int ticks = (int)manifest_int(manifest, "ticks");
    int walls = strstr(manifest, "\"walls\":true") != NULL;

    struct world w;
    world_init(&w, seed);
    w.dim = -1;   /* the Nether: WorldProviderHell, hasNoSky */

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
    an.dimension = -1;
    an.iew.world_rand.r.seed = start.world_rand & 0xFFFFFFFFFFFFULL;
    an.iew.world_rand.have_next_next_gaussian = start.world_rand_gauss;
    an.iew.world_rand.next_next_gaussian = 0.0;

    /* the lane's world tag: the player and the walls flag ride it */
    static struct gh_world gw;
    memset(&gw, 0, sizeof gw);
    gw.an = &an;
    gw.has_walls = walls;
    an.user_data = &gw;
    an.iew.on_fireball_impact = gh_fireball_impact;
    nw_env->cfg.ghast_negative_attack = neg_attack;
    nw_env->cfg.ghast_negative_waypoint = neg_waypoint;

    /* the probe player: the constructor's draws in Entity's order */
    an.has_player = 1;
    struct gh_player *p = &gw.player;
    p->entity_id = det_next_entity_id_role(&det, DET_OTHER);
    p->rand = det_new_random_role(&det, DET_OTHER);
    det_uuid_role(&det, DET_OTHER, &p->uuid_msb, &p->uuid_lsb);

    /* EntityLivingBase's constructor: the two float fields and the spawn yaw,
     * all Math.random; the yaw is overwritten by setLocationAndAngles */
    (void)det_math_random_role(&det, DET_OTHER);
    (void)det_math_random_role(&det, DET_OTHER);
    (void)det_math_random_role(&det, DET_OTHER);

    /* ------------------------------------------------------------ the spawns */
    snprintf(path, sizeof path, "%s/spawns.bin", dir);
    size_t spnlen;
    unsigned char *spawns = read_file(path, &spnlen);

    if (spnlen % SPAWN_BYTES != 0)
    {
        fprintf(stderr, "%s: spawns.bin is %zu bytes\n", dir, spnlen);
        return 2;
    }

    int nspawns = (int)(spnlen / SPAWN_BYTES);

    /* the trace opens before the spawns: the spawn-time draws are part of the
     * diff */
    trace_open(trace_path);

    /* the recorded spawn NBT, for the constructor check */
    snprintf(path, sizeof path, "%s/spawns.txt.gz", dir);
    struct gunzip *sfile = gunzip_open(path);

    if (!sfile)
    {
        perror(path);
        return 2;
    }

    char line[16384];

    for (int i = 0; i < nspawns; ++i)
    {
        const unsigned char *s = spawns + (size_t)i * SPAWN_BYTES;
        int index = (int)le32(s);
        int entity_id = (int)le32(s + 4);
        int kind = (int)le32(s + 8);
        (void)kind;
        double x = le_double(s + 12), y = le_double(s + 20), z = le_double(s + 28);
        float yaw = le_float(s + 36), pitch = le_float(s + 40);
        float health = le_float(s + 44);
        int is_player = s[48];

        if (is_player)
        {
            p->entity_id = entity_id;
            p->pos_x = x; p->pos_y = y; p->pos_z = z;
            p->rotation_yaw = yaw; p->rotation_pitch = pitch;
            p->health = health;
            an.player_x = x; an.player_y = y; an.player_z = z;
            cmp_int(-1, i, "player id", entity_id, p->entity_id);
        }
        else
        {
            struct living *l = gh_spawn_ghast(&an, index, x, y, z, yaw, pitch);

            if (!l)
            {
                printf("FAIL %s: could not spawn ghast %d\n", dir, i);
                return 2;
            }

            cmp_int(-1, i, "entity id", entity_id, l->entity_id);
        }

        if (gunzip_gets(sfile, line, sizeof line))
        {
            /* "<spawn_index> <entity_id> <kind name> <health bits or canonical NBT>" */
            char *text = strchr(line, '{');   /* the NBT itself, after "si id Kind " */

            if (text)
            {
                while (*text == ' ') ++text;
                size_t n = strlen(text);

                while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = 0;

                if (!is_player)
                {
                    char *mine;
                    living_nbt_text(lv_get(an_ent_at(an.slot[an.n - 1])->livh), &mine);

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
    }

    gunzip_close(sfile);
    free(spawns);

    int rc = gh_replay(&an, &gw, &w, &det, nents, ticks, dir, manifest);

    trace_close();
    an_free(&an);
    free(manifest);
    return (failures != 0 || rc != 0) ? 1 : 0;
}

/* ------------------------------------------------------------- the replay */

static int replay(const char *dir, struct an_world *an, struct gh_world *gw, struct world *w,
                  det_state *det, int nents, int ticks);

static int gh_replay(struct an_world *an, struct gh_world *gw, struct world *w,
                     det_state *det, int nents, int ticks, const char *dir, const char *manifest)
{
    g_manifest = manifest;
    return replay(dir, an, gw, an->w, an->det, nents, ticks);
}

static int replay(const char *dir, struct an_world *an, struct gh_world *gw, struct world *w,
                  det_state *det, int nents, int ticks)
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
    unsigned char prec[PLAYER_STATE_BYTES];
    int t;

    /* the removal stream */
    int rem_tick = -1, rem_si = 0, rem_id = 0, rem_kind = 0, rem_fire = 0, removal_eof = 0;
    unsigned long long rem_health = 0;

    if (gunzip_gets(rf, line, sizeof line))
    {
        if (sscanf(line, "%d %d %d %d %llx %d", &rem_tick, &rem_si, &rem_id, &rem_kind, &rem_health, &rem_fire) != 6)
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

    for (t = 0; t < ticks; ++t)
    {
        struct gh_removal removals[64];
        int n_rem = 0;
        gh_tick(an, t, removals, 64, &n_rem);

        /* the removals in visit order: the oracle's line names the tick, the
         * spawn index, the id, the kind, the health and the fire */
        for (int i = 0; i < n_rem; ++i)
        {
            const struct gh_removal *r = &removals[i];

            if (removal_eof)
            {
                printf("FAIL %s tick %d: removal of spawn %d but the oracle's removal stream ended\n",
                       dir, t, r->si);
                ++failures;
                break;
            }

            if (rem_tick != r->tick || rem_si != r->si || rem_id != r->entity_id || rem_kind != r->kind)
            {
                printf("FAIL %s tick %d: removal: the oracle recorded (t=%d si=%d id=%d kind=%d), the native"
                       " replay removed (t=%d si=%d id=%d kind=%d)\n", dir, t,
                       rem_tick, rem_si, rem_id, rem_kind, r->tick, r->si, r->entity_id, r->kind);
                ++failures;
            }
            else
            {
                if (r->health < 0.0F)
                {
                    if (rem_health != 0xFFFFFFFFFFFFFFFFULL)
                    {
                        char w[32];
                        snprintf(w, sizeof w, "%016llx", rem_health);
                        fail_rec(t, r->si, "removal health bits", w, "ffffffffffffffff (-1)");
                    }
                }
                else
                {
                    uint32_t bits;
                    float hf = r->health;
                    memcpy(&bits, &hf, 4);
                    cmp_int(t, r->si, "removal health bits", (long long)rem_health, (long long)bits);
                }

                cmp_int(t, r->si, "removal fire", (long long)rem_fire, (long long)r->fire);
            }

            if (gunzip_gets(rf, line, sizeof line))
            {
                if (sscanf(line, "%d %d %d %d %llx %d", &rem_tick, &rem_si, &rem_id, &rem_kind, &rem_health, &rem_fire) != 6)
                    removal_eof = 1;
            }
            else removal_eof = 1;
        }

        /* the tick header (8 bytes: tick, count), the player's record, then
         * one state record per live entity in list order */
        {
            unsigned char head[8];

            if (gunzip_read(tf, head, 8) != 8)
            {
                printf("FAIL %s: no tick header at tick %d\n", dir, t);
                ++failures;
                break;
            }

            int next_tick = (int)le32(head);
            int next_count = (int)le32(head + 4);

            if (next_tick != t)
            {
                printf("FAIL %s: tick header says %d at tick %d\n", dir, next_tick, t);
                ++failures;
                break;
            }

            if (next_count != an->n)
            {
                printf("FAIL %s tick %d: the oracle recorded %d live entities, the native replay has %d\n",
                       dir, t, next_count, an->n);
                ++failures;
            }
        }

        /* the player's record */
        if (gunzip_read(tf, prec, PLAYER_STATE_BYTES) != PLAYER_STATE_BYTES)
        {
            printf("FAIL %s tick %d: the player record is missing\n", dir, t);
            ++failures;
            break;
        }

        {
            struct gh_player *p = &gw->player;
            int si = 0;

            cmp_int(t, si, "player entity id", (long long)(int32_t)le32(prec), p->entity_id);
            cmp_dbl(t, si, "player posX", le_double(prec + 4), p->pos_x);
            cmp_dbl(t, si, "player posY", le_double(prec + 12), p->pos_y);
            cmp_dbl(t, si, "player posZ", le_double(prec + 20), p->pos_z);
            cmp_dbl(t, si, "player motionX", le_double(prec + 28), p->motion_x);
            cmp_dbl(t, si, "player motionY", le_double(prec + 36), p->motion_y);
            cmp_dbl(t, si, "player motionZ", le_double(prec + 44), p->motion_z);
            cmp_flt(t, si, "player rotationYaw", le_float(prec + 52), p->rotation_yaw);
            cmp_flt(t, si, "player rotationPitch", le_float(prec + 56), p->rotation_pitch);
            cmp_flt(t, si, "player health", le_float(prec + 60), p->health);
            cmp_flt(t, si, "player prevHealth", le_float(prec + 64), p->prev_health);
            cmp_flt(t, si, "player absorption", le_float(prec + 68), p->absorption);
            cmp_int(t, si, "player hurtTime", (long long)(int32_t)le32(prec + 72), p->hurt_time);
            cmp_int(t, si, "player maxHurtTime", (long long)(int32_t)le32(prec + 76), p->max_hurt_time);
            cmp_int(t, si, "player deathTime", (long long)(int32_t)le32(prec + 80), p->death_time);
            cmp_int(t, si, "player hurtResistantTime", (long long)(int32_t)le32(prec + 84), p->hurt_resistant_time);
            cmp_flt(t, si, "player lastDamage", le_float(prec + 88), p->last_damage);
            cmp_int(t, si, "player entityAge", (long long)(int32_t)le32(prec + 92), p->entity_age);
            cmp_int(t, si, "player isDead", (long long)(int32_t)le32(prec + 96), p->is_dead);
            cmp_int(t, si, "player rand state", (long long)(le64(prec + 100) & 0xFFFFFFFFFFFFULL),
                    (long long)((uint64_t)p->rand.r.seed & 0xFFFFFFFFFFFFULL));
        }

        for (int i = 0; i < an->n; ++i)
        {
            struct an_ent *en = an_ent_at(an->slot[i]);

            int ekind = en->is_living ? 0 : (ie_get(en->ieh)->kind == IE_LARGE_FIREBALL ? 2 : (ie_get(en->ieh)->kind == IE_ITEM ? 3 : 4));
            int size = (ekind == 0) ? 204 : (ekind == 2 ? 156 : (ekind == 3 ? 120 : 104));

            if (gunzip_read(tf, rec, (unsigned)size) != size)
            {
                printf("FAIL %s tick %d: the tick records end inside the tick\n", dir, t);
                ++failures;
                break;
            }

            int si = (int)le32(rec);
            int id = (int)le32(rec + 4);
            int rkind = (int)le32(rec + 8);

            int kind_matches = (ekind == rkind);

            if (en->spawn_index != si || !kind_matches)
            {
                printf("FAIL %s tick %d: record %d is spawn %d kind %d, the native replay has spawn %d kind %d\n",
                       dir, t, i, si, rkind, en->spawn_index, ekind);
                ++failures;
                continue;
            }

            if (en->is_living)
            {
                struct living *l = lv_get(en->livh);

                cmp_int(t, si, "entity id", id, l->entity_id);

                uint64_t want_hash = le64(rec + 12);
                uint64_t got_hash = living_nbt_hash(l);

                if (want_hash != got_hash)
                {
                    char wbuf[32], gbuf[32];
                    snprintf(wbuf, sizeof wbuf, "%016llx", (unsigned long long)want_hash);
                    snprintf(gbuf, sizeof gbuf, "%016llx", (unsigned long long)got_hash);
                    fail_rec(t, si, "nbt hash", wbuf, gbuf);
                }

                cmp_dbl(t, si, "posX", le_double(rec + 20), l->e.pos_x);
                cmp_dbl(t, si, "posY", le_double(rec + 28), l->e.pos_y);
                cmp_dbl(t, si, "posZ", le_double(rec + 36), l->e.pos_z);
                cmp_dbl(t, si, "motionX", le_double(rec + 44), l->e.motion_x);
                cmp_dbl(t, si, "motionY", le_double(rec + 52), l->e.motion_y);
                cmp_dbl(t, si, "motionZ", le_double(rec + 60), l->e.motion_z);
                cmp_flt(t, si, "rotationYaw", le_float(rec + 68), l->rotation_yaw);
                cmp_flt(t, si, "rotationPitch", le_float(rec + 72), l->rotation_pitch);
                cmp_flt(t, si, "prevRotationYaw", le_float(rec + 76), l->prev_rotation_yaw);
                cmp_flt(t, si, "prevRotationPitch", le_float(rec + 80), l->prev_rotation_pitch);
                cmp_flt(t, si, "rotationYawHead", le_float(rec + 84), l->rotation_yaw_head);
                cmp_flt(t, si, "renderYawOffset", le_float(rec + 88), l->render_yaw_offset);
                cmp_flt(t, si, "health", le_float(rec + 92), l->health);
                cmp_flt(t, si, "prevHealth", le_float(rec + 96), l->prev_health);
                cmp_int(t, si, "hurtTime", (long long)(int32_t)le32(rec + 100), l->hurt_time);
                cmp_int(t, si, "deathTime", (long long)(int32_t)le32(rec + 104), l->death_time);
                cmp_int(t, si, "hurtResistantTime", (long long)(int32_t)le32(rec + 108), l->hurt_resistant_time);
                cmp_flt(t, si, "lastDamage", le_float(rec + 112), l->last_damage);
                cmp_int(t, si, "entityAge", (long long)(int32_t)le32(rec + 116), l->entity_age);
                cmp_int(t, si, "ticksExisted", (long long)(int32_t)le32(rec + 120), l->ticks_existed);
                cmp_int(t, si, "prevAttackCounter", (long long)(int32_t)le32(rec + 124), l->prev_attack_counter);
                cmp_int(t, si, "attackCounter", (long long)(int32_t)le32(rec + 128), l->attack_counter);
                cmp_int(t, si, "courseChangeCooldown", (long long)(int32_t)le32(rec + 132), l->course_change_cooldown);
                cmp_int(t, si, "aggroCooldown", (long long)(int32_t)le32(rec + 136), l->aggro_cooldown);
                cmp_dbl(t, si, "waypointX", le_double(rec + 140), l->waypoint_x);
                cmp_dbl(t, si, "waypointY", le_double(rec + 148), l->waypoint_y);
                cmp_dbl(t, si, "waypointZ", le_double(rec + 156), l->waypoint_z);
                cmp_int(t, si, "targeted", (long long)(int32_t)le32(rec + 164),
                        l->ghast_target ? 1 : 0);
                cmp_int(t, si, "dataWatcher 16", (long long)(int32_t)le32(rec + 168), l->data_watcher_16);
                cmp_flt(t, si, "limbSwing", le_float(rec + 172), l->limb_swing);
                cmp_flt(t, si, "limbSwingAmount", le_float(rec + 176), l->limb_swing_amount);
                cmp_flt(t, si, "field_110154_aX", le_float(rec + 180), l->field_110154_aX);
                cmp_flt(t, si, "field_70764_aw", le_float(rec + 184), l->field_70764_aw);
                cmp_int(t, si, "explosionStrength", (long long)(int32_t)le32(rec + 188), l->explosion_power);
                cmp_int(t, si, "rand state", (long long)(le64(rec + 192) & 0xFFFFFFFFFFFFULL),
                        (long long)((uint64_t)l->rand.r.seed & 0xFFFFFFFFFFFFULL));
                cmp_int(t, si, "flags", (long long)(int32_t)le32(rec + 200),
                        (l->added_to_chunk ? 1 : 0) | (l->e.on_ground ? 2 : 0) | (l->is_dead ? 4 : 0));
            }
            else if (ie_get(en->ieh)->kind == IE_LARGE_FIREBALL)
            {
                ie_ent *ie = ie_get(en->ieh);

                cmp_int(t, si, "entity id", id, ie->entity_id);

                uint64_t want_hash = le64(rec + 12);
                uint64_t got_hash = ghast_fireball_nbt_hash(ie);

                if (want_hash != got_hash)
                {
                    char wbuf[32], gbuf[32];
                    snprintf(wbuf, sizeof wbuf, "%016llx", (unsigned long long)want_hash);
                    snprintf(gbuf, sizeof gbuf, "%016llx", (unsigned long long)got_hash);
                    fail_rec(t, si, "nbt hash", wbuf, gbuf);
                }

                cmp_dbl(t, si, "posX", le_double(rec + 20), ie->e.pos_x);
                cmp_dbl(t, si, "posY", le_double(rec + 28), ie->e.pos_y);
                cmp_dbl(t, si, "posZ", le_double(rec + 36), ie->e.pos_z);
                cmp_dbl(t, si, "motionX", le_double(rec + 44), ie->e.motion_x);
                cmp_dbl(t, si, "motionY", le_double(rec + 52), ie->e.motion_y);
                cmp_dbl(t, si, "motionZ", le_double(rec + 60), ie->e.motion_z);
                cmp_dbl(t, si, "accelX", le_double(rec + 68), ie->accel_x);
                cmp_dbl(t, si, "accelY", le_double(rec + 76), ie->accel_y);
                cmp_dbl(t, si, "accelZ", le_double(rec + 84), ie->accel_z);
                cmp_flt(t, si, "rotationYaw", le_float(rec + 92), ie->rotation_yaw);
                cmp_flt(t, si, "rotationPitch", le_float(rec + 96), ie->rotation_pitch);
                cmp_flt(t, si, "prevRotationYaw", le_float(rec + 100), ie->prev_yaw);
                cmp_flt(t, si, "prevRotationPitch", le_float(rec + 104), ie->prev_pitch);
                cmp_int(t, si, "ticksAlive", (long long)(int32_t)le32(rec + 108), ie->ticks_alive);
                cmp_int(t, si, "ticksInAir", (long long)(int32_t)le32(rec + 112), ie->ticks_in_air);
                cmp_int(t, si, "xTile", (long long)(int32_t)le32(rec + 116), ie->tile_x);
                cmp_int(t, si, "yTile", (long long)(int32_t)le32(rec + 120), ie->tile_y);
                cmp_int(t, si, "zTile", (long long)(int32_t)le32(rec + 124), ie->tile_z);
                /* the record writes getIdFromBlock(null) as 0; the field keeps -1 for
                 * "no block" (the NBT's inTile) */
                cmp_int(t, si, "inTile", (long long)(int32_t)le32(rec + 128),
                        ie->in_tile == -1 ? 0 : ie->in_tile);
                cmp_int(t, si, "inGround", (long long)(int32_t)le32(rec + 132), ie->in_ground);
                cmp_int(t, si, "explosionPower", (long long)(int32_t)le32(rec + 136), ie->explosion_power);
                cmp_int(t, si, "fire", (long long)(int32_t)le32(rec + 140), ie->e.fire);
                cmp_int(t, si, "rand state", (long long)(le64(rec + 144) & 0xFFFFFFFFFFFFULL),
                        (long long)((uint64_t)ie->rand.r.seed & 0xFFFFFFFFFFFFULL));
                cmp_int(t, si, "flags", (long long)(int32_t)le32(rec + 152),
                        (ie->added_to_chunk ? 1 : 0) | (ie->e.on_ground ? 2 : 0) | (ie->is_dead ? 4 : 0));
            }
            else if (ie_get(en->ieh)->kind == IE_ITEM)
            {
                ie_ent *ie = ie_get(en->ieh);

                cmp_int(t, si, "entity id", id, ie->entity_id);

                uint64_t want_hash = le64(rec + 12);
                uint64_t got_hash = item_entity_nbt_hash(ie);

                if (want_hash != got_hash)
                {
                    char wbuf[32], gbuf[32];
                    snprintf(wbuf, sizeof wbuf, "%016llx", (unsigned long long)want_hash);
                    snprintf(gbuf, sizeof gbuf, "%016llx", (unsigned long long)got_hash);
                    fail_rec(t, si, "nbt hash", wbuf, gbuf);
                }

                cmp_dbl(t, si, "posX", le_double(rec + 20), ie->e.pos_x);
                cmp_dbl(t, si, "posY", le_double(rec + 28), ie->e.pos_y);
                cmp_dbl(t, si, "posZ", le_double(rec + 36), ie->e.pos_z);
                cmp_dbl(t, si, "motionX", le_double(rec + 44), ie->e.motion_x);
                cmp_dbl(t, si, "motionY", le_double(rec + 52), ie->e.motion_y);
                cmp_dbl(t, si, "motionZ", le_double(rec + 60), ie->e.motion_z);
                cmp_flt(t, si, "rotationYaw", le_float(rec + 68), ie->rotation_yaw);
                cmp_flt(t, si, "rotationPitch", le_float(rec + 72), ie->rotation_pitch);
                cmp_int(t, si, "health", (long long)(int32_t)le32(rec + 76), ie->health);
                cmp_int(t, si, "age", (long long)(int32_t)le32(rec + 80), ie->age);
                cmp_int(t, si, "delayBeforeCanPickup", (long long)(int32_t)le32(rec + 84), ie->delay);
                cmp_int(t, si, "fire", (long long)(int32_t)le32(rec + 88), ie->e.fire);
                cmp_flt(t, si, "hoverStart", le_float(rec + 92), ie->hover_start);
                cmp_int(t, si, "item", (long long)(int32_t)le32(rec + 96), ie->stack_item);
                cmp_int(t, si, "damage", (long long)(int32_t)le32(rec + 100), ie->stack_damage);
                cmp_int(t, si, "count", (long long)(int32_t)le32(rec + 104), ie->stack_count);
                cmp_int(t, si, "rand state", (long long)(le64(rec + 108) & 0xFFFFFFFFFFFFULL),
                        (long long)((uint64_t)ie->rand.r.seed & 0xFFFFFFFFFFFFULL));
                cmp_int(t, si, "flags", (long long)(int32_t)le32(rec + 116),
                        (ie->added_to_chunk ? 1 : 0) | (ie->e.on_ground ? 2 : 0) | (ie->is_dead ? 4 : 0));
            }

            if (failures > 40)
            {
                printf("FAIL %s: stopping after 40 failures at tick %d\n", dir, t);
                goto out;
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
                else if (ie_get(an_ent_at(an->slot[i])->ieh)->kind == IE_LARGE_FIREBALL) ghast_fireball_nbt_text(ie_get(an_ent_at(an->slot[i])->ieh), &mine);
                else item_entity_nbt_text(ie_get(an_ent_at(an->slot[i])->ieh), &mine);

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
                        printf("FAIL %s tick %d entity %d: the NBT texts differ\n", dir, t,
                               an_ent_at(an->slot[i])->spawn_index);
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
            char *tok_p = strtok(line, " ");

            while (tok_p && n < 40)
            {
                tok[n++] = tok_p;
                tok_p = strtok(NULL, " ");
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

                    if (det_seeder_state(det, role) != a)
                    {
                        printf("FAIL %s tick %d: role %d seeder state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)a,
                               (unsigned long long)det_seeder_state(det, role));
                        ++failures;
                    }

                    if (det_math_state(det, role) != b)
                    {
                        printf("FAIL %s tick %d: role %d math state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)b,
                               (unsigned long long)det_math_state(det, role));
                        ++failures;
                    }

                    if (det_split_state(det, role) != c)
                    {
                        printf("FAIL %s tick %d: role %d split state: the oracle recorded %016llx, the native"
                               " replay has %016llx\n", dir, t, role, (unsigned long long)c,
                               (unsigned long long)det_split_state(det, role));
                        ++failures;
                    }
                }

                uint64_t nid = strtoull(tok[23], NULL, 10);

                if ((uint32_t)det->next_id[DET_OTHER] != (uint32_t)nid)
                {
                    printf("FAIL %s tick %d: next id: the oracle recorded %llu, the native replay has %d\n",
                           dir, t, (unsigned long long)nid, det->next_id[DET_OTHER]);
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

                cmp_int(t, -1, "world rand gaussian pending", atoi(tok[27]),
                        an->iew.world_rand.have_next_next_gaussian);
            }
        }

        if (failures > 40)
        {
            printf("FAIL %s: stopping after 40 failures at tick %d\n", dir, t);
            break;
        }
    }

out:
    /* the final block state */
    snprintf(path, sizeof path, "%s/final.bin.gz", dir);
    struct gunzip *ff = gunzip_open(path);

    if (ff)
    {
        const char *fp = strstr(g_manifest, "\"loaded\":[");
        fp += strlen("\"loaded\":[");
        unsigned char ids[65536 * 3];
        int chunk_fail = 0;

        while (*fp == '[')
        {
            int lx = (int)strtol(fp + 1, (char **)&fp, 10);
            int lz = (int)strtol(fp + 1, (char **)&fp, 10);

            if (*fp == ']') ++fp;
            if (*fp == ',') ++fp;

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
