/* Gate: the native item use against the oracle's ItemUseProbe
 * (oracle/harness/netherite/oracle/ItemUseProbe.java, recorded with
 * `make -C oracle run CLASS=ItemUseProbe`).
 *
 *   test_itemuse PROBE_DIR
 *
 * DIR holds manifest.json (kind "itemuse": seed, cases, cx, cz, radius, ring,
 * the scene list), scenes.bin (per (scene, variant) the setBlock(flag 2) ops
 * that build the scene, anchor-relative), cases.bin (115 bytes per case),
 * writes.bin (15 bytes per write) and sched.bin (34 bytes per scheduled
 * entry).
 *
 * The overworld and the nether load the probe's chunk square, every case
 * rebuilds its scene through world_set_block, poses the player, seeds
 * World.rand and runs itemuse_try_use_item or
 * itemuse_activate_block_or_use_item. The return, the held stack after, the
 * writes, the pending scheduled ticks and World.rand's 48-bit state compare
 * against the record; the first difference stops the run and names the case
 * and both values.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/blockcb.h"
#include "../engine/env.h"
#include "../engine/det.h"
#include "../engine/itemuse.h"
#include "../engine/jmath.h"
#include "../engine/ticks.h"
#include "probe.h"

/* one case, big-endian */
#define CASE_BYTES 115
#define OP_BYTES 7
#define WRITE_BYTES 15
#define SCHED_BYTES 34

#define MAX_SCENES 64

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int32_t be_i32(const unsigned char *p)
{
    return (int32_t)be32(p);
}

static uint64_t be64(const unsigned char *p)
{
    uint64_t v = 0;

    for (int i = 0; i < 8; ++i) v = v << 8 | p[i];

    return v;
}

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static float be_f32(const unsigned char *p)
{
    uint32_t u = be32(p);
    float f;

    memcpy(&f, &u, 4);
    return f;
}

static double be_f64(const unsigned char *p)
{
    uint64_t u = be64(p);
    double d;

    memcpy(&d, &u, 8);
    return d;
}

/* ------------------------------------------------------------- the writes */

struct writes
{
    const unsigned char *base;
    unsigned want, got;
    int overflow, bad;
};

static struct writes cur;
static char diff_what[256];

static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    struct writes *s = ctx;

    if (s->got >= s->want)
    {
        if (!s->overflow)
        {
            s->overflow = 1;
            snprintf(diff_what, sizeof diff_what, "write %u (%d,%d,%d) id %d meta %d: the oracle has only %u writes",
                     s->got + 1, x, y, z, (int16_t)(id & 0xffff), meta, s->want);
        }

        ++s->got;
        return;
    }

    const unsigned char *o = s->base + WRITE_BYTES * (size_t)s->got;
    int wx = be_i32(o), wy = be_i32(o + 4), wz = be_i32(o + 8);
    int wid = (int16_t)be16(o + 12);
    int wmeta = o[14];

    if (!s->bad && (x != wx || y != wy || z != wz || (int16_t)(id & 0xffff) != wid || meta != wmeta))
    {
        s->bad = (int)s->got + 1;
        snprintf(diff_what, sizeof diff_what, "write %u want (%d,%d,%d) id %d meta %d, got (%d,%d,%d) id %d meta %d",
                 s->got + 1, wx, wy, wz, wid, wmeta, x, y, z, (int16_t)(id & 0xffff), meta);
    }

    ++s->got;
}

/* -------------------------------------------------- the scheduled entries */

struct sched
{
    const unsigned char *base;
    unsigned want, got, cap;
    struct tick_entry *ent;
    int bad;
};

static struct sched sched_cur;

static void on_tick(void *ctx, const struct tick_entry *e)
{
    struct sched *s = ctx;

    if (s->got >= s->want)
    {
        ++s->got;
        return;
    }

    if (s->got >= s->cap)
    {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->ent = realloc(s->ent, s->cap * sizeof *s->ent);
    }

    if (s->got < 16) s->ent[s->got] = *e;

    const unsigned char *o = s->base + SCHED_BYTES * (size_t)s->got;

    if (!s->bad &&
        (e->x != be_i32(o) || e->y != be_i32(o + 4) || e->z != be_i32(o + 8) ||
         (e->block & 4095) != be16(o + 12) || e->time != (int64_t)be64(o + 14) ||
         e->priority != be_i32(o + 22) || e->entry != (int64_t)be64(o + 26)))
    {
        s->bad = (int)s->got + 1;
        snprintf(diff_what, sizeof diff_what,
                 "scheduled entry %u want (%d,%d,%d) block %u time %lld pri %d id %lld, got (%d,%d,%d) block %d "
                 "time %lld pri %d id %lld",
                 s->got + 1, be_i32(o), be_i32(o + 4), be_i32(o + 8), be16(o + 12), (long long)(int64_t)be64(o + 14),
                 be_i32(o + 22), (long long)(int64_t)be64(o + 26), e->x, e->y, e->z, e->block & 4095,
                 (long long)e->time, e->priority, (long long)e->entry);
    }

    ++s->got;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    probe_args(&argc, argv);

    if (argc != 2)
    {
        fprintf(stderr, "usage: test_itemuse [--cases N] PROBE_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1024];

    jmath_init();

    /* the block callbacks' Math.random draws (a lava fizz) need a Det stream;
     * the values are not part of the record, the draws just have to land
     * somewhere */
    static det_state det;

    det_init(&det);
    det_reset(&det, 0);
    nw_env->blockcb.env.det = &det;

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    char kind[64];

    manifest_str(manifest, "kind", kind, sizeof kind);

    if (strcmp(kind, "itemuse") != 0)
    {
        printf("SKIP %s: kind %s is not an item use probe\n", dir, kind);
        return 0;
    }

    long long seed = manifest_int(manifest, "seed");
    int cases = (int)manifest_int(manifest, "cases");
    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int radius = (int)manifest_int(manifest, "radius");
    int ring = (int)manifest_int(manifest, "ring");

    /* the manifest's "scenes":[{"name":..., "variants":...}, ...] */
    int nscenes = 0;
    int scene_variants[MAX_SCENES];

    for (const char *p = strstr(manifest, "\"scenes\":["); p != NULL && nscenes < MAX_SCENES; )
    {
        const char *n = strstr(p, "\"name\":\"");
        const char *v = n != NULL ? strstr(n, "\"variants\":") : NULL;

        if (n == NULL || v == NULL) break;

        scene_variants[nscenes] = (int)strtol(v + strlen("\"variants\":"), NULL, 10);
        ++nscenes;

        const char *close = strchr(v, '}');

        if (close == NULL) break;

        p = close;
    }

    if (nscenes == 0)
    {
        fprintf(stderr, "%s: the manifest has no scenes\n", dir);
        return 2;
    }

    int x0 = cx - radius - ring, x1 = cx + radius + ring;
    int z0 = cz - radius - ring, z1 = cz + radius + ring;

    struct world over, nether;

    world_init(&over, seed);
    world_init(&nether, seed);
    nether.dim = -1;

    for (int lx = x0; lx <= x1; ++lx)
    {
        for (int lz = z0; lz <= z1; ++lz)
        {
            world_load_chunk(&over, lx, lz);
            world_load_chunk(&nether, lx, lz);
        }
    }

    /* scenes.bin: per-case ops (v2) or every opset present (v1) */
    snprintf(path, sizeof path, "%s/scenes.bin", dir);
    size_t slen;
    unsigned char *sbuf = probe_read_file(path, &slen);

    int is_v2 = (strstr(manifest, "\"scenes_layout\":\"scenes.bin: per case") != NULL);
    size_t *opoff = NULL;
    uint32_t *opn = NULL;
    size_t sp = 0;

    if (!is_v2)
    {
        int opsets = 0;

        for (int s = 0; s < nscenes; ++s) opsets += scene_variants[s];

        opoff = malloc((size_t)opsets * sizeof *opoff);
        opn = malloc((size_t)opsets * sizeof *opn);

        for (int i = 0; i < opsets; ++i)
        {
            opoff[i] = 0;
            opn[i] = 0;
        }

        for (size_t p = 0; p + 8 <= slen; )
        {
            int si = sbuf[p] | sbuf[p + 1] << 8;
            int vi = sbuf[p + 2] | sbuf[p + 3] << 8;
            uint32_t count = le32(sbuf + p + 4);

            if (si >= nscenes || vi >= scene_variants[si])
            {
                fprintf(stderr, "%s: scenes.bin names scene %d variant %d\n", dir, si, vi);
                return 2;
            }

            int idx = 0;

            for (int s = 0; s < si; ++s) idx += scene_variants[s];

            idx += vi;
            opoff[idx] = p + 8;
            opn[idx] = count;
            p += 8 + (size_t)count * OP_BYTES;
        }
    }

    snprintf(path, sizeof path, "%s/cases.bin", dir);
    size_t clen;
    unsigned char *cbuf = probe_read_file(path, &clen);

    if ((int)(clen / CASE_BYTES) != cases || clen % CASE_BYTES != 0)
    {
        fprintf(stderr, "%s: cases.bin is %zu bytes, the manifest says %d cases\n", dir, clen, cases);
        return 2;
    }

    snprintf(path, sizeof path, "%s/writes.bin", dir);
    size_t wlen;
    unsigned char *wbuf = probe_read_file(path, &wlen);

    snprintf(path, sizeof path, "%s/sched.bin", dir);
    size_t schlen;
    unsigned char *schbuf = probe_read_file(path, &schlen);

    size_t woff = 0, schoff = 0;
    int fail = 0;
    long long total = 0;
    long long ntrue = 0;

    int run = probe_limit(cases);

    for (int i = 0; i < run && !fail; ++i)
    {
        const unsigned char *c = cbuf + CASE_BYTES * (size_t)i;
        int si = be16(c);
        int vi = be16(c + 2);
        int dim = (signed char)c[4];
        int side = c[5];
        int held_item = be16(c + 6);
        int held_count = c[8];
        int held_damage = be16(c + 9);
        int tx = be_i32(c + 11), ty = be_i32(c + 15), tz = be_i32(c + 19);
        float hx = be_f32(c + 23), hy = be_f32(c + 27), hz = be_f32(c + 31);
        double px = be_f64(c + 35), py = be_f64(c + 43), pz = be_f64(c + 51);
        float yaw = be_f32(c + 59), pitch = be_f32(c + 63);
        int allow_edit = c[67], creative = c[68];
        int64_t total_time = (int64_t)be64(c + 69);
        int64_t entry_before = (int64_t)be64(c + 77);
        int64_t rand_seed = (int64_t)be64(c + 85);
        int out_return = c[93];
        int want_item = be16(c + 94);
        int want_count = c[96];
        int want_damage = be16(c + 97);
        uint64_t want_rand = be64(c + 99);
        uint32_t nw = be32(c + 107);
        uint32_t ns = be32(c + 111);

        if (si >= nscenes || vi >= scene_variants[si])
        {
            printf("FAIL %s case %d: scene %d variant %d, the manifest has %d scenes\n", dir, i, si, vi, nscenes);
            ++fail;
            break;
        }

        if (woff + (size_t)nw * WRITE_BYTES > wlen)
        {
            fprintf(stderr, "%s: writes.bin is short at case %d\n", dir, i);
            return 2;
        }

        if (schoff + (size_t)ns * SCHED_BYTES > schlen)
        {
            fprintf(stderr, "%s: sched.bin is short at case %d\n", dir, i);
            return 2;
        }

        struct world *w = dim == -1 ? &nether : &over;

        /* the scene, with the listener off: the oracle records only what the
         * case's own use writes. The anchor is the case's target cell (tx, ty,
         * tz): the probe writes it for a block use and for a look-ray case
         * alike, and the scene ops are anchor-relative. */
        int bx = tx, by = ty, bz = tz;

        w->on_block = NULL;

        if (is_v2)
        {
            if (sp + 4 > slen)
            {
                fprintf(stderr, "%s: scenes.bin is short at case %d\n", dir, i);
                return 2;
            }

            uint32_t count = le32(sbuf + sp);
            sp += 4;

            if (sp + (size_t)count * OP_BYTES > slen)
            {
                fprintf(stderr, "%s: scenes.bin is short for ops at case %d\n", dir, i);
                return 2;
            }

            const unsigned char *ops = sbuf + sp;
            sp += (size_t)count * OP_BYTES;

            for (uint32_t k = 0; k < count; ++k)
            {
                const unsigned char *o = ops + OP_BYTES * k;
                int dx = (signed char)o[0], dy = (signed char)o[1], dz = (signed char)o[2];
                int id = o[3] | o[4] << 8, meta = o[5];

                world_set_block(w, bx + dx, by + dy, bz + dz, id, meta, 2);
            }
        }
        else
        {
            int idx = 0;

            for (int s = 0; s < si; ++s) idx += scene_variants[s];

            idx += vi;

            const unsigned char *ops = sbuf + opoff[idx];

            for (uint32_t k = 0; k < opn[idx]; ++k)
            {
                const unsigned char *o = ops + OP_BYTES * k;
                int dx = (signed char)o[0], dy = (signed char)o[1], dz = (signed char)o[2];
                int id = o[3] | o[4] << 8, meta = o[5];

                world_set_block(w, bx + dx, by + dy, bz + dz, id, meta, 2);
            }
        }

        ticks_reset(entry_before);
        ticks_set_total_time(total_time);

        struct iu_player pl;

        pl.pos_x = px;
        pl.pos_y = py;
        pl.pos_z = pz;
        pl.y_offset = 0.0;
        pl.yaw = yaw;
        pl.pitch = pitch;
        pl.allow_edit = allow_edit;
        pl.creative = creative;

        struct iu_stack held;

        held.item = held_item;
        held.count = held_count;
        held.damage = held_damage;

        jrand wr;

        jr_seed(&wr, rand_seed);

        cur.base = wbuf + woff;
        cur.want = nw;
        cur.got = 0;
        cur.overflow = 0;
        cur.bad = 0;

        sched_cur.base = schbuf + schoff;
        sched_cur.want = ns;
        sched_cur.got = 0;
        sched_cur.bad = 0;

        w->on_block = on_write;
        w->on_block_ctx = &cur;

        int got_return;

        if (side == 255) got_return = itemuse_try_use_item(w, &pl, &held, &wr, NULL);
        else got_return = itemuse_activate_block_or_use_item(w, &pl, &held, bx, by, bz, side, hx, hy, hz, &wr);

        w->on_block = NULL;

        /* ItemFlintAndSteel.onItemUse's damageItem(1, player) runs in its
         * caller (survival.c, surv_damage_stack): a survival player, unenchanted, below the
         * break, one point */
        if (side != 255 && got_return && held.item == 259 && held.count > 0 && !pl.creative) ++held.damage;

        /* the post-null rule processPlayerBlockPlacement applies */
        int got_item = held.item, got_count = held.count, got_damage = held.damage;

        if (got_count == 0)
        {
            got_item = 0;
            got_count = 0;
            got_damage = 0;
        }

        if (got_return != out_return)
        {
            printf("FAIL %s case %d scene %d variant %d: return want %d got %d\n", dir, i, si, vi, out_return,
                   got_return);
            ++fail;
            break;
        }

        if (cur.got != nw)
        {
            printf("FAIL %s case %d scene %d variant %d: %u writes, the oracle recorded %u\n", dir, i, si, vi,
                   cur.got, nw);
            ++fail;
            break;
        }

        if (cur.bad)
        {
            printf("FAIL %s case %d scene %d variant %d: %s\n", dir, i, si, vi, diff_what);
            ++fail;
            break;
        }

        if (got_item != want_item || got_count != want_count || got_damage != want_damage)
        {
            printf("FAIL %s case %d scene %d variant %d: held stack want (%d,%d,%d) got (%d,%d,%d)\n", dir, i, si,
                   vi, want_item, want_count, want_damage, got_item, got_count, got_damage);
            ++fail;
            break;
        }

        sched_cur.cap = 0;
        sched_cur.ent = NULL;
        ticks_walk(on_tick, &sched_cur);

        if (sched_cur.got != ns)
        {
            printf("FAIL %s case %d scene %d variant %d: %u scheduled entries, the oracle recorded %u\n", dir, i, si,
                   vi, sched_cur.got, ns);
            ++fail;
            break;
        }

        if (sched_cur.bad)
        {
            printf("FAIL %s case %d scene %d variant %d: %s\n", dir, i, si, vi, diff_what);
            ++fail;
            break;
        }

        free(sched_cur.ent);
        sched_cur.ent = NULL;

        if ((uint64_t)(wr.seed & 0xffffffffffffULL) != want_rand)
        {
            printf("FAIL %s case %d scene %d variant %d: World.rand state want %012llx got %012llx\n", dir, i, si,
                   vi, (unsigned long long)want_rand, (unsigned long long)(wr.seed & 0xffffffffffffULL));
            ++fail;
            break;
        }

        if (got_return) ++ntrue;
        total += nw;
        woff += (size_t)nw * WRITE_BYTES;
        schoff += (size_t)ns * SCHED_BYTES;
    }

    if (!fail)
        printf("PASS %s: %d%s cases, %lld writes, %lld true returns, seed %lld\n", dir, run,
               run < cases ? " (--cases)" : "", total, ntrue, seed);

    free(wbuf);
    free(schbuf);
    free(cbuf);
    free(sbuf);
    if (opoff) free(opoff);
    if (opn) free(opn);
    free(manifest);
    nw_env->blockcb.env.det = NULL;
    det_free(&det);
    world_free(&over);
    world_free(&nether);
    return fail;
}