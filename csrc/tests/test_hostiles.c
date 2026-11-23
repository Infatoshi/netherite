/* Gate: the native hostile entities and their AI against the oracle's HostileProbe
 * dump (oracle/harness/netherite/oracle/HostileProbe.java).
 */
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
#include "../engine/hostiles.h"
#include "../engine/hostiles_pigman.h"
#include "../engine/living.h"
#include "../engine/nbtjson.h"
#include "../engine/trace.h"
#include "../engine/world.h"

#define SPAWN_BYTES 128

/* The base record every living writes, and the buffer a dump's record is read
 * into. A dump recorded before the enderman lane carries 272 + 332 bytes; the
 * manifest's "ent_state_layout" names the size, and only a record as long as
 * this build's hostile record carries the enderman block at offset 604. */
#define ENT_STATE_BYTES 272
#define ENDERMAN_BLOCK_OFF (ENT_STATE_BYTES + HOSTILE_SHARED_EXTRA_BYTES)
#define REC_BUF_BYTES (HOSTILE_STATE_BYTES + 256)

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

/** A manifest key that older dumps do not carry is 0. */
static int manifest_flag(const char *json, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);

    if (p == NULL) return 0;

    return (int)strtoll(p + strlen(pat), NULL, 10);
}

/** The oracle's raw kind index (HostileProbe.KINDS) as the native kind. The two
 * enumerations part after the spider: the oracle's Player is 4, its Enderman 5
 * and its Witch 6, while the native kinds put the ghast, squid and bat (15-17)
 * between them, so the enderman is 18 and the witch 19. */
static int native_kind(int raw)
{
    switch (raw)
    {
        case 0: return HK_ZOMBIE;
        case 1: return HK_SKELETON;
        case 2: return HK_CREEPER;
        case 3: return HK_SPIDER;
        case 4: return HK_PLAYER;
        case 5: return HK_ENDERMAN;
case 6: return HK_WITCH;
        case 7: return HK_SILVERFISH;
        case 8: return HK_PIGMAN;
        case 9: return HK_BLAZE;
        case 10: return HK_CAVE_SPIDER;
    }

    printf("FAIL: the dump spawns an unknown kind %d\n", raw);
    exit(2);
}

/** The record size the dump declares in "ent_state_layout", or the pre-enderman
 * 272 + 332 when the key is missing. */
static int manifest_record_bytes(const char *json)
{
    const char *key = "\"ent_state_layout\":\"";
    const char *p = strstr(json, key);

    if (p == NULL) return ENT_STATE_BYTES + HOSTILE_SHARED_EXTRA_BYTES;

    return (int)strtol(p + strlen(key), NULL, 10);
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

static int replay(const char *dir, struct an_world *an, struct world *w, int nents, int ticks, const char *manifest, int night);

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "usage: test_hostiles HOSTILES_DIR [TRACE_PATH]\n");
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

    if (!strstr(manifest, "\"kind\":\"hostiles\""))
    {
        printf("skip %s: not a hostiles dump\n", dir);
        free(manifest);
        return 0;
    }

    if (!strstr(manifest, "\"kinds_filter\":\"\\\"zombie\\\"\"") &&
        !strstr(manifest, "\"kinds_filter\":\"\\\"spider,cavespider\\\"\"") &&
        !strstr(manifest, "\"kinds_filter\":\"\\\"skeleton\\\"\"") &&
        !strstr(manifest, "\"kinds_filter\":\"\\\"creeper\\\"\"") &&
        !strstr(manifest, "\"kinds_filter\":\"\\\"enderman\\\"\"") &&
        !strstr(manifest, "\"kinds_filter\":\"\\\"witch\\\"\"") &&
        !strstr(manifest, "\"kinds_filter\":\"\\\"pigman\\\"\"") &&
        !strstr(manifest, "\"kinds_filter\":\"\\\"silverfish\\\"\"") &&
        !strstr(manifest, "\"kinds_filter\":\"\\\"blaze\\\"\""))
    {
        printf("FAIL (not a supported single-hostile filter): %s\n", dir);
        free(manifest);
        return 2;
    }

    int rec_bytes = manifest_record_bytes(manifest);

    if (rec_bytes < ENT_STATE_BYTES || rec_bytes > REC_BUF_BYTES)
    {
        printf("FAIL %s: the dump declares a %d byte record\n", dir, rec_bytes);
        free(manifest);
        return 2;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int nents = (int)manifest_int(manifest, "mobs");
    int ticks = (int)manifest_int(manifest, "ticks");
    int night = (int)manifest_int(manifest, "night");

    struct world w;
    world_init(&w, seed);
    w.dim = manifest_flag(manifest, "dim");

    const char *lp = strstr(manifest, "\"loaded\":[");
    if (!lp) { fprintf(stderr, "%s: no loaded list\n", dir); free(manifest); return 2; }
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
        free(shapes);
        free(manifest);
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
    an.dimension = w.dim;
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
        free(spawns);
        free(manifest);
        return 2;
    }

    int nspawns = (int)(spnlen / SPAWN_BYTES);

    trace_open(argc == 3 ? argv[2] : NULL);

    snprintf(path, sizeof path, "%s/spawns.txt.gz", dir);
    struct gunzip *sfile = gunzip_open(path);

    if (!sfile)
    {
        perror(path);
        free(spawns);
        free(manifest);
        return 2;
    }

    char line[8192];
    struct an_ent *construct = NULL;

    for (int i = 0; i < nspawns; ++i)
    {
        const unsigned char *s = spawns + (size_t)i * SPAWN_BYTES;
        int index = (int)le32(s);
        int entity_id = (int)le32(s + 4);
        int raw_kind = (int)le32(s + 8);
        double x = le_double(s + 12), y = le_double(s + 20), z = le_double(s + 28);
        float yaw = le_float(s + 36), pitch = le_float(s + 40);
        double mx = le_double(s + 44), my = le_double(s + 52), mz = le_double(s + 60);
        int child = (int)le32(s + 68);
        int villager = (int)le32(s + 72);
        int wither = (int)le32(s + 76);
        int charged = (int)le32(s + 80);
        float diff_factor = le_float(s + 88);

        int kind = native_kind(raw_kind);
        struct living *l;

        if ((int)le32(s + 96) == 1)
        {
            /* A spawn-time construct (the spider jockey's rider): the spawn it
             * came from built it already, so the replay takes that entity
             * instead of spawning a second one. */
            if (!construct)
            {
                printf("FAIL %s: spawn %d is a construct but the previous spawn built none\n", dir, i);
                gunzip_close(sfile);
                free(spawns);
                free(manifest);
                return 2;
            }

            l = lv_get(construct->livh);
        }
        else
        {
            int before = an.n;
            l = hostile_spawn(&an, kind, index, x, y, z, yaw, pitch, mx, my, mz,
                              child, villager, wither, charged, diff_factor);

            /* a spider whose onSpawnWithEgg rolled a jockey leaves its rider as
             * the probe list's last entry, right after the spider it rides */
            construct = an.n > before + 1 ? an_ent_at(an.slot[an.n - 1]) : NULL;
        }

        if (!l)
        {
            printf("FAIL %s: could not spawn hostile %d\n", dir, i);
            gunzip_close(sfile);
            free(spawns);
            free(manifest);
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
    if (strstr(manifest, "\"kinds_filter\":\"\\\"silverfish\\\"\"")) an.next_spawn_index = nspawns;

    /* A run whose manifest says the probe player joined World.playerEntities
     * (an enderman run: findPlayerToAttack goes through
     * getClosestVulnerablePlayerToEntity, which reads that list) makes the
     * probe player the an_world's player, the way an/iew's other runs do. */
    if (manifest_flag(manifest, "probe_player_in_player_entities") == 1)
    {
        for (int i = 0; i < an.n; ++i)
        {
            struct an_ent *en = an_ent_at(an.slot[i]);

            if (en->is_living && lv_get(en->livh)->kind == HK_PLAYER)
            {
                an.playerh = lv_ref(lv_get(en->livh));
                an.has_player = 1;
                an.player_x = lv_get(en->livh)->e.pos_x;
                an.player_y = lv_get(en->livh)->e.pos_y;
                an.player_z = lv_get(en->livh)->e.pos_z;
                break;
            }
        }
    }

    int rc = replay(dir, &an, &w, nents, ticks, manifest, night);

    trace_close();
    an_free(&an);
    free(manifest);
    return (failures != 0 || rc != 0) ? 1 : 0;
}

static int replay(const char *dir, struct an_world *an, struct world *w, int nents, int ticks, const char *manifest, int night)
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
    int rec_bytes = manifest_record_bytes(manifest);
    unsigned char rec[REC_BUF_BYTES];
    unsigned char got_rec[HOSTILE_STATE_BYTES];
    int next_tick = -1, next_count = 0;
    int t;

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

    for (int i = 0; i < AN_MAX_ENTITIES; ++i) { oracle_prev_ok[i] = 0; mine_prev_ok[i] = 0; }

    for (t = 0; t < ticks; ++t)
    {
        if (t == 20 && strstr(manifest, "\"kinds_filter\":\"\\\"pigman\\\"\""))
        {
            struct living *player = NULL;
            struct living *victim = NULL;
            double best_distance = INFINITY;
            for (int i = 0; i < an->n; ++i)
            {
                struct living *l = an_ent_at(an->slot[i])->is_living ? lv_get(an_ent_at(an->slot[i])->livh) : NULL;
                if (l && l->kind == HK_PLAYER) player = l;
            }
            for (int i = 0; i < an->n; ++i)
            {
                struct living *l = an_ent_at(an->slot[i])->is_living ? lv_get(an_ent_at(an->slot[i])->livh) : NULL;
                if (l && l->kind == HK_PIGMAN && !l->is_dead && player)
                {
                    double dx = l->e.pos_x - player->e.pos_x, dz = l->e.pos_z - player->e.pos_z;
                    double distance = dx * dx + dz * dz;
                    if (distance < best_distance) { best_distance = distance; victim = l; }
                }
            }
            if (player && victim)
            {
                pigman_attack_entity_from(victim, player, DMG_MOB, 1.0F, an->det);
            }
        }
        if (t == 0 && strstr(manifest, "\"kinds_filter\":\"\\\"silverfish\\\"\""))
        {
            for (int i = 0; i < nents; ++i)
                if (an_ent_at(an->slot[i])->is_living && lv_get(an_ent_at(an->slot[i])->livh)->kind == HK_SILVERFISH)
                    living_attack_entity_from(lv_get(an_ent_at(an->slot[i])->livh), DMG_MAGIC, 1.0F, an->det);
        }
        if (night == 2)
        {
            an->skylight = (t < ticks / 2) ? 0 : 11;
        }

        /* removals stream for this tick */
        while (!removal_eof && rem_tick == t)
        {
            if (gunzip_gets(rf, line, sizeof line))
            {
                if (sscanf(line, "%d %d %d %d %llx %d", &rem_tick, &rem_si, &rem_id, &rem_reason, &rem_health, &rem_fire) != 6)
                    removal_eof = 1;
            }
            else removal_eof = 1;
        }

        
        
        an_tick(an, t);

        /* read tick header (8 bytes: tick, count) */
        {
            unsigned char head[8];
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
            if (gunzip_read(tf, rec, rec_bytes) != rec_bytes)
            {
                printf("FAIL %s tick %d: the tick records end inside the tick\n", dir, t);
                ++failures;
                break;
            }

            int si = (int)le32(rec);
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
                ++failures;
                continue;
            }

            hostile_write_state(an, en, got_rec, t);


            uint64_t want_rand = le64(rec + 260) & 0xFFFFFFFFFFFFULL;
            uint64_t got_rand = le64(got_rec + 260) & 0xFFFFFFFFFFFFULL;

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

            cmp_int(t, si, "entity id", (int32_t)le32(rec + 4), (int32_t)le32(got_rec + 4));
            cmp_int(t, si, "kind", (int32_t)le32(rec + 8), (int32_t)le32(got_rec + 8));

            uint64_t want_hash = le64(rec + 12);
            uint64_t got_hash = le64(got_rec + 12);

            if (want_hash != got_hash)
            {
                char wbuf[32], gbuf[32];
                snprintf(wbuf, sizeof wbuf, "%016llx", (unsigned long long)want_hash);
                snprintf(gbuf, sizeof gbuf, "%016llx", (unsigned long long)got_hash);
                fail_rec(t, si, "nbt hash", wbuf, gbuf);
            }

            if (en->is_living)
            {
                cmp_int(t, si, "entityAge", (long long)(int32_t)le32(rec + 20), (long long)(int32_t)le32(got_rec + 20));
                cmp_int(t, si, "ticksExisted", (long long)(int32_t)le32(rec + 24), (long long)(int32_t)le32(got_rec + 24));
                cmp_int(t, si, "ai tick count", (long long)(int32_t)le32(rec + 28), (long long)(int32_t)le32(got_rec + 28));
                cmp_int(t, si, "ai executing", (long long)(int32_t)le32(rec + 32), (long long)(int32_t)le32(got_rec + 32));

                for (int k = 0; k < 10; ++k)
                {
                    char field[32];
                    snprintf(field, sizeof field, "task_state[%d]", k);
                    cmp_int(t, si, field, (int32_t)le32(rec + 36 + k * 4), (int32_t)le32(got_rec + 36 + k * 4));
                }

                cmp_int(t, si, "nav has path", (int32_t)le32(rec + 76), (int32_t)le32(got_rec + 76));
                cmp_int(t, si, "nav index", (int32_t)le32(rec + 80), (int32_t)le32(got_rec + 80));
                cmp_int(t, si, "nav length", (int32_t)le32(rec + 84), (int32_t)le32(got_rec + 84));
                cmp_int(t, si, "nav totalTicks", (int32_t)le32(rec + 88), (int32_t)le32(got_rec + 88));
                cmp_int(t, si, "nav ticksAtLastPos", (int32_t)le32(rec + 92), (int32_t)le32(got_rec + 92));
                cmp_dbl(t, si, "nav speed", le_double(rec + 96), le_double(got_rec + 96));
                cmp_dbl(t, si, "nav lastPosCheck.x", le_double(rec + 104), le_double(got_rec + 104));
                cmp_dbl(t, si, "nav lastPosCheck.y", le_double(rec + 112), le_double(got_rec + 112));
                cmp_dbl(t, si, "nav lastPosCheck.z", le_double(rec + 120), le_double(got_rec + 120));

                uint64_t want_nav = le64(rec + 128);
                uint64_t got_nav = le64(got_rec + 128);
                if (want_nav != got_nav)
                {
                    char wb[32], gb[32];
                    snprintf(wb, sizeof wb, "%016llx", (unsigned long long)want_nav);
                    snprintf(gb, sizeof gb, "%016llx", (unsigned long long)got_nav);
                    fail_rec(t, si, "nav path points", wb, gb);
                }

                cmp_int(t, si, "moveHelper update", (int32_t)le32(rec + 136), (int32_t)le32(got_rec + 136));
                cmp_dbl(t, si, "moveHelper x", le_double(rec + 140), le_double(got_rec + 140));
                cmp_dbl(t, si, "moveHelper y", le_double(rec + 148), le_double(got_rec + 148));
                cmp_dbl(t, si, "moveHelper z", le_double(rec + 156), le_double(got_rec + 156));
                cmp_dbl(t, si, "moveHelper speed", le_double(rec + 164), le_double(got_rec + 164));
                cmp_int(t, si, "lookHelper isLooking", (int32_t)le32(rec + 172), (int32_t)le32(got_rec + 172));
                cmp_dbl(t, si, "lookHelper x", le_double(rec + 176), le_double(got_rec + 176));
                cmp_dbl(t, si, "lookHelper y", le_double(rec + 184), le_double(got_rec + 184));
                cmp_dbl(t, si, "lookHelper z", le_double(rec + 192), le_double(got_rec + 192));
                cmp_flt(t, si, "lookHelper deltaYaw", le_float(rec + 200), le_float(got_rec + 200));
                cmp_flt(t, si, "lookHelper deltaPitch", le_float(rec + 204), le_float(got_rec + 204));
                cmp_int(t, si, "jumpHelper isJumping", (int32_t)le32(rec + 208), (int32_t)le32(got_rec + 208));
                cmp_flt(t, si, "moveForward", le_float(rec + 212), le_float(got_rec + 212));
                cmp_flt(t, si, "moveStrafing", le_float(rec + 216), le_float(got_rec + 216));
                cmp_flt(t, si, "rotationYawHead", le_float(rec + 220), le_float(got_rec + 220));
                cmp_flt(t, si, "renderYawOffset", le_float(rec + 224), le_float(got_rec + 224));
                cmp_int(t, si, "livingSoundTime", (int32_t)le32(rec + 228), (int32_t)le32(got_rec + 228));
                cmp_int(t, si, "body counter", (int32_t)le32(rec + 232), (int32_t)le32(got_rec + 232));
                cmp_flt(t, si, "body yaw", le_float(rec + 236), le_float(got_rec + 236));
                cmp_int(t, si, "revengeTimer", (int32_t)le32(rec + 252), (int32_t)le32(got_rec + 252));
                cmp_int(t, si, "flags", (int32_t)le32(rec + 256), (int32_t)le32(got_rec + 256));
            }
            else
            {
                cmp_int(t, si, "ticksExisted", (int32_t)le32(rec + 24), (int32_t)le32(got_rec + 24));
                cmp_int(t, si, "flags", (int32_t)le32(rec + 256), (int32_t)le32(got_rec + 256));
            }

            /* extra hostile fields */
            cmp_int(t, si, "attackTime", (int32_t)le32(rec + 272), (int32_t)le32(got_rec + 272));
            cmp_int(t, si, "hurtTime", (int32_t)le32(rec + 276), (int32_t)le32(got_rec + 276));
            cmp_int(t, si, "hurtResistantTime", (int32_t)le32(rec + 280), (int32_t)le32(got_rec + 280));
            cmp_int(t, si, "maxHurtTime", (int32_t)le32(rec + 284), (int32_t)le32(got_rec + 284));
            cmp_int(t, si, "recentlyHit", (int32_t)le32(rec + 288), (int32_t)le32(got_rec + 288));
            cmp_flt(t, si, "health", le_float(rec + 292), le_float(got_rec + 292));
            cmp_int(t, si, "entityToAttack", (int32_t)le32(rec + 296), (int32_t)le32(got_rec + 296));
            cmp_int(t, si, "attackTarget", (int32_t)le32(rec + 300), (int32_t)le32(got_rec + 300));
            cmp_int(t, si, "lastAttacker", (int32_t)le32(rec + 304), (int32_t)le32(got_rec + 304));
            cmp_int(t, si, "currentTarget", (int32_t)le32(rec + 308), (int32_t)le32(got_rec + 308));
            cmp_int(t, si, "numTicksToChaseTarget", (int32_t)le32(rec + 312), (int32_t)le32(got_rec + 312));
            cmp_int(t, si, "fleeingTick", (int32_t)le32(rec + 316), (int32_t)le32(got_rec + 316));
            cmp_int(t, si, "hasAttacked", (int32_t)le32(rec + 320), (int32_t)le32(got_rec + 320));
            cmp_int(t, si, "navFlags", (int32_t)le32(rec + 324), (int32_t)le32(got_rec + 324));

            for (int s = 0; s < 5; ++s)
            {
                char f[48];
                snprintf(f, sizeof f, "equip[%d].id", s);
                cmp_int(t, si, f, (int32_t)le32(rec + 328 + s * 12), (int32_t)le32(got_rec + 328 + s * 12));
                snprintf(f, sizeof f, "equip[%d].damage", s);
                cmp_int(t, si, f, (int32_t)le32(rec + 332 + s * 12), (int32_t)le32(got_rec + 332 + s * 12));
                snprintf(f, sizeof f, "equip[%d].count", s);
                cmp_int(t, si, f, (int32_t)le32(rec + 336 + s * 12), (int32_t)le32(got_rec + 336 + s * 12));
            }

            cmp_int(t, si, "zombie conversionTime", (int32_t)le32(rec + 388), (int32_t)le32(got_rec + 388));
            cmp_int(t, si, "zombie isChild", (int32_t)le32(rec + 392), (int32_t)le32(got_rec + 392));
            cmp_int(t, si, "zombie isVillager", (int32_t)le32(rec + 396), (int32_t)le32(got_rec + 396));
            cmp_int(t, si, "zombie canBreakDoors", (int32_t)le32(rec + 400), (int32_t)le32(got_rec + 400));
            cmp_int(t, si, "zombie isConverting", (int32_t)le32(rec + 404), (int32_t)le32(got_rec + 404));

            cmp_int(t, si, "skeletonType", (int32_t)le32(rec + 408), (int32_t)le32(got_rec + 408));
            cmp_int(t, si, "arrowRangedAttackTime", (int32_t)le32(rec + 412), (int32_t)le32(got_rec + 412));
            cmp_int(t, si, "arrowField75318f", (int32_t)le32(rec + 416), (int32_t)le32(got_rec + 416));

            cmp_int(t, si, "creeper lastActiveTime", (int32_t)le32(rec + 420), (int32_t)le32(got_rec + 420));
            cmp_int(t, si, "creeper timeSinceIgnited", (int32_t)le32(rec + 424), (int32_t)le32(got_rec + 424));
            cmp_int(t, si, "creeper fuseTime", (int32_t)le32(rec + 428), (int32_t)le32(got_rec + 428));
            cmp_int(t, si, "creeper explosionRadius", (int32_t)le32(rec + 432), (int32_t)le32(got_rec + 432));
            cmp_int(t, si, "creeper state", (int32_t)le32(rec + 436), (int32_t)le32(got_rec + 436));
            cmp_int(t, si, "creeper powered", (int32_t)le32(rec + 440), (int32_t)le32(got_rec + 440));
            cmp_int(t, si, "creeper ignited", (int32_t)le32(rec + 444), (int32_t)le32(got_rec + 444));
            cmp_int(t, si, "spider climb", (int32_t)le32(rec + 448), (int32_t)le32(got_rec + 448));

            cmp_int(t, si, "attackOnCollide attackTick", (int32_t)le32(rec + 452), (int32_t)le32(got_rec + 452));
            cmp_int(t, si, "attackOnCollide cooldown", (int32_t)le32(rec + 456), (int32_t)le32(got_rec + 456));
            cmp_dbl(t, si, "attackOnCollide px", le_double(rec + 460), le_double(got_rec + 460));
            cmp_dbl(t, si, "attackOnCollide py", le_double(rec + 468), le_double(got_rec + 468));
            cmp_dbl(t, si, "attackOnCollide pz", le_double(rec + 476), le_double(got_rec + 476));

            cmp_int(t, si, "air", (int32_t)le32(rec + 484), (int32_t)le32(got_rec + 484));
            cmp_int(t, si, "fire", (int32_t)le32(rec + 488), (int32_t)le32(got_rec + 488));
            cmp_int(t, si, "deathTime", (int32_t)le32(rec + 492), (int32_t)le32(got_rec + 492));
            cmp_flt(t, si, "field_70764_aw", le_float(rec + 496), le_float(got_rec + 496));
            cmp_flt(t, si, "limbSwing", le_float(rec + 500), le_float(got_rec + 500));
            cmp_flt(t, si, "limbSwingAmount", le_float(rec + 504), le_float(got_rec + 504));

            cmp_dbl(t, si, "attr maxHealth", le_double(rec + 508), le_double(got_rec + 508));
            cmp_dbl(t, si, "attr moveSpeed", le_double(rec + 516), le_double(got_rec + 516));
            cmp_dbl(t, si, "attr followRange", le_double(rec + 524), le_double(got_rec + 524));
            cmp_dbl(t, si, "attr attackDamage", le_double(rec + 532), le_double(got_rec + 532));

            cmp_dbl(t, si, "posX", le_double(rec + 540), le_double(got_rec + 540));
            cmp_dbl(t, si, "posY", le_double(rec + 548), le_double(got_rec + 548));
            cmp_dbl(t, si, "posZ", le_double(rec + 556), le_double(got_rec + 556));
            cmp_dbl(t, si, "motionX", le_double(rec + 564), le_double(got_rec + 564));
            cmp_dbl(t, si, "motionY", le_double(rec + 572), le_double(got_rec + 572));
            cmp_dbl(t, si, "motionZ", le_double(rec + 580), le_double(got_rec + 580));
            cmp_flt(t, si, "rotationYaw", le_float(rec + 588), le_float(got_rec + 588));
            cmp_flt(t, si, "rotationPitch", le_float(rec + 592), le_float(got_rec + 592));
            cmp_int(t, si, "onGround", (int32_t)le32(rec + 596), (int32_t)le32(got_rec + 596));
            cmp_int(t, si, "silverfish allySummonCooldown", (int32_t)le32(rec + 600), (int32_t)le32(got_rec + 600));

            /* the enderman block, which only a record as long as this build's
             * carries */
            if (rec_bytes >= ENDERMAN_BLOCK_OFF + ENDERMAN_EXTRA_BYTES)
            {
                const char *path_kind = (int32_t)le32(rec + 8) == 7 ? "silverfish" : "enderman";
                char path_field[64];
                cmp_int(t, si, "enderman carried block",
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 0), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 0));
                cmp_int(t, si, "enderman carrying data",
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 4), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 4));
                cmp_int(t, si, "enderman stareTimer",
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 8), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 8));
                cmp_int(t, si, "enderman teleportDelay",
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 12), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 12));
                cmp_int(t, si, "enderman flags (isAggressive, screaming)",
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 16), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 16));
                cmp_int(t, si, "enderman lastEntityToAttack",
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 20), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 20));
                snprintf(path_field, sizeof path_field, "%s path hasPath", path_kind);
                cmp_int(t, si, path_field,
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 24), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 24));
                snprintf(path_field, sizeof path_field, "%s path index", path_kind);
                cmp_int(t, si, path_field,
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 28), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 28));
                snprintf(path_field, sizeof path_field, "%s path length", path_kind);
                cmp_int(t, si, path_field,
                        (int32_t)le32(rec + ENDERMAN_BLOCK_OFF + 32), (int32_t)le32(got_rec + ENDERMAN_BLOCK_OFF + 32));

                uint64_t want_path = le64(rec + ENDERMAN_BLOCK_OFF + 36);
                uint64_t got_path = le64(got_rec + ENDERMAN_BLOCK_OFF + 36);

                if (want_path != got_path)
                {
                    char wb[32], gb[32];
                    snprintf(wb, sizeof wb, "%016llx", (unsigned long long)want_path);
                    snprintf(gb, sizeof gb, "%016llx", (unsigned long long)got_path);
                    snprintf(path_field, sizeof path_field, "%s path points", path_kind);
                    fail_rec(t, si, path_field, wb, gb);
                }
            }
            if (rec_bytes >= ENDERMAN_BLOCK_OFF + ENDERMAN_EXTRA_BYTES + 12)
            {
                cmp_int(t, si, "pigman angerLevel", (int32_t)le32(rec + 648), (int32_t)le32(got_rec + 648));
                cmp_int(t, si, "pigman randomSoundDelay", (int32_t)le32(rec + 652), (int32_t)le32(got_rec + 652));
                cmp_int(t, si, "pigman lastEntityToAttack", (int32_t)le32(rec + 656), (int32_t)le32(got_rec + 656));
            }
            if (rec_bytes >= HOSTILE_STATE_BYTES)
            {
                cmp_int(t, si, "pigman path hasPath", (int32_t)le32(rec + 660), (int32_t)le32(got_rec + 660));
                cmp_int(t, si, "pigman path index", (int32_t)le32(rec + 664), (int32_t)le32(got_rec + 664));
                cmp_int(t, si, "pigman path length", (int32_t)le32(rec + 668), (int32_t)le32(got_rec + 668));
                uint64_t want_pig_path = le64(rec + 672), got_pig_path = le64(got_rec + 672);
                if (want_pig_path != got_pig_path)
                {
                    char wb[32], gb[32];
                    snprintf(wb, sizeof wb, "%016llx", (unsigned long long)want_pig_path);
                    snprintf(gb, sizeof gb, "%016llx", (unsigned long long)got_pig_path);
                    fail_rec(t, si, "pigman path points", wb, gb);
                }
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

                if (strcmp(mine, text) != 0)
                {
                    struct nbt *wa = nbt_parse(text);
                    struct nbt *go = nbt_parse(mine);

                    if (wa && go)
                    {
                        char buf[512];
                        nbt_diff(wa, go, buf, sizeof buf);
                        printf("FAIL %s tick %d entity %d: NBT text: %s\n",
                               dir, t, an_ent_at(an->slot[i])->spawn_index, buf);
                    }
                    else
                    {
                        printf("FAIL %s tick %d entity %d: NBT text differs and does not parse\n",
                               dir, t, an_ent_at(an->slot[i])->spawn_index);
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

            if (n != 32 || strcmp(tok[0], "t") != 0 || atoi(tok[1]) != t)
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

        if (failures > 40)
        {
            printf("FAIL %s: stopping after 40 failures at tick %d\n", dir, t);
            break;
        }
    }

    /* final block state */
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
    return failures != 0 ? 1 : 0;
}
