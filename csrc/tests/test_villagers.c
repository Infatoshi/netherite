#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/gunzip.h"
#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/jmath.h"
#include "../engine/living.h"
#include "../engine/nbtjson.h"
#include "../engine/trace.h"
#include "../engine/trades.h"
#include "../engine/villagers.h"
#include "../engine/world.h"

#define ENT_STATE_BYTES 320
#define SPAWN_BYTES 100
#define VILLAGE_STATE_BYTES 36
#define BLOCK_WRITE_BYTES 20

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

static void fail_v(int tick, int vi, const char *field, const char *want, const char *got)
{
    printf("FAIL tick %d village %d: %s: the oracle recorded %s, the native replay has %s\n",
           tick, vi, field, want, got);
    ++failures;
}

static void fail_bw(int tick, int bwi, const char *field, const char *want, const char *got)
{
    printf("FAIL tick %d blockwrite %d: %s: the oracle recorded %s, the native replay has %s\n",
           tick, bwi, field, want, got);
    ++failures;
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

static void cmp_dbl(int tick, int si, const char *field, double want, double got)
{
    if (want == want && want == got) return;
    char w[64], g[64];
    snprintf(w, sizeof w, "%a", want);
    snprintf(g, sizeof g, "%a", got);
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

static void cmp_v_int(int tick, int vi, const char *field, long long want, long long got)
{
    if (want == got) return;
    char w[32], g[32];
    snprintf(w, sizeof w, "%lld", want);
    snprintf(g, sizeof g, "%lld", got);
    fail_v(tick, vi, field, w, g);
}

static void cmp_bw_int(int tick, int bwi, const char *field, long long want, long long got)
{
    if (want == got) return;
    char w[32], g[32];
    snprintf(w, sizeof w, "%lld", want);
    snprintf(g, sizeof g, "%lld", got);
    fail_bw(tick, bwi, field, w, g);
}

/* Skylight and celestial angle calculation matching MC 1.7.10 */
static float calc_celestial_angle(int64_t world_time)
{
    int day = (int)(world_time % 24000L);
    float a = ((float)day + 1.0f) / 24000.0f - 0.25f;
    if (a < 0.0f) ++a;
    if (a > 1.0f) --a;
    float b = a;
    a = 1.0f - (float)((cos((double)a * 3.141592653589793) + 1.0) / 2.0);
    return b + (a - b) / 3.0f;
}

static int calc_skylight_subtracted(int64_t world_time)
{
    float ang = calc_celestial_angle(world_time);
    float v = 1.0f - (mh_cos(ang * (float)(3.14159265358979323846 * 2.0)) * 2.0f + 0.5f);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (int)(v * 11.0f);
}

struct block_write {
    int x, y, z, meta, flags;
};
static struct block_write tick_writes[512];
static int num_tick_writes = 0;

static void on_block_write(int x, int y, int z, int meta, int flags, void *ctx)
{
    (void)ctx;
    if (num_tick_writes < 512)
    {
        tick_writes[num_tick_writes++] = (struct block_write){x, y, z, meta, flags};
    }
}

static uint64_t calc_nav_hash(const struct path_ent *p)
{
    uint64_t nh = 0xcbf29ce484222325ULL;
    if (!p) return nh;
    for (int i = 0; i < p->length; ++i)
    {
        int px = p->pts[i][0];
        int py = p->pts[i][1];
        int pz = p->pts[i][2];
        for (int k = 0; k < 4; ++k) nh = (nh ^ (uint64_t)((px >> (8 * k)) & 255)) * 0x100000001b3ULL;
        for (int k = 0; k < 4; ++k) nh = (nh ^ (uint64_t)((py >> (8 * k)) & 255)) * 0x100000001b3ULL;
        for (int k = 0; k < 4; ++k) nh = (nh ^ (uint64_t)((pz >> (8 * k)) & 255)) * 0x100000001b3ULL;
    }
    return nh;
}

static int get_task_counter(const struct ai_task *t)
{
    switch (t->cls)
    {
        case AIC_VILLAGER_MATE: return t->mating_timeout;
        case AIC_FOLLOW_GOLEM: return t->take_golem_rose_tick;
        case AIC_PLAY: return t->play_time;
        case AIC_OPEN_DOOR: return t->door_counter;
        case AIC_LOOK_AT_VILLAGER: return t->look_time;
        case AIC_WATCH_CLOSEST:
        case AIC_WATCH_CLOSEST2:
        case AIC_LOOK_AT_TRADE_PLAYER: return t->look_time;
        case AIC_LOOK_IDLE: return t->idle_time;
        default: return 0;
    }
}



static void check_entity(int tick, int si, struct living *l, const unsigned char *b)
{
    cmp_int(tick, si, "spawn_index", (int)le32(b), si);
    cmp_int(tick, si, "entity_id", (int)le32(b + 4), l->entity_id);
    cmp_int(tick, si, "kind", (int)le32(b + 8), l->kind == VK_VILLAGER ? 0 : (l->kind == VK_IRON_GOLEM ? 1 : 2));
    cmp_int(tick, si, "nbt_hash", (long long)le64(b + 12), (long long)living_nbt_hash(l));
    cmp_int(tick, si, "entity_age", (int)le32(b + 20), l->entity_age);
    cmp_int(tick, si, "ticks_existed", (int)le32(b + 24), l->ticks_existed);
    cmp_int(tick, si, "task_tick_count", (int)le32(b + 28), lv_ai(l)->tasks.tick_count);

    int mask = 0;
    for (int i = 0; i < lv_ai(l)->tasks.nexec; ++i) mask |= (1 << lv_ai(l)->tasks.executing[i]);
    cmp_int(tick, si, "task_mask", (int)le32(b + 32), mask);

    for (int i = 0; i < 16; ++i)
    {
        int slot_val = -1;
        if (i < lv_ai(l)->tasks.n)
        {
            slot_val = get_task_counter(&lv_ai(l)->tasks.entries[i].t);
        }
        char name[32];
        snprintf(name, sizeof name, "slot_%d", i);
        cmp_int(tick, si, name, (int)le32(b + 36 + i * 4), slot_val);
    }

    const struct path_ent *path = path_at(l->nav.path);
    cmp_int(tick, si, "nav_has_path", (int)le32(b + 100), path != NULL ? 1 : 0);
    cmp_int(tick, si, "nav_curr_idx", (int)le32(b + 104), path != NULL ? path->index : 0);
    cmp_int(tick, si, "nav_path_len", (int)le32(b + 108), path != NULL ? path->length : 0);
    cmp_int(tick, si, "nav_total_ticks", (int)le32(b + 112), l->nav.total_ticks);
    cmp_int(tick, si, "nav_ticks_at_last_pos", (int)le32(b + 116), l->nav.ticks_at_last_pos);
    cmp_dbl(tick, si, "nav_speed", le_double(b + 120), l->nav.speed);
    cmp_dbl(tick, si, "nav_lx", le_double(b + 128), l->nav.lx);
    cmp_dbl(tick, si, "nav_ly", le_double(b + 136), l->nav.ly);
    cmp_dbl(tick, si, "nav_lz", le_double(b + 144), l->nav.lz);
    cmp_int(tick, si, "nav_path_hash", (long long)le64(b + 152), (long long)calc_nav_hash(path));

    cmp_int(tick, si, "move_update", (int)le32(b + 160), l->move.update);
    cmp_dbl(tick, si, "move_speed", le_double(b + 164), l->move.speed);
    cmp_dbl(tick, si, "move_x", le_double(b + 172), l->move.x);
    cmp_dbl(tick, si, "move_y", le_double(b + 180), l->move.y);
    cmp_dbl(tick, si, "move_z", le_double(b + 188), l->move.z);

    cmp_int(tick, si, "look_is_looking", (int)le32(b + 196), l->look.is_looking);
    cmp_flt(tick, si, "look_delta_yaw", le_float(b + 200), l->look.delta_look_yaw);
    cmp_flt(tick, si, "look_delta_pitch", le_float(b + 204), l->look.delta_look_pitch);
    cmp_dbl(tick, si, "look_x", le_double(b + 208), l->look.x);
    cmp_dbl(tick, si, "look_y", le_double(b + 216), l->look.y);
    cmp_dbl(tick, si, "look_z", le_double(b + 224), l->look.z);

    cmp_int(tick, si, "jump_is_jumping", (int)le32(b + 232), l->jump.is_jumping);
    cmp_flt(tick, si, "move_forward", le_float(b + 236), l->move_forward);
    cmp_flt(tick, si, "move_strafing", le_float(b + 240), l->move_strafing);
    cmp_flt(tick, si, "rotation_yaw_head", le_float(b + 244), l->rotation_yaw_head);
    cmp_flt(tick, si, "render_yaw_offset", le_float(b + 248), l->render_yaw_offset);
    cmp_int(tick, si, "living_sound_time", (int)le32(b + 252), l->living_sound_time);

    cmp_int(tick, si, "body_counter", (int)le32(b + 256), l->body.counter);
    cmp_flt(tick, si, "body_yaw", le_float(b + 260), l->body.yaw);
    cmp_flt(tick, si, "health", le_float(b + 264), l->health);

    int flags = (l->added_to_chunk ? 1 : 0) | (l->e.on_ground ? 2 : 0) |
                (l->is_in_water ? 4 : 0) | (l->e.is_collided_horizontally ? 8 : 0);
    cmp_int(tick, si, "flags", (int)le32(b + 268), flags);

    cmp_int(tick, si, "growing_age", (int)le32(b + 272), l->growing_age);
    cmp_int(tick, si, "profession", (int)le32(b + 276), l->profession);
    cmp_int(tick, si, "wealth", (int)le32(b + 280), l->wealth);
    cmp_int(tick, si, "time_until_reset", (int)le32(b + 284), l->time_until_reset);
    cmp_int(tick, si, "needs_initialization", (int)le32(b + 288), l->needs_initialization);
    cmp_int(tick, si, "is_mating", (int)le32(b + 292), l->is_mating);
    cmp_int(tick, si, "isPlaying", (int)le32(b + 296), l->is_playing);
    cmp_int(tick, si, "attack_timer", (int)le32(b + 300), l->attack_timer);
    cmp_int(tick, si, "hold_rose_tick", (int)le32(b + 304), l->hold_rose_tick);
    cmp_int(tick, si, "rand_state", (long long)le64(b + 308), (long long)det_rng_state(&l->rand));
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "usage: test_villagers VILLAGERS_DIR [TRACE_PATH]\n");
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

    if (!strstr(manifest, "\"kind\":\"villagers\""))
    {
        printf("skip %s: not a villagers dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int ticks = (int)manifest_int(manifest, "ticks");
    int64_t timestart = manifest_int(manifest, "timestart");
    int64_t opseed = manifest_int(manifest, "opseed");

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

    /* ------------------------------------------------------------ shapes */
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

    /* ------------------------------------------------------------- Det */
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
    an.world_time = timestart;

    jrand shuf_rand;
    jr_seed(&shuf_rand, opseed + 999L);
    an.shuf_rand = &shuf_rand;

    an.on_write_cb = on_block_write;
    an.on_write_ctx = NULL;

    /* ------------------------------------------------ VillageCollection */
    struct village_collection vc;
    vc_init(&vc, &an);
    an.village_collection = &vc;

    struct village_siege vs;
    vs_init(&vs, &an);
    an.village_siege = &vs;

    int village_centers[3][2] = {{-45, -45}, {45, -45}, {-45, 45}};
    for (int vi = 0; vi < 3; ++vi)
    {
        int vx = village_centers[vi][0];
        int vz = village_centers[vi][1];
        vc_add_villager_position(&vc, vx, 64, vz);
        vc_add_villager_position(&vc, vx - 8, 64, vz - 8);
        vc_add_villager_position(&vc, vx + 8, 64, vz - 8);
        vc_add_villager_position(&vc, vx - 8, 64, vz + 8);
        vc_add_villager_position(&vc, vx + 8, 64, vz + 8);
        vc_add_villager_position(&vc, vx, 64, vz - 22);
        vc_add_villager_position(&vc, vx, 64, vz + 22);
        vc_add_villager_position(&vc, vx - 22, 64, vz);
        vc_add_villager_position(&vc, vx + 22, 64, vz);
    }

    /* ------------------------------------------------------------ spawns */
    snprintf(path, sizeof path, "%s/spawns.bin", dir);
    size_t spnlen;
    unsigned char *spawns = read_file(path, &spnlen);
    if (spnlen % SPAWN_BYTES != 0)
    {
        fprintf(stderr, "%s: spawns.bin is %zu bytes\n", dir, spnlen);
        return 2;
    }
    int total_spawns = (int)(spnlen / SPAWN_BYTES);

    trace_open(argc == 3 ? argv[2] : NULL);

    for (int i = 0; i < total_spawns; ++i)
    {
        const unsigned char *s = spawns + (size_t)i * SPAWN_BYTES;
        int idx = (int)le32(s);
        int kind = (int)le32(s + 8);
        double x = le_double(s + 12), y = le_double(s + 20), z = le_double(s + 28);
        float yaw = le_float(s + 36), pitch = le_float(s + 40);
        int growing_age = (int)le32(s + 68);
        int profession = (int)le32(s + 72);
        int wealth = (int)le32(s + 76);
        int player_created = (int)le32(s + 80);
        int spawn_tick = (int)le32(s + 84);

        if (spawn_tick == 0)
        {
            vil_spawn_living(&an, kind == 0 ? VK_VILLAGER : VK_IRON_GOLEM, idx, x, y, z, yaw, pitch,
                             growing_age, profession, wealth, player_created, 1);
        }
    }
    free(spawns);
    an.next_spawn_index = an.n;

    /* ------------------------------------------------------------ player */
    an.has_player = 1;
    an.player_x = -45.0;
    an.player_y = 64.0;
    an.player_z = -45.0;

    double waypoints[8][2] = {
        {-45.0, -45.0},
        {-40.0, -45.0},
        {-40.0, -40.0},
        {-45.0, -40.0},
        {-50.0, -40.0},
        {-50.0, -45.0},
        {-50.0, -50.0},
        {-45.0, -50.0}
    };

    /* ------------------------------------------------------------- ticks */
    snprintf(path, sizeof path, "%s/ticks.bin.gz", dir);
    struct gunzip *tf = gunzip_open(path);
    if (!tf)
    {
        perror(path);
        return 2;
    }

    unsigned char head[16];
    unsigned char vrec[VILLAGE_STATE_BYTES];
    unsigned char bwrec[BLOCK_WRITE_BYTES];
    unsigned char erec[ENT_STATE_BYTES];

    for (int t = 0; t < ticks; ++t)
    {
        trace("tick", "i", t);
        num_tick_writes = 0;

        int64_t wt = timestart + t;
        an.world_time = wt;
        an.skylight = calc_skylight_subtracted(wt);

        vc_tick(&vc);
        vs_tick(&vs);

        if (t < 1700)
        {
            int wp_idx = (t / 40) % 8;
            int next_wp = (wp_idx + 1) % 8;
            double frac = (double)(t % 40) / 40.0;
            an.player_x = waypoints[wp_idx][0] + (waypoints[next_wp][0] - waypoints[wp_idx][0]) * frac;
            an.player_y = 64.0;
            an.player_z = waypoints[wp_idx][1] + (waypoints[next_wp][1] - waypoints[wp_idx][1]) * frac;

            if (t % 120 == 60)
            {
                struct living *nearest_adult = NULL;
                double best_dist = 16.0;
                for (int i = 0; i < an.n; ++i)
                {
                    struct an_ent *en = an_ent_at(an.slot[i]);
                    if (en && en->is_living && en->livh && !lv_get(en->livh)->is_dead &&
                        lv_get(en->livh)->kind == VK_VILLAGER && lv_get(en->livh)->growing_age == 0)
                    {
                        double dx = an.player_x - lv_get(en->livh)->e.pos_x;
                        double dy = an.player_y - lv_get(en->livh)->e.pos_y;
                        double dz = an.player_z - lv_get(en->livh)->e.pos_z;
                        double d = dx * dx + dy * dy + dz * dz;
                        if (d < best_dist)
                        {
                            best_dist = d;
                            nearest_adult = lv_get(en->livh);
                        }
                    }
                }
                if (nearest_adult)
                {
                    villager_get_recipes(nearest_adult, an.shuf_rand);
                    villager_player_trade(nearest_adult, lv_villager(nearest_adult)->recipes.n - 1);
                }
            }
        }
        else
        {
            an.player_x = -45.0;
            an.player_y = 64.0;
            an.player_z = -45.0;
        }

        an_tick(&an, t);

        if (gunzip_read(tf, head, 16) != 16)
        {
            fprintf(stderr, "%s: short read on tick header %d\n", dir, t);
            exit(2);
        }

        int exp_tick = (int)le32(head);
        int exp_num_ents = (int)le32(head + 4);
        int exp_num_villages = (int)le32(head + 8);
        int exp_num_writes = (int)le32(head + 12);

        cmp_int(t, -1, "tick", exp_tick, t);
        cmp_int(t, -1, "num_entities", exp_num_ents, an.n);
        cmp_int(t, -1, "num_villages", exp_num_villages, vc.num_villages);
        cmp_int(t, -1, "num_block_writes", exp_num_writes, num_tick_writes);

        for (int vi = 0; vi < exp_num_villages; ++vi)
        {
            if (gunzip_read(tf, vrec, VILLAGE_STATE_BYTES) != VILLAGE_STATE_BYTES)
            {
                fprintf(stderr, "%s: short read on village state %d tick %d\n", dir, vi, t);
                exit(2);
            }
            if (vi < vc.num_villages)
            {
                struct village *v = &vc.villages[vi];
                cmp_v_int(t, vi, "center_x", (int)le32(vrec), v->center_x);
                cmp_v_int(t, vi, "center_y", (int)le32(vrec + 4), v->center_y);
                cmp_v_int(t, vi, "center_z", (int)le32(vrec + 8), v->center_z);
                cmp_v_int(t, vi, "radius", (int)le32(vrec + 12), v->village_radius);
                cmp_v_int(t, vi, "num_doors", (int)le32(vrec + 16), v->num_doors);
                cmp_v_int(t, vi, "num_villagers", (int)le32(vrec + 20), v->num_villagers);
                cmp_v_int(t, vi, "num_iron_golems", (int)le32(vrec + 24), v->num_iron_golems);
                cmp_v_int(t, vi, "no_breed_ticks", (int)le32(vrec + 28), v->no_breed_ticks);
                cmp_v_int(t, vi, "last_add_door_timestamp", (int)le32(vrec + 32), v->last_add_door_timestamp);
            }
        }

        for (int bwi = 0; bwi < exp_num_writes; ++bwi)
        {
            if (gunzip_read(tf, bwrec, BLOCK_WRITE_BYTES) != BLOCK_WRITE_BYTES)
            {
                fprintf(stderr, "%s: short read on block write %d tick %d\n", dir, bwi, t);
                exit(2);
            }
            if (bwi < num_tick_writes)
            {
                cmp_bw_int(t, bwi, "write_x", (int)le32(bwrec), tick_writes[bwi].x);
                cmp_bw_int(t, bwi, "write_y", (int)le32(bwrec + 4), tick_writes[bwi].y);
                cmp_bw_int(t, bwi, "write_z", (int)le32(bwrec + 8), tick_writes[bwi].z);
                cmp_bw_int(t, bwi, "write_meta", (int)le32(bwrec + 12), tick_writes[bwi].meta);
                cmp_bw_int(t, bwi, "write_flags", (int)le32(bwrec + 16), tick_writes[bwi].flags);
            }
        }

        for (int ei = 0; ei < exp_num_ents; ++ei)
        {
            if (gunzip_read(tf, erec, ENT_STATE_BYTES) != ENT_STATE_BYTES)
            {
                fprintf(stderr, "%s: short read on entity state %d tick %d\n", dir, ei, t);
                exit(2);
            }
            if (ei < an.n)
            {
                struct an_ent *en = an_ent_at(an.slot[ei]);
                if (en && en->is_living && en->livh)
                {
                    check_entity(t, en->spawn_index, lv_get(en->livh), erec);
                }
            }
        }

        if (failures > 20)
        {
            fprintf(stderr, "stopping after %d failures at tick %d\n", failures, t);
            break;
        }
    }

    gunzip_close(tf);
    trace_close();

    if (failures == 0)
    {
        printf("PASS %s: %d ticks ok\n", dir, ticks);
        return 0;
    }
    else
    {
        printf("FAIL %s: %d failures\n", dir, failures);
        return 1;
    }
}
