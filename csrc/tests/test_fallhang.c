/* Gate: native falling blocks and hanging entities against the oracle's
 * FallHangProbe dump (oracle/harness/netherite/oracle/FallHangProbe.java). The
 * probe loads a raw region, plays a script over the ticks (scene builds,
 * digs, wall builds and removals, hanging placements through
 * ItemHangingEntity, NBT-loaded falling entities) and ticks the world the
 * way the server ticks it: the total time, the scheduled tick list, then the
 * entity pass. The replay applies the same script through world_set_block
 * and fallhang, runs ticks_tick_updates with the same world Random seed, and
 * compares, per tick: the spawn records, the item drops, the removals, the
 * aux sounds, every live entity's state, and the Det digest with the world
 * Random's state and the pending tick count; every 64th tick and at the end,
 * the region hash and every live entity's canonical NBT; at the end, the
 * Det state and the whole loaded region (final.bin.gz).
 *
 * A failure names the tick, the spawn index and the field. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "probe.h"
#include "../engine/env.h"
#include "../engine/blocks.h"
#include "../engine/fire.h"
#include "../engine/fallhang.h"
#include "../engine/features_springs.h"
#include "../engine/randomtick.h"
#include "../engine/ticks.h"
#include "../engine/world.h"

#define SPAWN_BYTES 86
#define FALL_BYTES 97
#define HANG_BYTES 71
#define DROP_BYTES 82

static int failures = 0;
static int g_tick_now;
static fh_world *g_fw;
static int next_spawn_index;
static FILE *hashes_file;
static gzFile nbt_gz;

static int le16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}

static double le_double(const unsigned char *p)
{
    uint64_t b = le64(p);
    double d;
    memcpy(&d, &b, 8);
    return d;
}

static float le_float(const unsigned char *p)
{
    uint32_t b = le32(p);
    float f;
    memcpy(&f, &b, 4);
    return f;
}

static void fail_rec(int tick, int si, const char *field, const char *want, const char *got)
{
    printf("FAIL tick %d entity %d: %s: the oracle recorded %s, the native replay has %s\n",
           tick, si, field, want, got);
    ++failures;
}

static void cmp_dbl(int tick, int si, const char *field, double want, double got)
{
    if (want == got) return;

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

/* The DetProbe snapshot format, as test_items reads it. */
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

static void update_dispatch(struct world *w, int x, int y, int z, int id, jrand *wr)
{
    /* servertick.c's update_tick, with the falling blocks' spawn routed into
     * the probe's entity list: one virtual Block.updateTick over the world's
     * own Random */
    randomtick_tick_rand(wr);
    int b = id & 4095;

    switch (b)
    {
    case 8:
    case 10:
        liquid_update_tick(w, x, y, z, b, wr);
        break;

    case 9:
    case 11:
        liquid_static_update_tick(w, x, y, z, b, wr);
        break;

    case 12:
    case 13:
    case 145:
    case 122:
        fallhang_update_tick(g_fw, b, x, y, z);
        break;

    case FIRE_BLOCK:
        fire_update_tick(w, wr, x, y, z);
        break;

    default:
        randomtick_update_tick(w, b, wr, wr, x, y, z);
        break;
    }
}

/* WorldServer.tickUpdates(false), servertick.c's consumption over the
 * pending set: the smallest due entries, at most 1000, ticked or
 * re-scheduled by a rejected chunks-exist check. */
static void tick_updates(struct world *w, int64_t total_time, jrand *wr)
{
    int n = ticks_pending_count();

    if (n > 1000) n = 1000;

    struct tick_entry this_tick[1000];
    int nt = 0;

    for (int i = 0; i < n; ++i)
    {
        struct tick_entry e;

        if (!ticks_peek(1, &e)) break;
        if (e.time > total_time) break;

        ticks_pop(&e);
        this_tick[nt++] = e;
    }

    for (int i = 0; i < nt; ++i)
    {
        struct tick_entry e = this_tick[i];

        if (world_check_chunks_exist(w, e.x, e.y, e.z, e.x, e.y, e.z))
        {
            int id = world_get_block(w, e.x, e.y, e.z);

            if (BLOCKS[id & 4095].material != 0 /* air */ && (id & 4095) == (e.block & 4095))
                update_dispatch(w, e.x, e.y, e.z, e.block, wr);
        }
        else
        {
            ticks_schedule_block_update(w, e.x, e.y, e.z, e.block, 0);
        }
    }
}

/* The item drop hook: the oracle assigns the spawn index at its flush, in
 * creation order across items and entities, so the counter is shared with
 * the entity flush. */
static const unsigned char *drop_data;
static size_t drop_len, drop_pos;
static int drop_eof;

static void record_drop(void *ctx, const fh_drop *d)
{
    (void)ctx;
    /* the oracle's flush gives an item the index the next listed entity
     * will take: nextSpawn advances only for listed entities */
    int si = next_spawn_index;

    if (drop_pos + DROP_BYTES > drop_len)
    {
        if (!drop_eof)
        {
            printf("FAIL tick %d: item drops end before the replay's drop (spawn %d id %d)\n",
                   g_tick_now, si, d->entity_id);
            ++failures;
            drop_eof = 1;
        }

        return;
    }

    const unsigned char *r = drop_data + drop_pos;
    drop_pos += DROP_BYTES;

    int wtick = (int)le32(r);
    int wsi = (int)le32(r + 4);
    int weid = (int)le32(r + 8);

    if (wtick != g_tick_now || wsi != si || weid != d->entity_id)
    {
        printf("FAIL tick %d drop: the oracle has (tick %d spawn %d id %d), the replay has (spawn %d id %d)\n",
               g_tick_now, wtick, wsi, weid, si, d->entity_id);
        ++failures;
        return;
    }

    int t = g_tick_now;
    cmp_int(t, si, "drop item", le16(r + 12), d->item);
    cmp_int(t, si, "drop damage", le16(r + 14), d->damage);
    cmp_int(t, si, "drop count", r[16], d->count);
    cmp_dbl(t, si, "drop x", le_double(r + 18), d->x);
    cmp_dbl(t, si, "drop y", le_double(r + 26), d->y);
    cmp_dbl(t, si, "drop z", le_double(r + 34), d->z);
    cmp_dbl(t, si, "drop motionX", le_double(r + 42), d->motion_x);
    cmp_dbl(t, si, "drop motionY", le_double(r + 50), d->motion_y);
    cmp_dbl(t, si, "drop motionZ", le_double(r + 58), d->motion_z);
    cmp_flt(t, si, "drop yaw", le_float(r + 66), d->yaw);
    cmp_flt(t, si, "drop hoverStart", le_float(r + 70), d->hover);
    cmp_int(t, si, "drop delay", (int32_t)le32(r + 74), d->delay);
    cmp_int(t, si, "drop age", (int32_t)le32(r + 78), d->age);
}

struct sfx_rec { int type, x, y, z, v; };
static struct sfx_rec sfx_buf[256];
static int sfx_n;

static void record_sfx(void *ctx, int type, int x, int y, int z, int v)
{
    (void)ctx;

    if (sfx_n < 256)
    {
        sfx_buf[sfx_n].type = type;
        sfx_buf[sfx_n].x = x;
        sfx_buf[sfx_n].y = y;
        sfx_buf[sfx_n].z = z;
        sfx_buf[sfx_n].v = v;
        ++sfx_n;
    }
}

/* The spawn records, indexed by spawn index. */
struct spawn_rec {
    int si, entity_id, kind;
    double x, y, z;
    float yaw;
    int time, block, meta, drop, hurt, hurt_max, ted;
    float hurt_amount;
    int dir, counter, tile_x, tile_y, tile_z, art, rot, item, damage;
};

static struct spawn_rec *spawn_recs;
static int nspawn_recs;

static void cmp_spawn(const fh_ent *en, const struct spawn_rec *sr)
{
    int t = g_tick_now, si = en->spawn_index;
    cmp_int(t, si, "spawn entity id", sr->entity_id, en->entity_id);
    cmp_dbl(t, si, "spawn x", sr->x, en->e.pos_x);
    cmp_dbl(t, si, "spawn y", sr->y, en->e.pos_y);
    cmp_dbl(t, si, "spawn z", sr->z, en->e.pos_z);
    cmp_flt(t, si, "spawn yaw", sr->yaw, en->rotation_yaw);

    if (sr->kind == FH_FALLING)
    {
        cmp_int(t, si, "spawn Time", sr->time, en->time);
        cmp_int(t, si, "spawn block", sr->block, en->block);
        cmp_int(t, si, "spawn meta", sr->meta, en->meta);
        cmp_int(t, si, "spawn DropItem", sr->drop, en->drop_item);
        cmp_int(t, si, "spawn HurtEntities", sr->hurt, en->hurt_entities);
        cmp_flt(t, si, "spawn FallHurtAmount", sr->hurt_amount, en->hurt_amount);
        cmp_int(t, si, "spawn FallHurtMax", sr->hurt_max, en->hurt_max);
        cmp_int(t, si, "spawn TileEntityData", sr->ted, en->tile_entity_data != NULL);
    }
    else
    {
        cmp_int(t, si, "spawn dir", sr->dir, en->dir);
        cmp_int(t, si, "spawn tickCounter1", sr->counter, en->tick_counter1);
        cmp_int(t, si, "spawn tileX", sr->tile_x, en->tile_x);
        cmp_int(t, si, "spawn tileY", sr->tile_y, en->tile_y);
        cmp_int(t, si, "spawn tileZ", sr->tile_z, en->tile_z);
        cmp_int(t, si, "spawn art", sr->art == 0xffff ? -1 : sr->art, en->kind == FH_PAINTING ? en->art : -1);
        cmp_int(t, si, "spawn rot", sr->rot, en->rot);
        cmp_int(t, si, "spawn item", sr->item, en->item);
        cmp_int(t, si, "spawn damage", sr->damage, en->item_damage);
    }
}

/* FNV-1a 64 over the 9x9 chunks from (chx, chz), dx outer 0..8, dz inner
 * 0..8, the setblock probe's chunk bytes; an unloaded chunk is one zero
 * byte, like the oracle's hashRegion. */
static uint64_t hash_region(struct world *w, int chx, int chz, unsigned char *buf)
{
    uint64_t h = FNV_OFFSET;

    for (int dx = 0; dx <= 8; ++dx)
    {
        for (int dz = 0; dz <= 8; ++dz)
        {
            struct chunk *c = world_chunk(w, chx + dx, chz + dz);

            if (!c)
            {
                h *= FNV_PRIME;
                continue;
            }

            chunk_bytes(c, buf);

            for (int i = 0; i < CHUNK_BYTES; ++i) h = (h ^ buf[i]) * FNV_PRIME;
        }
    }

    return h;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);

    if (argc != 2)
    {
        fprintf(stderr, "usage: test_fallhang FALLHANG_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1200];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    if (!manifest)
    {
        printf("skip %s: no manifest\n", dir);
        return 0;
    }

    if (!strstr(manifest, "\"kind\":\"fallhang\""))
    {
        printf("skip %s: not a fallhang dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int radius = (int)manifest_int(manifest, "radius");
    int ticks = (int)manifest_int(manifest, "ticks");
    long long total_time0 = manifest_int(manifest, "totalTime0");
    long long world_rand_seed = manifest_int(manifest, "worldRandSeed");
    int fx0_chunk = cx - radius, fz0_chunk = cz - radius;

    int nchunks;
    int *lcz;
    int *lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    struct world w;
    world_init(&w, seed);

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    /* ------------------------------------------------------------- Det */
    struct ref start;
    read_ref(&start, dir, "start.txt");

    det_state det;
    det_init(&det);
    det_load(&det, start.world_seed, start.seeder, start.math, start.next_id);

    for (int i = 0; i < start.nsplits; ++i)
    {
        det_split_add(&det, start.splits[i].name, start.splits[i].state, start.splits[i].used);
    }

    jrand world_rand;
    jr_seed(&world_rand, world_rand_seed);

    /* ---------------------------------------------------- the script files */
    snprintf(path, sizeof path, "%s/script.bin", dir);
    size_t oplen, len;
    unsigned char *ops_data = probe_read_file(path, &oplen);

    snprintf(path, sizeof path, "%s/hang.bin", dir);
    unsigned char *hang_data = probe_read_file(path, &len);

    snprintf(path, sizeof path, "%s/nbtspawns.bin", dir);
    unsigned char *nbt_data = probe_read_file(path, &len);

    snprintf(path, sizeof path, "%s/spawns.bin", dir);
    unsigned char *spawns_data = probe_read_file(path, &len);
    nspawn_recs = (int)(len / SPAWN_BYTES);
    spawn_recs = calloc((size_t)nspawn_recs, sizeof *spawn_recs);

    for (int i = 0; i < nspawn_recs; ++i)
    {
        const unsigned char *r = spawns_data + (size_t)i * SPAWN_BYTES;
        struct spawn_rec *sr = &spawn_recs[i];
        sr->si = (int)le32(r);
        sr->entity_id = (int)le32(r + 4);
        sr->kind = r[8];
        sr->x = le_double(r + 12);
        sr->y = le_double(r + 20);
        sr->z = le_double(r + 28);
        sr->yaw = le_float(r + 36);
        sr->time = (int)le32(r + 44);
        sr->block = le16(r + 48);
        sr->meta = r[50];
        sr->drop = r[51];
        sr->hurt = r[52];
        sr->hurt_amount = le_float(r + 53);
        sr->hurt_max = (int)le32(r + 57);
        sr->ted = r[61];
        sr->dir = r[62];
        sr->counter = (int)le32(r + 63);
        sr->tile_x = (int)le32(r + 67);
        sr->tile_y = (int)le32(r + 71);
        sr->tile_z = (int)le32(r + 75);
        sr->art = le16(r + 79);
        sr->rot = r[81];
        sr->item = le16(r + 82);
        sr->damage = le16(r + 84);
    }

    free(spawns_data);

    snprintf(path, sizeof path, "%s/itemdrops.bin", dir);
    drop_data = probe_read_file(path, &drop_len);
    drop_pos = 0;

    /* ----------------------------------------------------- the streams */
    snprintf(path, sizeof path, "%s/ticks.bin.gz", dir);
    gzFile tf = gzopen(path, "rb");

    snprintf(path, sizeof path, "%s/removals.txt.gz", dir);
    gzFile rf = gzopen(path, "rb");

    snprintf(path, sizeof path, "%s/sfx.txt.gz", dir);
    gzFile sf = gzopen(path, "rb");

    snprintf(path, sizeof path, "%s/digest.txt.gz", dir);
    gzFile df = gzopen(path, "rb");

    snprintf(path, sizeof path, "%s/hashes.txt", dir);
    hashes_file = fopen(path, "r");

    snprintf(path, sizeof path, "%s/nbt64.txt.gz", dir);
    nbt_gz = gzopen(path, "rb");

    if (!tf || !rf || !sf || !df || !hashes_file || !nbt_gz)
    {
        fprintf(stderr, "%s: a stream is missing\n", dir);
        return 2;
    }

    /* ------------------------------------------------------- the world */
    fh_world fw;
    fh_init(&fw, &w, &det);
    g_fw = &fw;

    struct fh_env env = {0};
    env.ctx = NULL;
    env.drop = record_drop;
    env.aux_sfx = record_sfx;
    nw_env->fallhang.env = env;

    /* per-tick stream state */
    char line[4096];
    int rem_eof = 0, sfx_eof = 0, nbt_eof = 0;
    int rem_tick = -1, rem_si = -1, rem_id = -1, rem_reason = -1, rem_extra = -1;
    double rem_y = 0.0;
    int sfx_tick = -1, sfx_type = -1, sfx_x = -1, sfx_y = -1, sfx_z = -1, sfx_v = -1;
    int nbt_tick = -1;

    if (gzgets(rf, line, sizeof line))
    {
        char yb[32];

        if (sscanf(line, "%d %d %d %d %31s %d", &rem_tick, &rem_si, &rem_id, &rem_reason, yb, &rem_extra) == 6)
        {
            uint64_t bits = strtoull(yb, NULL, 16);
            memcpy(&rem_y, &bits, 8);
        }
        else rem_eof = 1;
    }
    else rem_eof = 1;

    if (gzgets(sf, line, sizeof line))
    {
        if (sscanf(line, "%d %d %d %d %d %d", &sfx_tick, &sfx_type, &sfx_x, &sfx_y, &sfx_z, &sfx_v) != 6)
            sfx_eof = 1;
    }
    else sfx_eof = 1;

    unsigned char recbuf[HANG_BYTES > FALL_BYTES ? HANG_BYTES : FALL_BYTES];

    size_t op_pos = 0, hang_pos = 0, nbt_pos = 0;

    for (int t = 0; t < ticks; ++t)
    {
        g_tick_now = t;
        sfx_n = 0;

        /* WorldServer.tick: the total time, then the tick's script ops */
        ticks_set_total_time(total_time0 + t + 1);

        int op_tick, op_count;
        memcpy(&op_tick, ops_data + op_pos, 4);
        memcpy(&op_count, ops_data + op_pos + 4, 4);
        op_pos += 8;

        for (int i = 0; i < op_count; ++i)
        {
            int kind = ops_data[op_pos];
            int x, y, z;
            memcpy(&x, ops_data + op_pos + 1, 4);
            memcpy(&y, ops_data + op_pos + 5, 4);
            memcpy(&z, ops_data + op_pos + 9, 4);
            int id = le16(ops_data + op_pos + 13);
            int meta = ops_data[op_pos + 15];
            int flags = ops_data[op_pos + 16];
            op_pos += 17;

            if (kind == 1) fh_set_fall_instantly(meta);
            else world_set_block(&w, x, y, z, id, meta, flags);
        }

        int hang_tick, hang_count;
        memcpy(&hang_tick, hang_data + hang_pos, 4);
        memcpy(&hang_count, hang_data + hang_pos + 4, 4);
        hang_pos += 8;

        for (int i = 0; i < hang_count; ++i)
        {
            int kind = hang_data[hang_pos];
            int tx, ty, tz;
            memcpy(&tx, hang_data + hang_pos + 1, 4);
            memcpy(&ty, hang_data + hang_pos + 5, 4);
            memcpy(&tz, hang_data + hang_pos + 9, 4);
            int hdir = hang_data[hang_pos + 13];
            int item = le16(hang_data + hang_pos + 14);
            int damage = le16(hang_data + hang_pos + 16);
            int rot = hang_data[hang_pos + 18];
            hang_pos += 19;

            fh_place_hanging(&fw, kind == 0 ? FH_PAINTING : FH_FRAME, tx, ty, tz, hdir,
                             item, damage, rot);
        }

        int nbt_tick_field, nbt_count;
        memcpy(&nbt_tick_field, nbt_data + nbt_pos, 4);
        memcpy(&nbt_count, nbt_data + nbt_pos + 4, 4);
        nbt_pos += 8;

        for (int i = 0; i < nbt_count; ++i)
        {
            int text_len;
            memcpy(&text_len, nbt_data + nbt_pos, 4);
            nbt_pos += 4;
            char *text = malloc((size_t)text_len + 1);
            memcpy(text, nbt_data + nbt_pos, (size_t)text_len);
            text[text_len] = 0;
            nbt_pos += (size_t)text_len;

            nbt *tag = nbt_parse(text);
            free(text);

            if (!tag)
            {
                printf("FAIL tick %d: the NBT spawn text does not parse\n", t);
                ++failures;
                continue;
            }

            fh_ent *en = fh_spawn_falling_nbt(&fw, tag);
            nbt_free(tag);

            if (en) fh_added_to_world(&fw, en);
        }

        /* the flush after the ops phase: the spawn compare, in list order;
         * unassigned entities are always at the end of the list */
        for (int i = 0; i < fw.n; ++i)
        {
            fh_ent *en = fh_ent_at(fw.slot[i]);

            if (en->spawn_index >= 0) continue;

            en->spawn_index = next_spawn_index++;

            if (en->spawn_index >= nspawn_recs)
            {
                printf("FAIL tick %d: more spawns than the oracle recorded\n", t);
                ++failures;
                break;
            }

            cmp_spawn(en, &spawn_recs[en->spawn_index]);
        }

        (void)nbt_tick_field;

        /* WorldServer.tickUpdates(false) */
        tick_updates(&w, total_time0 + t + 1, &world_rand);

        /* the flush after the scheduled ticks */
        for (int i = 0; i < fw.n; ++i)
        {
            fh_ent *en = fh_ent_at(fw.slot[i]);

            if (en->spawn_index >= 0) continue;

            en->spawn_index = next_spawn_index++;

            if (en->spawn_index >= nspawn_recs)
            {
                printf("FAIL tick %d: more spawns than the oracle recorded\n", t);
                ++failures;
                break;
            }

            cmp_spawn(en, &spawn_recs[en->spawn_index]);
        }

        /* the entity pass, then the removal compare */
        fh_removal rems[FH_MAX_ENTITIES];
        int n_rems = 0;
        fh_tick(&fw, t, rems, FH_MAX_ENTITIES, &n_rems);

        for (int i = 0; i < n_rems; ++i)
        {
            const fh_removal *rv = &rems[i];

            if (rem_eof || rv->tick != rem_tick || rv->spawn_index != rem_si || rv->entity_id != rem_id)
            {
                printf("FAIL tick %d: removal: the oracle has (tick %d spawn %d id %d), the replay has"
                       " (spawn %d id %d)\n", t, rem_eof ? -1 : rem_tick, rem_eof ? -1 : rem_si,
                       rem_eof ? -1 : rem_id, rv->spawn_index, rv->entity_id);
                ++failures;
            }
            else
            {
                cmp_int(t, rv->spawn_index, "removal reason", rem_reason, rv->reason);
                cmp_dbl(t, rv->spawn_index, "removal posY", rem_y, rv->pos_y);
                cmp_int(t, rv->spawn_index, "removal extra", rem_extra, rv->extra);
            }

            if (gzgets(rf, line, sizeof line))
            {
                char yb[32];

                if (sscanf(line, "%d %d %d %d %31s %d", &rem_tick, &rem_si, &rem_id, &rem_reason, yb, &rem_extra) == 6)
                {
                    uint64_t bits = strtoull(yb, NULL, 16);
                    memcpy(&rem_y, &bits, 8);
                }
                else rem_eof = 1;
            }
            else rem_eof = 1;
        }

        /* the pass spawns no entities, only item drops, already recorded */

        /* one state record per live entity, in list order, then the compare */
        for (int i = 0; i < fw.n; ++i)
        {
            fh_ent *en = fh_ent_at(fw.slot[i]);
            int size = en->kind == FH_FALLING ? FALL_BYTES : HANG_BYTES;
            int got = (int)gzread(tf, recbuf, (unsigned)size);

            if (got == 0)
            {
                printf("FAIL tick %d: tick records end before the replay's %d live entities\n", t, fw.n);
                ++failures;
                break;
            }

            if (got != size)
            {
                printf("FAIL tick %d: tick record %d bytes, expected %d\n", t, got, size);
                ++failures;
                break;
            }

            int rt = (int)le32(recbuf);
            int rsi = (int)le32(recbuf + 4);
            int rid = (int)le32(recbuf + 8);

            if (rt != t || rsi != en->spawn_index || rid != en->entity_id)
            {
                printf("FAIL tick %d: record order: the oracle has (tick %d spawn %d id %d), the replay"
                       " has (spawn %d id %d)\n", t, rt, rsi, rid, en->spawn_index, en->entity_id);
                ++failures;
                break;
            }

            int si = en->spawn_index;
            cmp_dbl(t, si, "x", le_double(recbuf + 12), en->e.pos_x);
            cmp_dbl(t, si, "y", le_double(recbuf + 20), en->e.pos_y);
            cmp_dbl(t, si, "z", le_double(recbuf + 28), en->e.pos_z);

            if (en->kind == FH_FALLING)
            {
                cmp_dbl(t, si, "motionX", le_double(recbuf + 36), en->e.motion_x);
                cmp_dbl(t, si, "motionY", le_double(recbuf + 44), en->e.motion_y);
                cmp_dbl(t, si, "motionZ", le_double(recbuf + 52), en->e.motion_z);
                cmp_flt(t, si, "yaw", le_float(recbuf + 60), en->rotation_yaw);
                cmp_flt(t, si, "fallDistance", le_float(recbuf + 64), en->e.fall_distance);
                cmp_int(t, si, "onGround", recbuf[68], en->e.on_ground);
                cmp_int(t, si, "isCollidedHorizontally", recbuf[69], en->e.is_collided_horizontally);
                cmp_int(t, si, "isCollidedVertically", recbuf[70], en->e.is_collided_vertically);
                cmp_int(t, si, "velocityChanged", recbuf[71], en->e.velocity_changed);
                cmp_int(t, si, "Time", (int32_t)le32(recbuf + 72), en->time);
                cmp_int(t, si, "block", le16(recbuf + 76), en->block);
                cmp_int(t, si, "meta", recbuf[78], en->meta);
                cmp_int(t, si, "DropItem", recbuf[79], en->drop_item);
                cmp_int(t, si, "HurtEntities", recbuf[80], en->hurt_entities);
                cmp_flt(t, si, "FallHurtAmount", le_float(recbuf + 81), en->hurt_amount);
                cmp_int(t, si, "FallHurtMax", (int32_t)le32(recbuf + 85), en->hurt_max);
                cmp_int(t, si, "fire", (int32_t)le32(recbuf + 89), en->e.fire);
                cmp_int(t, si, "ticksExisted", (int32_t)le32(recbuf + 93), en->ticks_existed);
            }
            else
            {
                cmp_flt(t, si, "yaw", le_float(recbuf + 36), en->rotation_yaw);
                cmp_int(t, si, "dir", recbuf[40], en->dir);
                cmp_int(t, si, "tickCounter1", (int32_t)le32(recbuf + 44), en->tick_counter1);
                cmp_int(t, si, "tileX", (int32_t)le32(recbuf + 48), en->tile_x);
                cmp_int(t, si, "tileY", (int32_t)le32(recbuf + 52), en->tile_y);
                cmp_int(t, si, "tileZ", (int32_t)le32(recbuf + 56), en->tile_z);
                int want_art = le16(recbuf + 60);
                cmp_int(t, si, "art", want_art == 0xffff ? -1 : want_art,
                        en->kind == FH_PAINTING ? en->art : -1);
                cmp_int(t, si, "rot", recbuf[62], en->rot);
                int want_item = le16(recbuf + 63);
                cmp_int(t, si, "item", want_item == 0xffff ? 0 : want_item, en->item);
                cmp_int(t, si, "damage", le16(recbuf + 65), en->item_damage);
                cmp_int(t, si, "ticksExisted", (int32_t)le32(recbuf + 67), en->ticks_existed);
            }
        }

        /* the sfx compare, in order, against the current sfx line */
        for (int i = 0; i < sfx_n; ++i)
        {
            if (sfx_eof || sfx_tick != t)
            {
                printf("FAIL tick %d: the oracle has no sfx line for the replay's type %d\n", t, sfx_buf[i].type);
                ++failures;
                break;
            }

            cmp_int(t, -1, "sfx type", sfx_type, sfx_buf[i].type);
            cmp_int(t, -1, "sfx x", sfx_x, sfx_buf[i].x);
            cmp_int(t, -1, "sfx y", sfx_y, sfx_buf[i].y);
            cmp_int(t, -1, "sfx z", sfx_z, sfx_buf[i].z);
            cmp_int(t, -1, "sfx value", sfx_v, sfx_buf[i].v);

            if (gzgets(sf, line, sizeof line))
            {
                if (sscanf(line, "%d %d %d %d %d %d", &sfx_tick, &sfx_type, &sfx_x, &sfx_y, &sfx_z, &sfx_v) != 6)
                    sfx_eof = 1;
            }
            else sfx_eof = 1;
        }

        /* a sound the replay did not make */
        if (!sfx_eof && sfx_tick == t)
        {
            printf("FAIL tick %d: the oracle recorded sfx type %d the replay did not make\n", t, sfx_type);
            ++failures;

            if (gzgets(sf, line, sizeof line))
            {
                if (sscanf(line, "%d %d %d %d %d %d", &sfx_tick, &sfx_type, &sfx_x, &sfx_y, &sfx_z, &sfx_v) != 6)
                    sfx_eof = 1;
            }
            else sfx_eof = 1;
        }

        /* the digest line: the Det states, the OTHER next id, the world
         * Random's state and the pending count */
        if (!gzgets(df, line, sizeof line))
        {
            printf("FAIL tick %d: digest line missing\n", t);
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

            /* t <t> then per role: role <r> <seeder> <math> <split>, then
             * nextId <n> worldRand <hex> pending <n>: 2 + 20 + 6 tokens */
            if (n != 28 || strcmp(tok[0], "t") != 0 || atoi(tok[1]) != t)
            {
                printf("FAIL tick %d: digest line has %d tokens, expected 28\n", t, n);
                ++failures;
            }
            else
            {
                for (int role = 0; role < DET_ROLES; ++role)
                {
                    uint64_t a = strtoull(tok[4 + role * 5], NULL, 16);
                    uint64_t b = strtoull(tok[5 + role * 5], NULL, 16);
                    uint64_t c = strtoull(tok[6 + role * 5], NULL, 16);

                    if (det_seeder_state(&det, role) != a)
                    {
                        printf("FAIL tick %d: role %d seeder state: the oracle recorded %016llx, the replay"
                               " has %016llx\n", t, role, (unsigned long long)a,
                               (unsigned long long)det_seeder_state(&det, role));
                        ++failures;
                    }

                    if (det_math_state(&det, role) != b)
                    {
                        printf("FAIL tick %d: role %d math state: the oracle recorded %016llx, the replay"
                               " has %016llx\n", t, role, (unsigned long long)b,
                               (unsigned long long)det_math_state(&det, role));
                        ++failures;
                    }

                    if (det_split_state(&det, role) != c)
                    {
                        printf("FAIL tick %d: role %d split state: the oracle recorded %016llx, the replay"
                               " has %016llx\n", t, role, (unsigned long long)c,
                               (unsigned long long)det_split_state(&det, role));
                        ++failures;
                    }
                }

                cmp_int(t, -1, "next id", (long long)(uint32_t)strtoull(tok[23], NULL, 10),
                        (long long)(uint32_t)det.next_id[DET_OTHER]);
                cmp_int(t, -1, "world rand state", (long long)(uint64_t)strtoull(tok[25], NULL, 16),
                        (long long)(world_rand.seed & 0xffffffffffffULL));
                cmp_int(t, -1, "pending ticks", (long long)strtol(tok[27], NULL, 10),
                        (long long)ticks_pending_count());
            }
        }

        /* the hash and the NBT canon, every 64th tick and at the end */
        if (t % 64 == 63 || t == ticks - 1)
        {
            unsigned char *buf = malloc(CHUNK_BYTES);
            uint64_t got = hash_region(&w, fx0_chunk, fz0_chunk, buf);
            free(buf);

            if (fgets(line, sizeof line, hashes_file))
            {
                int want_tick;
                char hexbuf[32];

                if (sscanf(line, "t %d hash %16s", &want_tick, hexbuf) == 2)
                {
                    if (want_tick != t)
                    {
                        printf("FAIL: the oracle's hash line is for tick %d, the replay is at %d\n", want_tick, t);
                        ++failures;
                    }

                    uint64_t want = strtoull(hexbuf, NULL, 16);

                    if (want != got)
                    {
                        printf("FAIL tick %d: region hash: the oracle recorded %016llx, the replay has %016llx\n",
                               t, (unsigned long long)want, (unsigned long long)got);
                        ++failures;
                    }
                }
                else
                {
                    printf("FAIL tick %d: bad hash line: %s", t, line);
                    ++failures;
                }
            }
            else
            {
                printf("FAIL tick %d: the oracle's hash lines ended early\n", t);
                ++failures;
            }

            /* the canonical NBT of every live entity, in list order */
            if (!gzgets(nbt_gz, line, sizeof line) || sscanf(line, "t %d", &nbt_tick) != 1 || nbt_tick != t)
            {
                printf("FAIL tick %d: the oracle's nbt64 header is missing or not for this tick\n", t);
                ++failures;
                nbt_eof = 1;
            }

            if (!nbt_eof)
            {
                for (int i = 0; i < fw.n; ++i)
                {
                    if (!gzgets(nbt_gz, line, sizeof line))
                    {
                        printf("FAIL tick %d: the oracle's nbt64 lines ended at entity %d\n", t, i);
                        ++failures;
                        nbt_eof = 1;
                        break;
                    }

                    char nl[8192];
                    nbt *tag = fh_write_nbt(fh_ent_at(fw.slot[i]));
                    char *text = nbt_render(tag);
                    nbt_free(tag);
                    snprintf(nl, sizeof nl, "%d %s\n", fh_ent_at(fw.slot[i])->spawn_index, text);
                    free(text);

                    if (strcmp(line, nl) != 0)
                    {
                        printf("FAIL tick %d: NBT of entity %d:\n  want %s  got  %s", t,
                               fh_ent_at(fw.slot[i])->spawn_index, line, nl);
                        ++failures;
                    }
                }
            }
        }
    }

    /* ------------------------------------------------------------- the end */
    /* the Det state after the last tick */
    struct ref end;
    read_ref(&end, dir, "end.txt");

    for (int role = 0; role < DET_ROLES; ++role)
    {
        if (det_seeder_state(&det, role) != end.seeder[role])
        {
            printf("FAIL: end role %d seeder state: the oracle recorded %016llx, the replay has %016llx\n",
                   role, (unsigned long long)end.seeder[role], (unsigned long long)det_seeder_state(&det, role));
            ++failures;
        }

        if (det_math_state(&det, role) != end.math[role])
        {
            printf("FAIL: end role %d math state: the oracle recorded %016llx, the replay has %016llx\n",
                   role, (unsigned long long)end.math[role], (unsigned long long)det_math_state(&det, role));
            ++failures;
        }

        if (det_split_state(&det, role) != end.split[role])
        {
            printf("FAIL: end role %d split state: the oracle recorded %016llx, the replay has %016llx\n",
                   role, (unsigned long long)end.split[role], (unsigned long long)det_split_state(&det, role));
            ++failures;
        }
    }

    /* the whole loaded region, in load order */
    snprintf(path, sizeof path, "%s/final.bin.gz", dir);

    if (cmp_final(dir, path, &w, lcx, lcz, nchunks)) ++failures;

    gzclose(tf);
    gzclose(rf);
    gzclose(sf);
    gzclose(df);
    fclose(hashes_file);
    gzclose(nbt_gz);
    free(ops_data);
    free(hang_data);
    free(nbt_data);
    free(spawn_recs);
    fh_free(&fw);
    world_free(&w);
    free(manifest);

    if (!failures)
    {
        printf("PASS %s: %d ticks, %d spawns, %d chunks, seed %lld\n", dir, ticks, nspawn_recs, nchunks,
               (long long)seed);
    }
    else
    {
        printf("%s: %d failures\n", dir, failures);
    }

    return failures != 0;
}
