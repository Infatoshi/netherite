#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/entity.h"
#include "../engine/item_entity.h"
#include "../engine/living.h"
#include "../engine/projectile.h"
#include "../engine/world.h"

#define SPAWN_BYTES 104
#define TICK_BYTES 144

static int failures = 0;
static int kind_by_sidx[8192];

static void hatch_chicken(ie_world *iew, ie_ent *egg)
{
    struct an_world *an = iew->user_data;
    an_egg_chicken(an, egg);
    struct an_ent *newborn = an_ent_at(an->slot[an->n - 1]);
    newborn->spawn_index = iew->next_spawn_index++;
    lv_get(newborn->livh)->spawn_index = newborn->spawn_index;
}

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
    if (!p) { fprintf(stderr, "manifest: no %s\n", key); exit(2); }
    return strtoll(p + strlen(pat), NULL, 10);
}

static void *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    unsigned char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "short read in %s\n", path); exit(2); }
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
    struct splitref splits[64];
    int nsplits;
};

static void read_ref(struct ref *r, const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
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

static void ie_fizz_stub(void *self)
{
    ie_ent *en = self;
    (void)det_rng_float(&en->rand);
    (void)det_rng_float(&en->rand);
}

static void ie_attack_stub(void *self, int src, float damage)
{
    ie_ent *en = self;
    (void)src;
    en->health -= (int)damage;
    if (en->health <= 0) en->is_dead = 1;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_projectiles PROJECTILES_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1200];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest;
    {
        FILE *f = fopen(path, "rb");
        if (!f) { printf("skip %s: no manifest\n", dir); return 0; }
        fseek(f, 0, SEEK_END);
        mlen = (size_t)ftell(f);
        rewind(f);
        manifest = malloc(mlen + 1);
        if (fread(manifest, 1, mlen, f) != mlen) { fprintf(stderr, "short read\n"); return 2; }
        manifest[mlen] = 0;
        fclose(f);
    }

    if (!strstr(manifest, "\"kind\":\"projectiles\""))
    {
        printf("skip %s: not a projectiles dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int nProj = (int)manifest_int(manifest, "projectiles");
    int nshapes = (int)manifest_int(manifest, "shapes");
    int ticks = (int)manifest_int(manifest, "ticks");

    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int radius = (int)manifest_int(manifest, "radius");
    int ring = (int)manifest_int(manifest, "ring");

    struct world w;
    world_init(&w, seed);

    int x0 = cx - radius - ring, x1 = cx + radius + ring;
    int z0 = cz - radius - ring, z1 = cz + radius + ring;
    for (int lx = x0; lx <= x1; ++lx)
    {
        for (int lz = z0; lz <= z1; ++lz)
        {
            world_load_chunk(&w, lx, lz);
        }
    }
    /* Shapes */
    snprintf(path, sizeof path, "%s/shapes.bin", dir);
    size_t slen;
    unsigned char *shapes = read_file(path, &slen);
    if (slen % 16 != 0) { fprintf(stderr, "%s: shapes.bin is %zu bytes\n", dir, slen); return 2; }

    for (int i = 0; i < nshapes; ++i)
    {
        const unsigned char *s = shapes + i * 16;
        int x = (int)le32(s), y = (int)le32(s + 4), z = (int)le32(s + 8);
        int id = le16(s + 12), meta = s[14];
        world_set_block(&w, x, y, z, id, meta, 2);
    }
    free(shapes);

    /* Det state */
    struct ref start;
    read_ref(&start, dir, "start.txt");

    det_state det;
    det_init(&det);
    det_load(&det, start.world_seed, start.seeder, start.math, start.next_id);

    for (int i = 0; i < start.nsplits; ++i)
    {
        det_split_add(&det, start.splits[i].name, start.splits[i].state, start.splits[i].used);
    }

    ie_world iew;
    ie_init(&iew, &w, &det);
    iew.world_rand = det_new_random_role(&det, DET_OTHER);
    int keep_chickens = strstr(manifest, "\"keep_chickens\":true") != NULL;
    struct an_world an;
    if (keep_chickens)
    {
        an_init(&an, &w, &det);
        an.iew.world_rand = iew.world_rand;
        iew.user_data = &an;
        iew.on_egg_chicken = hatch_chicken;
    }

    /* Spawns */
    snprintf(path, sizeof path, "%s/spawns.bin", dir);
    size_t spnlen;
    unsigned char *spawns = read_file(path, &spnlen);
    if (spnlen % SPAWN_BYTES != 0) { fprintf(stderr, "%s: spawns.bin is %zu bytes\n", dir, spnlen); return 2; }
    int nSpawns = (int)(spnlen / SPAWN_BYTES);

    for (int i = 0; i < nSpawns; ++i)
    {
        const unsigned char *s = spawns + (size_t)i * SPAWN_BYTES;
        int sidx = (int)le32(s);
        int eid = (int)le32(s + 4);
        int kind = s[8];
        double px = le_double(s + 9);
        double py = le_double(s + 17);
        double pz = le_double(s + 25);
        double mx = le_double(s + 33);
        double my = le_double(s + 41);
        double mz = le_double(s + 49);
        float yaw = le_float(s + 57);
        float pitch = le_float(s + 61);
        double ax = le_double(s + 65);
        double ay = le_double(s + 73);
        double az = le_double(s + 81);
        int is_crit = s[89];
        int pot_dmg = (int)le32(s + 90);

        ie_ent *en = ie_ent_alloc();
        entity_init(&en->e, iew.w);
        en->e.can_trigger_walking = 0;
        en->e.self = en;
        if (kind <= IE_ORB) en->e.attack_from = ie_attack_stub;
        en->e.fizz = ie_fizz_stub;
        en->e.first_update = 1;
        en->spawn_index = sidx;
        en->entity_id = eid;
        en->kind = kind;
        det_next_entity_id_role(iew.det, iew.role);
        en->rand = det_new_random_role(iew.det, iew.role);
        int64_t msb, lsb;
        det_uuid_role(iew.det, iew.role, &msb, &lsb);
        en->health = 5;

        if (kind == IE_ITEM)
        {
            det_math_random_role(iew.det, iew.role);
            det_math_random_role(iew.det, iew.role);
            det_math_random_role(iew.det, iew.role);
            det_math_random_role(iew.det, iew.role);
            entity_set_size(&en->e, 0.25F, 0.25F);
            en->e.y_offset = en->e.height / 2.0F;
            en->stack_item = 1; /* stone */
            en->stack_count = 1;
        }
        else if (kind == IE_ARROW)
        {
            det_rng_gaussian(&en->rand); det_rng_bool(&en->rand);
            det_rng_gaussian(&en->rand); det_rng_bool(&en->rand);
            det_rng_gaussian(&en->rand); det_rng_bool(&en->rand);
            entity_set_size(&en->e, 0.5F, 0.5F);
            en->e.y_offset = 0.0F;
        }
        else if (kind >= IE_SNOWBALL && kind <= IE_POTION)
        {
            det_rng_gaussian(&en->rand);
            det_rng_gaussian(&en->rand);
            det_rng_gaussian(&en->rand);
            entity_set_size(&en->e, 0.25F, 0.25F);
            en->e.y_offset = 0.0F;
        }
        else if (kind == IE_LARGE_FIREBALL)
        {
            entity_set_size(&en->e, 1.0F, 1.0F);
            en->e.y_offset = 0.0F;
            en->explosion_power = 1;
        }
        else if (kind == IE_SMALL_FIREBALL)
        {
            entity_set_size(&en->e, 0.3125F, 0.3125F);
            en->e.y_offset = 0.0F;
        }

        entity_set_position(&en->e, px, py, pz);
        en->e.motion_x = mx;
        en->e.motion_y = my;
        en->e.motion_z = mz;
        en->prev_yaw = en->rotation_yaw = yaw;
        en->prev_pitch = en->rotation_pitch = pitch;
        en->accel_x = ax;
        en->accel_y = ay;
        en->accel_z = az;
        en->is_critical = is_crit;
        en->potion_damage = pot_dmg;
        en->tile_x = en->tile_y = en->tile_z = -1;
        if (sidx >= 0 && sidx < 8192) kind_by_sidx[sidx] = kind;

        iew.slot[iew.n++] = ie_ent_index(en);
        ie_added_to_world(&iew, en);
    }
    free(spawns);
    iew.next_spawn_index = nSpawns;

    /* Open reference streams */
    snprintf(path, sizeof path, "%s/removals.txt.gz", dir);
    gzFile rf = gzopen(path, "r");
    if (!rf) { perror(path); return 2; }

    snprintf(path, sizeof path, "%s/ticks.bin.gz", dir);
    gzFile tf = gzopen(path, "rb");
    if (!tf) { perror(path); return 2; }

    snprintf(path, sizeof path, "%s/digest.txt.gz", dir);
    gzFile df = gzopen(path, "r");
    if (!df) { perror(path); return 2; }
    gzFile cf = NULL;
    if (keep_chickens)
    {
        snprintf(path, sizeof path, "%s/chickens.txt.gz", dir);
        cf = gzopen(path, "r");
        if (!cf) { perror(path); return 2; }
    }

    char line[1024];
    int next_tick = -1, next_si = -1, next_id = -1, next_reason = -1, next_health = -1, next_fire = -1, next_age = -1;
    double next_y = 0.0;
    int removal_eof = 0;

    if (gzgets(rf, line, sizeof line))
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

    /* Metrics per kind: 3..10 */
    int spawned_counts[11] = {0};
    int hits_blocks[11] = {0};
    int hits_entities[11] = {0};
    int stuck_counts[11] = {0};
    int removed_counts[11] = {0};

    for (int i = 0; i < iew.n; ++i)
    {
        if (ie_ent_at(iew.slot[i])->kind >= 3 && ie_ent_at(iew.slot[i])->kind <= 10)
        {
            ++spawned_counts[ie_ent_at(iew.slot[i])->kind];
        }
    }

    for (int t = 0; t < ticks; ++t)
    {

        /* Check inGround transitions before tick */
        int prev_in_ground[IE_MAX_ENTITIES];
        for (int i = 0; i < iew.n; ++i)
        {
            prev_in_ground[i] = ie_ent_at(iew.slot[i])->in_ground;
        }

        ie_removal removals[IE_MAX_ENTITIES];
        int n_removals = 0;
        ie_tick(&iew, t, removals, IE_MAX_ENTITIES, &n_removals);
        if (keep_chickens)
        {
            an.iew.world_rand = iew.world_rand;
            for (int i = 0; i < an.n; ++i)
                if (an_ent_at(an.slot[i])->used) (void)an_tick_one(&an, an_ent_at(an.slot[i]), t);
            iew.world_rand = an.iew.world_rand;
        }

        for (int i = 0; i < n_removals; ++i)
        {
            const ie_removal *rv = &removals[i];


            int rkind = (rv->spawn_index >= 0 && rv->spawn_index < 8192) ? kind_by_sidx[rv->spawn_index] : 0;
            if (rkind >= 3 && rkind <= 10)
            {
                ++removed_counts[rkind];
                if (rv->reason == 4)
                {
                    /* Died on impact: check if it hit entity or block */
                    /* If target velocity changed, hit entity */
                    int hit_ent = 0;
                    for (int j = 0; j < iew.n; ++j)
                    {
                        if (ie_ent_at(iew.slot[j])->kind == IE_LARGE_FIREBALL && ie_ent_at(iew.slot[j])->velocity_changed)
                        {
                            hit_ent = 1;
                            ie_ent_at(iew.slot[j])->velocity_changed = 0;
                            break;
                        }
                    }
                    if (hit_ent) ++hits_entities[rkind];
                    else ++hits_blocks[rkind];
                }
            }

            if (removal_eof || rv->tick != next_tick || rv->spawn_index != next_si || rv->entity_id != next_id)
            {
                printf("FAIL %s tick %d: removal: oracle (t=%d si=%d id=%d), native (t=%d si=%d id=%d)\n",
                       dir, t, next_tick, next_si, next_id, rv->tick, rv->spawn_index, rv->entity_id);
                ++failures;
            }
            else
            {
                cmp_int(t, rv->spawn_index, "removal reason", next_reason, rv->reason);
                cmp_int(t, rv->spawn_index, "removal fire", next_fire, rv->fire);
                cmp_dbl(t, rv->spawn_index, "removal posY", next_y, rv->pos_y);
            }

            if (gzgets(rf, line, sizeof line))
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

        /* Check new inGround sticking */
        for (int i = 0; i < iew.n; ++i)
        {
            ie_ent *en = ie_ent_at(iew.slot[i]);
            if (en->is_dead) continue;
            if (!prev_in_ground[i] && en->in_ground)
            {
                if (en->kind >= 3 && en->kind <= 10)
                {
                    ++hits_blocks[en->kind];
                    if (en->kind == IE_ARROW) ++stuck_counts[en->kind];
                }
            }
        }

        for (int i = 0; i < iew.n; ++i) {
            if (ie_ent_at(iew.slot[i])->spawn_index >= 0 && ie_ent_at(iew.slot[i])->spawn_index < 8192) {
                kind_by_sidx[ie_ent_at(iew.slot[i])->spawn_index] = ie_ent_at(iew.slot[i])->kind;
            }
        }

        /* Compare tick records */
        while (1)
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
                got = gzread(tf, recbuf, TICK_BYTES);
            }

            if (got == 0)
            {
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
                memcpy(pending_rec, recbuf, TICK_BYTES);
                pending = 1;
                break;
            }
            if (rt < t)
            {
                printf("FAIL %s: tick records out of order: %d inside tick %d\n", dir, rt, t);
                ++failures;
                continue;
            }

            int rsi = (int)le32(recbuf + 4);
            int rid = (int)le32(recbuf + 8);
            int rkind = recbuf[12];
            double rx = le_double(recbuf + 13);
            double ry = le_double(recbuf + 21);
            double rz = le_double(recbuf + 29);
            double rmx = le_double(recbuf + 37);
            double rmy = le_double(recbuf + 45);
            double rmz = le_double(recbuf + 53);
            float ryaw = le_float(recbuf + 61);
            float rpitch = le_float(recbuf + 65);
            int ringround = recbuf[69];
            int rshake = recbuf[70];
            int rtig = (int)le32(recbuf + 71);
            int rtia = (int)le32(recbuf + 75);
            int rte = (int)le32(recbuf + 79);
            int rtx = (int)le32(recbuf + 83);
            int rty = (int)le32(recbuf + 87);
            int rtz = (int)le32(recbuf + 91);
            int rintile = (int)le32(recbuf + 95);
            int rindata = (int)le32(recbuf + 99);
            int rfire = (int)le32(recbuf + 103);
            int rflags = recbuf[107];
            double rax = le_double(recbuf + 108);
            double ray = le_double(recbuf + 116);
            double raz = le_double(recbuf + 124);

            ie_ent *en = NULL;
            for (int i = 0; i < iew.n; ++i)
            {
                if (ie_ent_at(iew.slot[i])->spawn_index == rsi)
                {
                    en = ie_ent_at(iew.slot[i]);
                    break;
                }
            }

            struct living *chicken = NULL;
            if (rkind == 12 && keep_chickens)
                for (int i = 0; i < an.n; ++i)
                    if (an_ent_at(an.slot[i])->used && an_ent_at(an.slot[i])->spawn_index == rsi)
                    { chicken = lv_get(an_ent_at(an.slot[i])->livh); break; }

            if (!en && !chicken)
            {
                printf("FAIL %s tick %d entity %d (id %d kind %d): oracle has live entity, native does not\n",
                       dir, t, rsi, rid, rkind);
                ++failures;
                continue;
            }

            if (chicken)
            {
                cmp_int(t, rsi, "entity id", rid, chicken->entity_id);
                cmp_dbl(t, rsi, "posX", rx, chicken->e.pos_x);
                cmp_dbl(t, rsi, "posY", ry, chicken->e.pos_y);
                cmp_dbl(t, rsi, "posZ", rz, chicken->e.pos_z);
                cmp_dbl(t, rsi, "motionX", rmx, chicken->e.motion_x);
                cmp_dbl(t, rsi, "motionY", rmy, chicken->e.motion_y);
                cmp_dbl(t, rsi, "motionZ", rmz, chicken->e.motion_z);
                cmp_flt(t, rsi, "yaw", ryaw, chicken->rotation_yaw);
                cmp_flt(t, rsi, "pitch", rpitch, chicken->rotation_pitch);
                cmp_int(t, rsi, "ticksExisted", rte, chicken->ticks_existed);
                cmp_int(t, rsi, "fire", rfire, chicken->e.fire);
                int ct, ci, age, egg_time;
                unsigned long long rand_state;
                if (!gzgets(cf, line, sizeof line) ||
                    sscanf(line, "%d %d %d %d %llx", &ct, &ci, &age, &egg_time, &rand_state) != 5)
                    fail_rec(t, rsi, "chicken record", "present", "missing");
                else
                {
                    cmp_int(t, rsi, "chicken tick", ct, t);
                    cmp_int(t, rsi, "chicken index", ci, rsi);
                    cmp_int(t, rsi, "growing age", age, chicken->growing_age);
                    cmp_int(t, rsi, "egg timer", egg_time, chicken->time_until_next_egg);
                    cmp_int(t, rsi, "chicken random", rand_state, det_rng_state(&chicken->rand));
                }
                continue;
            }

            cmp_dbl(t, rsi, "posX", rx, en->e.pos_x);
            cmp_dbl(t, rsi, "posY", ry, en->e.pos_y);
            cmp_dbl(t, rsi, "posZ", rz, en->e.pos_z);
            cmp_dbl(t, rsi, "motionX", rmx, en->e.motion_x);
            cmp_dbl(t, rsi, "motionY", rmy, en->e.motion_y);
            cmp_dbl(t, rsi, "motionZ", rmz, en->e.motion_z);
            cmp_flt(t, rsi, "yaw", ryaw, en->rotation_yaw);
            cmp_flt(t, rsi, "pitch", rpitch, en->rotation_pitch);
            cmp_int(t, rsi, "inGround", ringround, en->in_ground);
            cmp_int(t, rsi, "shake", rshake, en->shake);
            cmp_int(t, rsi, "ticksInGround", rtig, en->ticks_in_ground);
            cmp_int(t, rsi, "ticksInAir", rtia, en->ticks_in_air);
            cmp_int(t, rsi, "ticksExisted", rte, en->ticks_existed);
            cmp_int(t, rsi, "tileX", rtx, en->tile_x);
            cmp_int(t, rsi, "tileY", rty, en->tile_y);
            cmp_int(t, rsi, "tileZ", rtz, en->tile_z);
            cmp_int(t, rsi, "inTile", rintile, en->in_tile);
            cmp_int(t, rsi, "inData", rindata, en->in_data);
            cmp_int(t, rsi, "fire", rfire, en->e.fire);
            int nflags = (en->e.on_ground ? 1 : 0) | (en->in_water ? 2 : 0) | (en->is_critical ? 4 : 0);
            cmp_int(t, rsi, "flags", rflags, nflags);
            cmp_dbl(t, rsi, "accelX", rax, en->accel_x);
            cmp_dbl(t, rsi, "accelY", ray, en->accel_y);
            cmp_dbl(t, rsi, "accelZ", raz, en->accel_z);

            if (failures >= 20)
            {
                printf("FAIL %s: too many failures, stopping\n", dir);
                gzclose(rf);
                gzclose(tf);
                gzclose(df);
                return 1;
            }
        }

        /* Check Det digest */
        if (gzgets(df, line, sizeof line))
        {
            int dt;
            unsigned long long s0, m0, sp0, s1, m1, sp1, s2, m2, sp2, s3, m3, sp3;
            int nid;
            int nscan = sscanf(line, "t %d role 0 %llx %llx %llx role 1 %llx %llx %llx role 2 %llx %llx %llx role 3 %llx %llx %llx nextId %d",
                               &dt, &s0, &m0, &sp0, &s1, &m1, &sp1, &s2, &m2, &sp2, &s3, &m3, &sp3, &nid);
            if (nscan >= 10)
            {
                uint64_t my_math2 = det_math_state(&det, 2);
                if (my_math2 != m2)
                {
                    printf("FAIL %s tick %d: Det role 2 math state: oracle %llx, native %llx\n", dir, t, m2, (unsigned long long)my_math2);
                    ++failures;
                }
            }
            else if (dt != t)
            {
                printf("FAIL %s tick %d: digest out of sync: %s\n", dir, t, line);
                ++failures;
            }
        }
    }

    gzclose(rf);
    gzclose(tf);
    gzclose(df);
    if (cf) gzclose(cf);

    const char *kname[11] = {"", "item", "orb", "arrow", "snowball", "egg", "ender_pearl", "exp_bottle", "potion", "small_fireball", "large_fireball"};
    printf("PASS %s (seed %lld, %d projectiles, %d ticks)\n", dir, (long long)seed, nProj, ticks);
    printf("kind            spawned  hits_blocks  hits_entities  stuck  removed\n");
    for (int k = 3; k <= 10; ++k)
    {
        printf("%-15s %7d %12d %14d %6d %8d\n",
               kname[k], spawned_counts[k], hits_blocks[k], hits_entities[k], stuck_counts[k], removed_counts[k]);
    }

    world_free(&w);
    ie_free(&iew);
    return failures == 0 ? 0 : 1;
}
