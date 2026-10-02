/* Gate: the activation replay, the first half of ItemInWorldManager
 * .activateBlockOrUseItem against the oracle's ActivateProbe dump
 * (oracle/harness/netherite/oracle/ActivateProbe.java).
 *
 *   test_activate DIR
 *
 * DIR holds manifest.json, start.bin.gz (the scattered region the cases
 * activate, in load order), cases.bin (one 104-byte record per case: the
 * kind, the target, the half, the side and hit offsets, the pose, the
 * sneaking and day flags, the food stats, the inventory fullness, the meta
 * and tile entity state, the held stack and the world Random's seed),
 * caseout.bin (the return, the chat, the GUI flag, the held stack and food
 * after, the streams), invs.bin (the player's 36 main slots after),
 * writes.bin (the block writes), events.bin (the block events), ticks.bin
 * (the newly scheduled tick entries), ents.bin (the spawned item entities)
 * and final.bin.gz (the whole region after the last case).
 *
 * The native side loads the region, then replays every case through
 * activate.c the way NetHandlerPlayServer drives a C08: the site's setup
 * with the listener off, then the call with it on, then the GUI close. Per
 * case it compares the events, the spawned entities, the writes, the
 * inventory and the caseout record field by field; every 64 cases it hashes
 * the 3x3 chunks around the target, and the whole region is compared after
 * the last case.
 */
#define _POSIX_C_SOURCE 200809L

#include "probe.h"
#include "../engine/snapshot.h"
#include "../engine/activate.h"
#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/ticks.h"

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

/* The per-kind tally: cases, returns true, GUIs, chats, events, entities. */
static long kind_cases[MAX_KINDS], kind_true[MAX_KINDS], kind_gui[MAX_KINDS];
static long kind_chat[MAX_KINDS], kind_events[MAX_KINDS], kind_ents[MAX_KINDS];

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

/* The write listener: the case's Rows.onBlock rows. */
static struct recs writes_o, writes;
static int cur_case;

static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    (void)ctx;
    unsigned char r[19];

    put_le32(r, cur_case);
    put_le32(r + 4, x);
    put_le32(r + 8, y);
    put_le32(r + 12, z);
    r[16] = (unsigned char)id;
    r[17] = (unsigned char)(id >> 8);
    r[18] = (unsigned char)meta;
    recs_put(&writes, r);
}

/* Two rows against each other, for the error line. */
static int cmp_ent(const unsigned char *o, const act_ent *e, char *what, size_t n)
{
    int item = o[5] | o[6] << 8, damage = o[7] | o[8] << 8;
    int count = o[9] | o[10] << 8;
    int eid = (int)le32(o + 13);

    if (item != e->item || damage != e->damage || count != e->count || eid != e->entity_id)
    {
        snprintf(what, n, "stack (item %d, damage %d, count %d, id %d) want (%d, %d, %d, %d)",
                 item, damage, count, eid, e->item, e->damage, e->count, e->entity_id);
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


/* The machine's new tick entries, gathered in tree order. */
struct tick_gather { int64_t id_before; struct tick_entry v[512]; int n; };
static struct tick_gather gather;

static void tick_gather_cb(void *ctx, const struct tick_entry *e)
{
    struct tick_gather *g = ctx;

    if (g->n < 512 && e->entry >= g->id_before) g->v[g->n++] = *e;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);

    if (argc != 2)
    {
        fprintf(stderr, "usage: test_activate DIR\n");
        return 2;
    }

    char *dir = argv[1];
    char path[512];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    char *manifest = read_whole(path);

    int64_t seed = manifest_int(manifest, "seed");
    int cases = (int)manifest_int(manifest, "cases");
    int dim = (int)manifest_int(manifest, "dim");
    int64_t total_time = manifest_int(manifest, "total_time");
    int64_t tick_start = manifest_int(manifest, "next_tick_entry_start");
    scan_kinds(manifest);

    int nchunks;
    int *lcx, *lcz;
    lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    struct world w;
    world_init(&w, seed);
    w.dim = dim;

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

    /* The scatter placed every site's blocks with setBlock, whose
     * Chunk.func_150807_a gives a container its tile entity, all before the
     * recorded start. The region's bytes carry the blocks but not the chunk
     * maps, so the entities are made here, before the case environment is
     * live: their constructor Randoms (the dispenser's) were spent before
     * det_seeder_start, so they draw nothing now. */
    for (int i = 0; i < nchunks; ++i)
        for (int y = 0; y < 256; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x)
                {
                    int bx = lcx[i] * 16 + x, bz = lcz[i] * 16 + z;

                    if (BLOCKS[world_get_block(&w, bx, y, bz) & 4095].tile_entity) world_tile_entity(&w, bx, y, bz);
                }

    /* The Det state the manifest records: the seeder and the entity-ID
     * counter of the OTHER role after the scatter and the players'
     * construction. The Math stream is reseeded per case. */
    det_state det;
    det_init(&det);
    det.world_seed = seed;
    det.seeder[DET_OTHER].r.seed = (uint64_t)manifest_int(manifest, "det_seeder_start");
    det.math[DET_OTHER].r.seed = (uint64_t)manifest_int(manifest, "det_math_start");
    det.next_id[DET_OTHER] = (int32_t)manifest_int(manifest, "det_nextid_start");

    act_env d;
    act_init(&d, &w, &det, total_time, tick_start);

    d.can_respawn = dim == 0;

    /* The oracle's streams and the machine's own. */
    struct recs cases_r, caseout, invs, ents, events_o, ticks_o, hashes;
    snprintf(path, sizeof path, "%s/cases.bin", dir);
    recs_load(&cases_r, path, 1);
    snprintf(path, sizeof path, "%s/caseout.bin", dir);
    recs_load(&caseout, path, 116);
    snprintf(path, sizeof path, "%s/invs.bin", dir);
    recs_load(&invs, path, 180);
    snprintf(path, sizeof path, "%s/ents.bin", dir);
    recs_load(&ents, path, 69);
    snprintf(path, sizeof path, "%s/events.bin", dir);
    recs_load(&events_o, path, 20);
    snprintf(path, sizeof path, "%s/writes.bin", dir);
    recs_load(&writes_o, path, 19);
    snprintf(path, sizeof path, "%s/ticks.bin", dir);
    recs_load(&ticks_o, path, 40);
    snprintf(path, sizeof path, "%s/hash.bin", dir);
    recs_load(&hashes, path, 12);

    if ((size_t)cases * 116 != caseout.n)
    {
        fprintf(stderr, "%s: caseout has %zu records, the manifest says %d cases\n", dir, caseout.n, cases);
        return 2;
    }

    memset(&writes, 0, sizeof writes);
    writes.rec = 19;

    w.on_block = on_write;
    d.on_write = NULL;

    int fail = 0;
    size_t wpos = 0, epos = 0, evpos = 0, tpos = 0, hpos = 0;

    for (int ci = 0; ci < cases && !fail; ++ci)
    {
        const unsigned char *c = cases_r.v + (size_t)ci * 104;
        act_case k;

        memset(&k, 0, sizeof k);
        k.kind = (int)le32(c);
        k.x = (int)le32(c + 4);
        k.y = (int)le32(c + 8);
        k.z = (int)le32(c + 12);
        k.half = c[16];
        k.side = c[17];
        memcpy(&k.vx, c + 18, 4);
        memcpy(&k.vy, c + 22, 4);
        memcpy(&k.vz, c + 26, 4);
        memcpy(&k.yaw, c + 30, 4);
        memcpy(&k.px, c + 34, 8);
        memcpy(&k.py, c + 42, 8);
        memcpy(&k.pz, c + 50, 8);
        k.sneak = c[58];
        k.day = c[59];
        k.food_level = c[60];
        memcpy(&k.food_sat, c + 61, 4);
        k.inv_full = c[65];
        k.meta = c[66];
        k.bed_dir = c[67];
        k.foot_occ = c[68];
        k.head_occ = c[69];
        k.pot_full = c[70];
        k.pot_item = (int)(c[71] | c[72] << 8);
        k.pot_data = c[73];
        k.jmeta = c[74];
        k.jdisc = (int)(c[75] | c[76] << 8);
        k.note_pitch = c[77];
        k.held_item = (int)(c[78] | c[79] << 8);
        k.held_damage = (int)(c[80] | c[81] << 8);
        k.held_count = c[82];
        k.opseed = (int64_t)le64(c + 83);
        k.dummy_sleeping = c[91];
        k.chest_blocked = c[94];

        int kind = k.kind;
        kind_cases[kind]++;

        writes.n = 0;
        cur_case = ci;
        act_case_begin(&d, &k);

        /* the setup's own scheduled ticks carry smaller entry ids, what the
         * probe's pending list walk skips (its idBefore comes from after the
         * setup) */
        int64_t id_before = ticks_next_entry_id();
        gather.id_before = id_before;

        int ret = act_run(&d, &k);
        act_out out;
        act_case_end(&d, &out);
        out.ret = ret;

        /* The case's event rows, in add order. */
        int want_events = 0;

        while (evpos + 20 <= events_o.n && (int)le32(events_o.v + evpos) == ci)
        {
            ++want_events;
            evpos += 20;
        }

        if (want_events != d.events.n)
        {
            printf("FAIL %s case %d kind %d: %d block events, the oracle recorded %d\n",
                   dir, ci, k.kind, d.events.n, want_events);
            fail = 1;
            break;
        }

        for (int i = 0; i < want_events && !fail; ++i)
        {
            const unsigned char *o = events_o.v + (evpos - (size_t)(want_events - i) * 20);
            const int32_t *g = d.events.v[i];
            int bx = (int)le32(o + 4), by = (int)le32(o + 8), bz = (int)le32(o + 12);
            int bb = (int)(o[16] | o[17] << 8);

            if (bx != g[0] || by != g[1] || bz != g[2] || bb != g[3]
                || o[18] != (unsigned char)g[4] || o[19] != (unsigned char)g[5])
            {
                printf("FAIL %s case %d kind %d: event at (%d,%d,%d) block %d id %d param %d,"
                       " want (%d,%d,%d) block %d id %d param %d\n",
                       dir, ci, k.kind, g[0], g[1], g[2], g[3], g[4], g[5],
                       bx, by, bz, bb, o[18], o[19]);
                fail = 1;
            }
        }

        if (fail) break;

        /* The case's entity rows, in spawn order. */
        int want_ents = 0;

        while (epos + 69 <= ents.n && (int)le32(ents.v + epos) == ci) { ++want_ents; epos += 69; }

        if (want_ents != d.nents)
        {
            fprintf(stderr, "machine ents:");
            for (int i = 0; i < d.nents; ++i) fprintf(stderr, " %d", d.ents[i].item);
            fprintf(stderr, "\n");
            fprintf(stderr, "machine writes:");
            for (size_t i = 0; i < writes.n / 19; ++i)
                fprintf(stderr, " (%d,%d,%d)%d/%d", (int)le32(writes.v + i * 19 + 4),
                        (int)le32(writes.v + i * 19 + 8), (int)le32(writes.v + i * 19 + 12),
                        (int)(writes.v[i * 19 + 16] | writes.v[i * 19 + 17] << 8), writes.v[i * 19 + 18]);
            fprintf(stderr, "\n");
            fprintf(stderr, "machine wr=%016llx oracle wr=%016llx\n",
                    (unsigned long long)out.wr_state,
                    (unsigned long long)le64(caseout.v + caseout.rec * (size_t)ci + 75));

            printf("FAIL %s case %d kind %d: %d entities, the oracle recorded %d\n",
                   dir, ci, k.kind, d.nents, want_ents);
            fail = 1;
            break;
        }


        for (int i = 0; i < d.nents && !fail; ++i)
        {
            const unsigned char *er = ents.v + (epos - (size_t)(want_ents - i) * 69);
            char what[256];

            if (cmp_ent(er, &d.ents[i], what, sizeof what))
            {
                printf("FAIL %s case %d kind %d: entity %d: %s\n", dir, ci, k.kind, i, what);
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
            printf("FAIL %s case %d kind %d: %d block writes, the oracle recorded %d\n",
                   dir, ci, k.kind, (int)writes.n / 19, want_writes);
            fail = 1;
            break;
        }

        for (int i = 0; i < want_writes && !fail; ++i)
        {
            const unsigned char *o = writes_o.v + (wpos - (size_t)(want_writes - i) * 19);
            const unsigned char *gg = writes.v + (size_t)i * 19;
            int wx = (int)le32(o + 4), wy = (int)le32(o + 8), wz = (int)le32(o + 12);
            int wid = (int)(o[16] | o[17] << 8), wmeta = o[18];
            int gx = (int)le32(gg + 4), gy = (int)le32(gg + 8), gz2 = (int)le32(gg + 12);
            int gid = (int)(gg[16] | gg[17] << 8), gmeta = gg[18];

            if (wx != gx || wy != gy || wz != gz2 || wid != gid || wmeta != gmeta)
            {
                printf("FAIL %s case %d kind %d: write %d at (%d,%d,%d) id %d meta %d,"
                       " want (%d,%d,%d) id %d meta %d\n",
                       dir, ci, k.kind, i, gx, gy, gz2, gid, gmeta, wx, wy, wz, wid, wmeta);
                fail = 1;
            }
        }

        if (fail) break;

        /* The tick rows: the case's new entries, in tree order. */
        int want_ticks = 0;

        while (tpos + 40 <= ticks_o.n && (int)le32(ticks_o.v + tpos) == ci)
        {
            ++want_ticks;
            tpos += 40;
        }

        /* the machine's new entries, in tree order */
        struct tick_gather gather;
        gather.id_before = id_before;
        gather.n = 0;
        ticks_walk(tick_gather_cb, &gather);

        if (want_ticks != gather.n)
        {
            printf("FAIL %s case %d kind %d: %d new tick entries, the oracle recorded %d\n",
                   dir, ci, k.kind, gather.n, want_ticks);
            fail = 1;
            break;
        }

        for (int i = 0; i < want_ticks && !fail; ++i)
        {
            const unsigned char *o = ticks_o.v + (tpos - (size_t)(want_ticks - i) * 40);
            const struct tick_entry *te = &gather.v[i];

            int tx2 = (int)le32(o + 4), ty2 = (int)le32(o + 8), tz2 = (int)le32(o + 12);
            int tb = (int)(o[16] | o[17] << 8);
            int64_t tt = (int64_t)le64(o + 18);
            int tp2 = (int32_t)le32(o + 26);
            int64_t te2 = (int64_t)le64(o + 30);

            if (tx2 != te->x || ty2 != te->y || tz2 != te->z || tb != te->block
                || tt != te->time || tp2 != te->priority || te2 != te->entry)
            {
                printf("FAIL %s case %d kind %d: tick entry %d want (%d,%d,%d) block %d "
                       "time %lld prio %d entry %lld, got (%d,%d,%d) block %d time %lld prio %d entry %lld\n",
                       dir, ci, k.kind, i, tx2, ty2, tz2, tb, (long long)tt, tp2, (long long)te2,
                       te->x, te->y, te->z, te->block, (long long)te->time, te->priority, (long long)te->entry);
                fail = 1;
            }
        }

        if (fail) break;

        /* The invs record: the player's 36 main slots after. */
        const unsigned char *ir = invs.v + (size_t)ci * 180;

        for (int s = 0; s < 36 && !fail; ++s)
        {
            int item = (int)(ir[s * 5] | ir[s * 5 + 1] << 8);
            int dmg = (int)(ir[s * 5 + 2] | ir[s * 5 + 3] << 8);
            int cnt = ir[s * 5 + 4];
            struct craft_stack *st = &d.player.slot[s];
            int gi = craft_slot_empty(st) ? 0 : st->item;
            int gd = craft_slot_empty(st) ? 0 : st->damage;
            int gc = craft_slot_empty(st) ? 0 : st->count;

            if (item != gi || dmg != gd || cnt != gc)
            {
                printf("FAIL %s case %d kind %d: inventory slot %d want (%d,%d,%d) got (%d,%d,%d)\n",
                       dir, ci, k.kind, s, item, dmg, cnt, gi, gd, gc);
                fail = 1;
            }
        }

        if (fail) break;

        /* The caseout record, field by field. */
        const unsigned char *o = caseout.v + caseout.rec * (size_t)ci;
        float sat, exh;
        memcpy(&sat, o + 10, 4);
        memcpy(&exh, o + 14, 4);
        int chest_num = (int32_t)le32(o + 43), chest_pair_num = (int32_t)le32(o + 47);
        int pot_item = (int32_t)le32(o + 51), pot_data = (int32_t)le32(o + 55);
        int note_pitch = (int32_t)le32(o + 59);
        int disc_item = (int32_t)le32(o + 63), disc_count = (int32_t)le32(o + 67);
        int cmp_out = (int32_t)le32(o + 71);
        uint64_t wr = le64(o + 75), math = le64(o + 83), seeder = le64(o + 91);
        int32_t next_id = (int32_t)le32(o + 99);
        int64_t tick_entry = (int64_t)le64(o + 103);
        int held1 = (int)(o[4] | o[5] << 8), damage1 = (int)(o[6] | o[7] << 8);
        int count1 = o[8];

        if (o[0] != (unsigned char)ret)
        {
            printf("FAIL %s case %d kind %d: return want %d got %d\n", dir, ci, k.kind, o[0], ret);
            fail = 1;
        }
        else if (o[1] != (unsigned char)d.chat)
        {
            printf("FAIL %s case %d kind %d: chat want %d got %d\n", dir, ci, k.kind, o[1], d.chat);
            fail = 1;
        }
        else if (o[2] != (unsigned char)out.gui)
        {
            printf("FAIL %s case %d kind %d: gui want %d got %d\n", dir, ci, k.kind, o[2], out.gui);
            fail = 1;
        }
        else if (o[3] != (unsigned char)d.stat_use)
        {
            printf("FAIL %s case %d kind %d: stat_use want %d got %d\n", dir, ci, k.kind, o[3], d.stat_use);
            fail = 1;
        }
        else if (held1 != out.held_item)
        {
            printf("FAIL %s case %d kind %d: held item want %d got %d\n", dir, ci, k.kind, held1, out.held_item);
            fail = 1;
        }
        else if (damage1 != out.held_damage)
        {
            printf("FAIL %s case %d kind %d: held damage want %d got %d\n", dir, ci, k.kind, damage1, out.held_damage);
            fail = 1;
        }
        else if (count1 != out.held_count)
        {
            printf("FAIL %s case %d kind %d: held count want %d got %d\n", dir, ci, k.kind, count1, out.held_count);
            fail = 1;
        }
        else if (o[9] != (unsigned char)out.food_level)
        {
            printf("FAIL %s case %d kind %d: food level want %d got %d\n", dir, ci, k.kind, o[9], out.food_level);
            fail = 1;
        }
        else if (memcmp(&sat, &out.food_sat, 4) != 0 || memcmp(&exh, &out.food_exh, 4) != 0)
        {
            float gs, ge;
            memcpy(&gs, &out.food_sat, 4);
            memcpy(&ge, &out.food_exh, 4);
            printf("FAIL %s case %d kind %d: food want (%.9g, %.9g) got (%.9g, %.9g)\n",
                   dir, ci, k.kind, (double)sat, (double)exh, (double)gs, (double)ge);
            fail = 1;
        }
        else if (o[18] != (unsigned char)out.sleeping)
        {
            printf("FAIL %s case %d kind %d: sleeping want %d got %d\n", dir, ci, k.kind, o[18], out.sleeping);
            fail = 1;
        }
        else
        {
            double opx, opy, opz;
            memcpy(&opx, o + 19, 8);
            memcpy(&opy, o + 27, 8);
            memcpy(&opz, o + 35, 8);

            if (opx != out.px || opy != out.py || opz != out.pz)
            {
                printf("FAIL %s case %d kind %d: player pos want (%.17g,%.17g,%.17g) "
                       "got (%.17g,%.17g,%.17g)\n",
                       dir, ci, k.kind, (double)opx, (double)opy, (double)opz,
                       (double)out.px, (double)out.py, (double)out.pz);
                fail = 1;
            }
        }

        if (!fail && chest_num != out.chest_num)
        {
            printf("FAIL %s case %d kind %d: chest num want %d got %d\n", dir, ci, k.kind, chest_num, out.chest_num);
            fail = 1;
        }
        else if (!fail && chest_pair_num != out.chest_pair_num)
        {
            printf("FAIL %s case %d kind %d: chest pair num want %d got %d\n",
                   dir, ci, k.kind, chest_pair_num, out.chest_pair_num);
            fail = 1;
        }
        else if (!fail && (pot_item != out.pot_item || pot_data != out.pot_data))
        {
            printf("FAIL %s case %d kind %d: pot want (%d,%d) got (%d,%d)\n",
                   dir, ci, k.kind, pot_item, pot_data, out.pot_item, out.pot_data);
            fail = 1;
        }
        else if (!fail && note_pitch != out.note_pitch)
        {
            printf("FAIL %s case %d kind %d: note pitch want %d got %d\n", dir, ci, k.kind, note_pitch, out.note_pitch);
            fail = 1;
        }
        else if (!fail && (disc_item != out.disc_item || disc_count != out.disc_count))
        {
            printf("FAIL %s case %d kind %d: disc want (%d,%d) got (%d,%d)\n",
                   dir, ci, k.kind, disc_item, disc_count, out.disc_item, out.disc_count);
            fail = 1;
        }
        else if (!fail && cmp_out != out.cmp_out)
        {
            printf("FAIL %s case %d kind %d: comparator out want %d got %d\n", dir, ci, k.kind, cmp_out, out.cmp_out);
            fail = 1;
        }
        else if (!fail && wr != out.wr_state)
        {
            printf("FAIL %s case %d kind %d: world Random state want %016llx got %016llx\n",
                   dir, ci, k.kind, (unsigned long long)wr, (unsigned long long)out.wr_state);
            fail = 1;
        }
        else if (!fail && math != out.math_state)
        {
            printf("FAIL %s case %d kind %d: Math.random state want %016llx got %016llx\n",
                   dir, ci, k.kind, (unsigned long long)math, (unsigned long long)out.math_state);
            fail = 1;
        }
        else if (!fail && seeder != out.seeder_state)
        {
            printf("FAIL %s case %d kind %d: seeder state want %016llx got %016llx\n",
                   dir, ci, k.kind, (unsigned long long)seeder, (unsigned long long)out.seeder_state);
            fail = 1;
        }
        else if (!fail && next_id != out.next_id)
        {
            printf("FAIL %s case %d kind %d: next entity id want %d got %d\n",
                   dir, ci, k.kind, next_id, out.next_id);
            fail = 1;
        }
        else if (!fail && tick_entry != out.tick_entry)
        {
            printf("FAIL %s case %d kind %d: next tick entry want %lld got %lld\n",
                   dir, ci, k.kind, (long long)tick_entry, (long long)out.tick_entry);
            fail = 1;
        }

        if (fail) break;

        /* The 3x3 hash, every 64 cases and on the last. */
        if (probe_hash_due(ci, cases))
        {
            const unsigned char *h = hashes.v + hashes.rec * hpos;
            int hcase = (int)le32(h);
            uint64_t want = le64(h + 4);
            uint64_t got = hash_around(&w, k.x >> 4, k.z >> 4, cb);

            ++hpos;

            if (hcase != ci)
            {
                printf("FAIL %s: hash row %zu is for case %d, expected %d\n", dir, hpos - 1, hcase, ci);
                fail = 1;
            }
            else if (want != got)
            {
                printf("FAIL %s case %d kind %d: hash of the 3x3 chunks want %016llx got %016llx\n",
                       dir, ci, k.kind, (unsigned long long)want, (unsigned long long)got);
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

    if (!fail)
        printf("PASS %s %d activation cases\n", dir, cases);

    /* The per-kind table, pass or fail. */
    for (int k = 0; k < nkind_ids; ++k)
        printf("kind %2d (block %3d): %ld cases, %ld true, %ld guis, %ld chats, "
               "%ld events, %ld entities\n",
               k, kind_ids[k], kind_cases[k], kind_true[k], kind_gui[k],
               kind_chat[k], kind_events[k], kind_ents[k]);

    return fail;
}