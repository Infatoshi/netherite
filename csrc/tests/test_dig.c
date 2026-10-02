/* Gate: the dig replay, native block breaking against the oracle's DigProbe
 * dump (oracle/harness/netherite/oracle/DigProbe.java).
 *
 *   test_dig DIR
 *
 * DIR holds manifest.json, start.bin.gz (the scattered region the cases dig,
 * in load order, tile entities empty: the chest's contents arrive per case),
 * cases.bin (one variable-length record per case: the target, the held stack
 * and its enchants, the pose, the two potion amplifiers, the world Random's
 * and the player Random's seeds, the chest slots and the op bytes),
 * ticks.bin (the destroyBlockInWorldPartially events), writes.bin (the block
 * writes), ents.bin (the spawned item and XP entities), caseout.bin (the held
 * stack after, the exhaustion, the stats, the Det and Random states) and
 * final.bin.gz (the whole region after the last case).
 *
 * The native side loads the region, then replays every case through dig.c
 * the way NetHandlerPlayServer drives a C07: tick 0 onBlockClicked, then the
 * recorded ops with updateBlockRemoving after each. Per case it compares the
 * progress events, the writes, the entities and the caseout record field by
 * field; every 64 cases it hashes the 3x3 chunks around the target, and the
 * whole region is compared after the last case.
 */
#define _POSIX_C_SOURCE 200809L

#include "probe.h"
#include "../engine/det.h"
#include "../engine/dig.h"
#include "../engine/loot.h"
#include "../engine/tileentity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* The whole file, NUL-terminated, or exit. */
static char *read_whole(const char *path)
{
    size_t n;
    char *s = probe_read_file(path, &n);
    return s;
}

/* The kinds, in the manifest's order: the block id of each kind index, for
 * the per-kind tally. */
#define MAX_KINDS 64
static int kind_ids[MAX_KINDS];
static int nkind_ids;

/* The per-kind tally: cases, breaks, cancels, finish packets, item and xp
 * entities, tool breaks. */
static long kind_cases[MAX_KINDS], kind_broke[MAX_KINDS], kind_cancel[MAX_KINDS];
static long kind_finish[MAX_KINDS], kind_items[MAX_KINDS], kind_orbs[MAX_KINDS];
static long kind_toolbreak[MAX_KINDS];

static void scan_kinds(const char *json)
{
    const char *p = strstr(json, "\"kinds\":[");

    if (p == NULL)
    {
        fprintf(stderr, "manifest: no kinds\n");
        exit(2);
    }

    p += strlen("\"kinds\":[");

    while ((p = strstr(p, "\"id\":")) != NULL)
    {
        kind_ids[nkind_ids++] = (int)strtol(p + 5, NULL, 10);

        if (nkind_ids == MAX_KINDS) break;
        p += 5;
    }
}

/* One growable record array; rec is the record size. */
struct recs {
    unsigned char *v;
    size_t n, cap, rec;
};

static void recs_put(struct recs *r, const void *rec)
{
    if (r->n + r->rec > r->cap)
    {
        while (r->n + r->rec > r->cap) r->cap = r->cap ? r->cap * 2 : 1 << 16;
        r->v = realloc(r->v, r->cap);
    }

    memcpy(r->v + r->n, rec, r->rec);
    r->n += r->rec;
}

static void recs_load(struct recs *r, const char *path, size_t rec)
{
    size_t bytes;
    r->v = probe_read_file(path, &bytes);
    r->rec = rec;

    if (bytes % rec != 0)
    {
        fprintf(stderr, "%s: %zu bytes, not a multiple of %zu\n", path, bytes, rec);
        exit(2);
    }

    r->n = bytes;
}

/* One chunk of start.bin.gz into the world: the generated chunk overwritten
 * with the recorded bytes, the skylight columns and the gap flag with them. */
static int load_start_chunk(struct gunzip *g, struct world *w, const unsigned char *cb, int want_cx, int want_cz)
{
    unsigned char head[8], cols[256], gap;

    if (gunzip_read(g, head, 8) != 8 || gunzip_read(g, (void *)cb, CHUNK_BYTES) != CHUNK_BYTES ||
        gunzip_read(g, cols, 256) != 256 || gunzip_read(g, &gap, 1) != 1)
    {
        fprintf(stderr, "start.bin.gz ends early\n");
        return 1;
    }

    int cx = (int)le32(head), cz = (int)le32(head + 4);

    if (cx != want_cx || cz != want_cz)
    {
        fprintf(stderr, "start.bin.gz chunk (%d,%d), the manifest says (%d,%d)\n", cx, cz, want_cx, want_cz);
        return 1;
    }

    /* the recorded bytes replace the generator's, so none is generated:
     * the chunk gets what generation leaves that the bytes do not carry */
    struct chunk *c = world_insert_chunk(w, cx, cz, NULL);
    chunk_cells_in(c, (const uint16_t *)(const void *)cb, cb + 2 * CHUNK_CELLS, cb + 3 * CHUNK_CELLS,
                   cb + 4 * CHUNK_CELLS);

    for (int i = 0; i < 256; ++i)
    {
        c->height[i] = (int32_t)le32(cb + 5 * CHUNK_CELLS + 4 * i);
        c->precip[i] = (int32_t)le32(cb + 5 * CHUNK_CELLS + 1024 + 4 * i);
    }

    c->height_min = (int32_t)le32(cb + 5 * CHUNK_CELLS + 2048);
    c->mask = (uint16_t)(cb[5 * CHUNK_CELLS + 2052] | cb[5 * CHUNK_CELLS + 2053] << 8);

    for (int i = 0; i < 256; ++i) c->update_skylight_columns[i] = cols[i] ? 1 : 0;
    c->gap_lighting_updated = gap ? 1 : 0;
    chunk_count_ticking(c);
    return 0;
}

/* The write listener: the case's Rows.onBlock rows, the dig writes. */
static struct recs writes;

static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    (void)ctx;
    unsigned char r[19];

    put_le32(r, x);
    put_le32(r + 4, y);
    put_le32(r + 8, z);
    r[16] = (unsigned char)id;
    r[17] = (unsigned char)(id >> 8);
    r[18] = (unsigned char)meta;
    recs_put(&writes, r);
}

/* The destroyBlockInWorldPartially events, with the dig tick the drive is
 * on: the capture's tick field. */
static struct recs ticks;
static int cur_tick;

static void on_partial(void *ctx, int casei, int entid, int x, int y, int z, int stage)
{
    (void)ctx;
    (void)casei;
    unsigned char r[22];

    put_le32(r + 4, entid);
    r[8] = (unsigned char)cur_tick;
    put_le32(r + 9, x);
    put_le32(r + 13, y);
    put_le32(r + 17, z);
    r[21] = (unsigned char)stage;
    recs_put(&ticks, r);
}

/* Two rows against each other, for the error line: the field names come out
 * through `why`. */
static int cmp_ent(const unsigned char *o, const dig_ent *e, char *what, size_t n)
{
    if (o[4] != (unsigned char)e->kind)
    {
        snprintf(what, n, "kind want %d got %d", o[4], e->kind);
        return 1;
    }

    int item = o[5] | o[6] << 8, damage = o[7] | o[8] << 8;
    int count = o[9] | o[10] << 8, xp = o[11] | o[12] << 8;
    int eid = (int)le32(o + 13);

    if (item != (e->kind == 1 ? e->item : 0) || damage != (e->kind == 1 ? e->damage : 0)
        || count != (e->kind == 1 ? e->count : 0) || xp != (e->kind == 2 ? e->xp : 0) || eid != e->entity_id)
    {
        snprintf(what, n, "stack (item %d, damage %d, count %d, xp %d, id %d) want "
                 "(%d, %d, %d, %d, %d)", item, damage, count, xp, eid,
                 e->kind == 1 ? e->item : 0, e->kind == 1 ? e->damage : 0,
                 e->kind == 1 ? e->count : 0, e->kind == 2 ? e->xp : 0, e->entity_id);
        return 1;
    }

    static const char *names[6] = { "x", "y", "z", "motion x", "motion y", "motion z" };
    double want[6] = { e->x, e->y, e->z, e->mx, e->my, e->mz };

    for (int i = 0; i < 6; ++i)
    {
        double got;
        memcpy(&got, o + 17 + 8 * i, 8);

        if (got != want[i])
        {
            snprintf(what, n, "%s want %.17g got %.17g", names[i], want[i], got);
            return 1;
        }
    }

    float yaw;
    memcpy(&yaw, o + 65, 4);

    if (yaw != e->yaw)
    {
        snprintf(what, n, "yaw want %.17g got %.17g", (double)e->yaw, (double)yaw);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);

    if (argc != 2)
    {
        fprintf(stderr, "usage: test_dig DIR\n");
        return 2;
    }

    char *dir = argv[1];
    char path[512];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    char *manifest = read_whole(path);

    int64_t seed = manifest_int(manifest, "seed");
    int cases = (int)manifest_int(manifest, "cases");
    int player_id = (int)manifest_int(manifest, "player_entity_id");
    uint64_t chest_start = (uint64_t)manifest_int(manifest, "chest_rand_start");
    scan_kinds(manifest);

    int nchunks;
    int *lcx, *lcz;
    lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    struct world w;
    world_init(&w, seed);

    snprintf(path, sizeof path, "%s/start.bin.gz", dir);
    struct gunzip *g = gunzip_open(path);

    if (g == NULL)
    {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }

    unsigned char *cb = malloc(CHUNK_BYTES);

    for (int i = 0; i < nchunks; ++i)
        if (load_start_chunk(g, &w, cb, lcx[i], lcz[i])) return 2;
    world_owed_last(&w);

    gunzip_close(g);

    /* The Det state the manifest records: the seeder, the Math stream and the
     * entity-ID counter of the OTHER role after the scatter and the player's
     * construction. The Math stream is reseeded per case; the seeder and the
     * counter continue across cases, as the oracle's do. */
    det_state det;
    det_init(&det);
    det.world_seed = seed;
    det.seeder[DET_OTHER].r.seed = (uint64_t)manifest_int(manifest, "det_seeder_start");
    det.math[DET_OTHER].r.seed = (uint64_t)manifest_int(manifest, "det_math_start");
    det.next_id[DET_OTHER] = (int32_t)manifest_int(manifest, "det_nextid_start");

    dig_env d;
    dig_init(&d, &w, &det, chest_start);
    d.p.entity_id = player_id;
    d.partial = on_partial;

    w.on_block = on_write;

    /* The oracle's streams and the dig's own. */
    struct recs cases_r, caseout, ents, writes_o, ticks_o, hashes;
    snprintf(path, sizeof path, "%s/cases.bin", dir);
    recs_load(&cases_r, path, 1);
    snprintf(path, sizeof path, "%s/caseout.bin", dir);
    recs_load(&caseout, path, 52);
    snprintf(path, sizeof path, "%s/ents.bin", dir);
    recs_load(&ents, path, 69);
    snprintf(path, sizeof path, "%s/writes.bin", dir);
    recs_load(&writes_o, path, 19);
    snprintf(path, sizeof path, "%s/ticks.bin", dir);
    recs_load(&ticks_o, path, 22);
    snprintf(path, sizeof path, "%s/hash.bin", dir);
    recs_load(&hashes, path, 12);

    if ((size_t)cases * 52 != caseout.n)
    {
        fprintf(stderr, "%s: caseout has %zu records, the manifest says %d cases\n", dir, caseout.n, cases);
        return 2;
    }

    memset(&writes, 0, sizeof writes);
    writes.rec = 19;
    memset(&ticks, 0, sizeof ticks);
    ticks.rec = 22;

    int fail = 0;
    size_t cpos = 0, epos = 0, tpos = 0, wpos = 0, hpos = 0;

    for (int ci = 0; ci < cases && !fail; ++ci)
    {
        const unsigned char *c = cases_r.v + cpos;
        int kind = (int)le32(c);
        int tx = (int)le32(c + 4), ty = (int)le32(c + 8), tz = (int)le32(c + 12);
        int held = (int)(c[16] | c[17] << 8);
        int count0 = c[18];
        int damage0 = (int)(c[19] | c[20] << 8);
        int eff = c[21], unbr = c[22], silk = c[23], fortune = c[24];
        int on_ground = c[25], in_water = c[26], haste = c[27], fatigue = c[28];
        /* the record's haste and fatigue bytes are the drawn value + 1, and
         * the drawn value is the amplifier + 1 the machine wants */
        int64_t opseed = (int64_t)le64(c + 29), prand_seed = (int64_t)le64(c + 37);
        int chest = c[46];
        int nops = (int)(c[183] | c[184] << 8);
        const unsigned char *ops = c + 185;
        cpos += 185 + nops;

        kind_cases[kind]++;

        dig_case_begin(&d, ci, tx, ty, tz, held, damage0, count0, eff, unbr, silk, fortune,
                       on_ground, in_water, haste - 1, fatigue - 1, opseed, prand_seed);

        /* The chest's contents, exactly as the probe filled them. */
        if (chest)
        {
            struct tile_entity *te = world_tile_entity(&w, tx, ty, tz);

            if (te == NULL || te->kind != TE_CHEST)
            {
                printf("FAIL %s case %d: no chest tile entity at (%d,%d,%d)\n", dir, ci, tx, ty, tz);
                fail = 1;
                break;
            }

            for (int s = 0; s < CHEST_SLOTS; ++s)
            {
                const unsigned char *sl = c + 48 + s * 5;
                struct te_stack *st = &te->u.chest.slots[s];

                st->tag = 0;

                if (sl[4])
                {
                    st->item = (int)(sl[0] | sl[1] << 8);
                    st->damage = sl[2];
                    st->count = sl[3];
                }
                else
                {
                    st->item = -1;
                    st->damage = 0;
                    st->count = 0;
                }
            }
        }

        /* The drive: tick 0 the click, then the recorded ops, one dig tick
         * each. The click packet's side is not recorded: the fire sweep it
         * feeds reads the region and writes nothing. */
        writes.n = 0;
        ticks.n = 0;
        cur_tick = 0;
        dig_op_click(&d, tx, ty, tz, 0);

        for (int i = 0; i < nops; ++i)
        {
            cur_tick = i + 1;

            if (ops[i] == DIG_OP_CANCEL) kind_cancel[kind]++;
            else if (ops[i] == DIG_OP_FINISH) kind_finish[kind]++;

            dig_op(&d, ops[i], tx, ty, tz);
        }

        dig_out out;
        dig_case_end(&d, &out);
        if (out.stat_break) kind_toolbreak[kind]++;
        if (out.broke) kind_broke[kind]++;

        for (int i = 0; i < d.nents; ++i)
        {
            if (d.ents[i].kind == 1) kind_items[kind]++;
            else kind_orbs[kind]++;
        }

        /* The tick rows (the dig progress events), in call order: first,
         * so a dig-speed divergence names the tick it happens on. */
        int want_ticks = 0;

        while (tpos + 22 <= ticks_o.n && (int)le32(ticks_o.v + tpos) == ci)
        {
            ++want_ticks;
            tpos += 22;
        }

        if (want_ticks * 22 != (int)ticks.n)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: %d progress events, the oracle recorded %d\n",
                   dir, ci, tx, ty, tz, kind, (int)ticks.n / 22, want_ticks);
            fail = 1;
            break;
        }

        for (int i = 0; i < want_ticks && !fail; ++i)
        {
            const unsigned char *o = ticks_o.v + (tpos - (size_t)(want_ticks - i) * 22);
            const unsigned char *t = ticks.v + (size_t)i * 22;
            int wt = o[8], ws = (signed char)o[21];
            int gt = t[8], gs = (signed char)t[21];
            int wx = (int)le32(o + 9), wy = (int)le32(o + 13), wz = (int)le32(o + 17);
            int gx = (int)le32(t + 9), gy = (int)le32(t + 13), gz2 = (int)le32(t + 17);

            if (wt != gt || ws != gs || wx != gx || wy != gy || wz != gz2)
            {
                printf("FAIL %s case %d (%d,%d,%d) kind %d: progress event %d: "
                       "tick %d stage %d at (%d,%d,%d), want tick %d stage %d at (%d,%d,%d)\n",
                       dir, ci, tx, ty, tz, kind, i, gt, gs, gx, gy, gz2, wt, ws, wx, wy, wz);
                fail = 1;
            }
        }

        if (fail) break;

        /* The case's entity rows, in spawn order. */
        int want_ents = 0;

        while (epos + 69 <= ents.n && (int)le32(ents.v + epos) == ci) { ++want_ents; epos += 69; }

        if (want_ents != d.nents)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: %d entities, the oracle recorded %d",
                   dir, ci, tx, ty, tz, kind, d.nents, want_ents);

            if (epos + 69 <= ents.n)
                printf(", the next oracle row is case %d", (int)le32(ents.v + epos));
            printf("\n");

            for (int i = 0; i < d.nents; ++i)
                printf("     native ent %d: kind %d item %d damage %d count %d xp %d id %d "
                       "at (%.17g,%.17g,%.17g) motion (%.17g,%.17g,%.17g) yaw %.9g\n",
                       i, d.ents[i].kind, d.ents[i].item, d.ents[i].damage, d.ents[i].count,
                       d.ents[i].xp, d.ents[i].entity_id, d.ents[i].x, d.ents[i].y, d.ents[i].z,
                       d.ents[i].mx, d.ents[i].my, d.ents[i].mz, (double)d.ents[i].yaw);
            fail = 1;
            break;
        }

        for (int i = 0; i < d.nents && !fail; ++i)
        {
            const unsigned char *er = ents.v + epos - (size_t)(d.nents - i) * 69;
            char what[256];

            if (cmp_ent(er, &d.ents[i], what, sizeof what))
            {
                printf("FAIL %s case %d (%d,%d,%d) kind %d: entity %d: %s\n",
                       dir, ci, tx, ty, tz, kind, i, what);
                fail = 1;
            }
        }

        if (fail) break;

        /* The write rows, in write order. */
        int want_writes = 0;

        while (wpos + 19 <= writes_o.n && (int)le32(writes_o.v + wpos) == ci)
        {
            ++want_writes;
            wpos += 19;
        }

        if (want_writes * 19 != (int)writes.n)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: %d block writes, the oracle recorded %d\n",
                   dir, ci, tx, ty, tz, kind, (int)writes.n / 19, want_writes);
            fail = 1;
            break;
        }

        for (int i = 0; i < want_writes && !fail; ++i)
        {
            const unsigned char *o = writes_o.v + (wpos - (size_t)(want_writes - i) * 19);
            const unsigned char *g = writes.v + (size_t)i * 19;
            int wx = (int)le32(o + 4), wy = (int)le32(o + 8), wz = (int)le32(o + 12);
            int wid = (int)(o[16] | o[17] << 8), wmeta = o[18];
            int gx = (int)le32(g), gy = (int)le32(g + 4), gz2 = (int)le32(g + 8);
            int gid = (int)(g[16] | g[17] << 8), gmeta = g[18];

            if (wx != gx || wy != gy || wz != gz2 || wid != gid || wmeta != gmeta)
            {
                printf("FAIL %s case %d (%d,%d,%d) kind %d: write %d at (%d,%d,%d) "
                       "id %d meta %d, want (%d,%d,%d) id %d meta %d\n",
                       dir, ci, tx, ty, tz, kind, i, gx, gy, gz2, gid, gmeta, wx, wy, wz, wid, wmeta);
                fail = 1;
            }
        }

        if (fail) break;

        /* The caseout record. */
        const unsigned char *o = caseout.v + caseout.rec * ci;
        int held1 = (int)(o[0] | o[1] << 8), damage1 = (int)(o[2] | o[3] << 8);
        int count1 = o[4];
        float exhaustion;
        int32_t ebits = (int32_t)le32(o + 5);
        memcpy(&exhaustion, &ebits, 4);
        int stat_mine = o[9], stat_use = o[10], stat_break = o[11];
        uint64_t wr = le64(o + 12), math = le64(o + 20), seeder = le64(o + 28);
        int32_t next_id = (int32_t)le32(o + 36);
        uint64_t chest_state = le64(o + 40);
        int broke = o[48];

        if (held1 != out.held_item)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: held item want %d got %d\n",
                   dir, ci, tx, ty, tz, kind, held1, out.held_item);
            fail = 1;
        }
        else if (damage1 != out.held_damage)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: held damage want %d got %d\n",
                   dir, ci, tx, ty, tz, kind, damage1, out.held_damage);
            fail = 1;
        }
        else if (count1 != out.held_count)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: held count want %d got %d\n",
                   dir, ci, tx, ty, tz, kind, count1, out.held_count);
            fail = 1;
        }
        else if (memcmp(&exhaustion, &out.exhaustion, 4) != 0)
        {
            float got;
            memcpy(&got, &out.exhaustion, 4);
            printf("FAIL %s case %d (%d,%d,%d) kind %d: exhaustion want %.9g got %.9g\n",
                   dir, ci, tx, ty, tz, kind, (double)exhaustion, (double)got);
            fail = 1;
        }
        else if (stat_mine != out.stat_mine || stat_use != out.stat_use || stat_break != out.stat_break)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: stats want (%d,%d,%d) got (%d,%d,%d)\n",
                   dir, ci, tx, ty, tz, kind, stat_mine, stat_use, stat_break,
                   out.stat_mine, out.stat_use, out.stat_break);
            fail = 1;
        }
        else if (wr != out.wr_state)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: world Random state want %016llx got %016llx\n",
                   dir, ci, tx, ty, tz, kind, (unsigned long long)wr, (unsigned long long)out.wr_state);
            fail = 1;
        }
        else if (math != out.math_state)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: Math.random state want %016llx got %016llx\n",
                   dir, ci, tx, ty, tz, kind, (unsigned long long)math, (unsigned long long)out.math_state);
            fail = 1;
        }
        else if (seeder != out.seeder_state)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: seeder state want %016llx got %016llx\n",
                   dir, ci, tx, ty, tz, kind, (unsigned long long)seeder, (unsigned long long)out.seeder_state);
            fail = 1;
        }
        else if (next_id != out.next_id)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: next entity id want %d got %d\n",
                   dir, ci, tx, ty, tz, kind, next_id, out.next_id);
            fail = 1;
        }
        else if (chest_state != out.chest_state)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: chest Random state want %016llx got %016llx\n",
                   dir, ci, tx, ty, tz, kind, (unsigned long long)chest_state,
                   (unsigned long long)out.chest_state);
            fail = 1;
        }
        else if (broke != out.broke)
        {
            printf("FAIL %s case %d (%d,%d,%d) kind %d: broke want %d got %d\n",
                   dir, ci, tx, ty, tz, kind, broke, out.broke);
            fail = 1;
        }

        if (fail) break;

        /* The 3x3 hash, every 64 cases and on the last. */
        if (probe_hash_due(ci, cases))
        {
            const unsigned char *h = hashes.v + hashes.rec * hpos;
            int hcase = (int)le32(h);
            uint64_t want = le64(h + 4);
            uint64_t got = hash_around(&w, tx >> 4, tz >> 4, cb);
            ++hpos;

            if (hcase != ci)
            {
                printf("FAIL %s: hash row %zu is for case %d, expected %d\n", dir, hpos - 1, hcase, ci);
                fail = 1;
            }
            else if (want != got)
            {
                printf("FAIL %s case %d (%d,%d,%d) kind %d: hash of the 3x3 chunks "
                       "want %016llx got %016llx\n",
                       dir, ci, tx, ty, tz, kind, (unsigned long long)want, (unsigned long long)got);
                fail = 1;
            }
        }

        if (fail) break;
    }

    if (!fail)
    {
        /* The whole region after the last case. */
        snprintf(path, sizeof path, "%s/final.bin.gz", dir);
        fail = cmp_final(dir, path, &w, lcx, lcz, nchunks);
    }

    long total_items = 0, total_orbs = 0;

    for (int k = 0; k < nkind_ids; ++k)
    {
        total_items += kind_items[k];
        total_orbs += kind_orbs[k];
    }

    if (!fail)
        printf("PASS %s %d dig cases: %ld items, %ld xp orbs\n", dir, cases, total_items, total_orbs);

    /* The per-kind table, pass or fail. */
    for (int k = 0; k < nkind_ids; ++k)
        printf("kind %2d (block %3d): %ld cases, %ld broke, %ld canceled, %ld finished, "
               "%ld items, %ld orbs, %ld tool breaks\n",
               k, kind_ids[k], kind_cases[k], kind_broke[k], kind_cancel[k], kind_finish[k],
               kind_items[k], kind_orbs[k], kind_toolbreak[k]);

    return fail;
}
