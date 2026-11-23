/* Gate: the native tile entity pass against the oracle's TileTickProbe
 * (oracle/harness/netherite/oracle/TileTickProbe.java, recorded with
 * `make -C oracle run CLASS=TileTickProbe`).
 *
 *   test_tileticks PROBE_DIR
 *
 * DIR holds manifest.json (kind "tileticks": seed, cx, cz, radius, ticks,
 * timestart, the census), build.bin.gz (the setups' setBlock flag-2 writes),
 * init.jsonl.gz (every tile entity's canonical NBT in creation order),
 * ticks.bin (per tick the recorded skylight and celestial-angle table plus
 * the walk, NBT-change and write counts), writes.bin.gz (the Rows.onBlock
 * stream), nbt.jsonl.gz (one line per changed tile entity per tick in walk
 * order), order.bin.gz (the walk order per tick, 2 bytes per entry),
 * chunkhash.bin.gz (per tick-with-writes the FNV of the 3x3 chunks around
 * each chunk a write landed in) and final.bin.gz.
 *
 * The region loads in the manifest's order, the build writes replay through
 * world_set_block (demand creation fills the world list the same way), the
 * init lines load the entities' fields in creation order, then one
 * tileticks_pass runs per tick with the recorded time values, the write
 * stream, the changed-NBT lines, the walk order, the chunk hashes and the
 * final region compared against the record. The first difference names the
 * tick, the tile entity and both values. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <ctype.h>

#include "../engine/features.h"
#include "../engine/tileticks.h"
#include "probe.h"

#define TICK_BYTES (96027 + 10) /* 96027 array + 10 trailer */
#define WRITE_BYTES 16

static char diff_what[512];

/* The oracle's write stream, byte compared in order. */
struct writes
{
    const unsigned char *base;
    uint32_t want;
    uint32_t got;
    int overflow;
    int bad;
};

static struct writes cur;

/* One int off a parsed canonical compound (nbtjson has no typed getter). */
static int nbt_int_local(const nbt *comp, const char *key)
{
    const nbt *v = nbt_get(comp, key);
    if (v == NULL) return 0;

    char *text = nbt_render(v);
    int r = atoi(text + 3);
    free(text);
    return r;
}

/* world's on_block hook: compare one write against the oracle's, in order.
 * A metadata-only write reports id -1, which the record stores as 0xffff. */
static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    struct writes *s = ctx;

    if (s->got >= s->want)
    {
        if (!s->overflow)
        {
            s->overflow = 1;
            snprintf(diff_what, sizeof diff_what, "write %u (%d,%d,%d) id %d meta %d: the oracle has only %u writes",
                     s->got + 1, x, y, z, id & 0xffff, meta, s->want);
        }

        ++s->got;
        return;
    }

    const unsigned char *o = s->base + WRITE_BYTES * (size_t)s->got;
    int wx = (int)le32(o), wy = (int)le32(o + 4), wz = (int)le32(o + 8);
    int wid = o[12] | o[13] << 8, wmeta = o[14];

    if (!s->bad && (x != wx || y != wy || z != wz || (id & 0xffff) != wid || meta != wmeta))
    {
        s->bad = (int)s->got + 1;
        snprintf(diff_what, sizeof diff_what, "write %u want (%d,%d,%d) id %d meta %d, got (%d,%d,%d) id %d meta %d",
                 s->got + 1, wx, wy, wz, wid, wmeta, x, y, z, id & 0xffff, meta);
    }

    ++s->got;
}

/* A whole gzip file into a malloc'd buffer. */
static unsigned char *read_gz(const char *path, size_t *len)
{
    gzFile g = gzopen(path, "rb");

    if (g == NULL)
    {
        perror(path);
        exit(2);
    }

    size_t cap = 1 << 20, n = 0;
    unsigned char *buf = malloc(cap);

    for (;;)
    {
        if (n == cap)
        {
            cap *= 2;
            buf = realloc(buf, cap);
        }

        int got = gzread(g, buf + n, (unsigned)(cap - n));

        if (got <= 0) break;

        n += (size_t)got;
    }

    gzclose(g);
    *len = n;
    return buf;
}

/* One canonical NBT line of init.jsonl.gz or nbt.jsonl.gz, up to the
 * newline. */
static char *read_line_gz(gzFile g, char *buf, int size)
{
    return gzgets(g, buf, size);
}

/* One float off a parsed canonical compound (nbtjson has no typed getter; the
 * rendered scalar is quoted, so the bits start after the quote and "f:"). */
static float nbt_float_local(const nbt *comp, const char *key)
{
    const nbt *v = nbt_get(comp, key);

    if (v == NULL || nbt_kind(v) != NBT_FLOAT) return 0.0F;

    char *text = nbt_render(v);
    uint32_t bits = (uint32_t)strtoul(text + 3, NULL, 16);
    float f;

    memcpy(&f, &bits, sizeof f);
    free(text);
    return f;
}

/* The lid stream: one line per chest and ender chest, one per entity per
 * tick in walk order. A tick's lines are consumed by walking the native list
 * in the same order, so the reader peeks one line ahead. */
static gzFile lid_g;
static char lid_buf[4096];
static int lid_pending;

/* The next line without its newline, or NULL at the end of the stream. */
static char *lid_peek(void)
{
    if (!lid_pending)
    {
        if (lid_g == NULL || gzgets(lid_g, lid_buf, sizeof lid_buf) == NULL)
        {
            lid_buf[0] = 0;
            lid_pending = 1;
            return NULL;
        }

        lid_pending = 1;

        size_t n = strlen(lid_buf);
        while (n > 0 && (lid_buf[n - 1] == '\n' || lid_buf[n - 1] == '\r')) --n;
        lid_buf[n] = 0;
        if (n == 0)
        {
            lid_pending = 0;
            return lid_peek(); /* a blank line is not a record */
        }
    }

    return lid_buf[0] == 0 ? NULL : lid_buf;
}

/* The peeked line is the one to compare against; consuming it lets the next
 * read move on. */
static void lid_take(void)
{
    lid_pending = 0;
    lid_buf[0] = 0;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_tileticks PROBE_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1024];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    char kind[64];

    manifest_str(manifest, "kind", kind, sizeof kind);
    if (strcmp(kind, "tileticks") != 0)
    {
        printf("SKIP %s: kind %s is not a tile tick probe\n", dir, kind);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int ticks = (int)manifest_int(manifest, "ticks");
    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int radius = (int)manifest_int(manifest, "radius");
    int slots_n = (int)manifest_int(manifest, "slots");
    int sx_n = 8;  /* the slot grid: SX columns; the manifest's slot_grid string documents it */
    (void)slots_n;

    int nchunks = 0, *lcz = NULL;
    int *lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    struct world w;

    world_init(&w, seed);

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    /* the setups' writes, through world_set_block with the listener off: the
     * oracle's probe records only what it wrote itself */
    snprintf(path, sizeof path, "%s/build.bin.gz", dir);
    size_t blen;
    unsigned char *bbuf = read_gz(path, &blen);

    if (blen % WRITE_BYTES != 0)
    {
        fprintf(stderr, "%s: build.bin.gz is %zu bytes\n", dir, blen);
        return 2;
    }

    /* The record's metadata-only writes (id 0xffff) come in two kinds: the
     * facing write a tile-entity block's onBlockAdded makes inside its own
     * setBlock (recorded BEFORE the block write itself, because Rows.onBlock
     * for the block fires after func_150807_a returns), and the plain
     * metadata ticks (the daylight detector, the brewing stand). The first
     * kind is reproduced by the block write's own onBlockAdded, so the replay
     * skips a 0xffff write that is immediately followed by a block write at
     * the same position; the second kind replays through world_set_meta. */
    for (size_t off = 0; off < blen; off += WRITE_BYTES)
    {
        const unsigned char *o = bbuf + off;
        int x = (int)le32(o), y = (int)le32(o + 4), z = (int)le32(o + 8);
        int id = o[12] | o[13] << 8, meta = o[14];

        if ((id & 0xffff) == 0xffff && off + WRITE_BYTES < blen)
        {
            const unsigned char *n = bbuf + off + WRITE_BYTES;
            int nx = (int)le32(n), ny = (int)le32(n + 4), nz = (int)le32(n + 8);
            int nid = n[12] | n[13] << 8;

            if ((nid & 0xffff) != 0xffff && nx == x && ny == y && nz == z) continue;
        }

        if ((id & 0xffff) == 0xffff) world_set_meta(&w, x, y, z, meta, 2);
        else world_set_block(&w, x, y, z, id, meta, 2);
    }

    free(bbuf);

    /* the tile entities in creation order: init.jsonl.gz, one canonical NBT
     * line each, into world_set_tile_entity */
    snprintf(path, sizeof path, "%s/init.jsonl.gz", dir);
    gzFile ig = gzopen(path, "rb");
    if (ig == NULL)
    {
        perror(path);
        return 2;
    }

    char line[65536];
    int ntiles = 0;

    /* the entities in creation order, for the walk-order compare: the
     * recorded slot is the SETUP's slot (every entity a setup made shares
     * it), and the piston entity is 0xffff */
    struct tile_entity **init_order = malloc(1024 * sizeof *init_order);
    int *init_slot = malloc(1024 * sizeof *init_slot);
    int init_cap = 1024;

    while (read_line_gz(ig, line, sizeof line))
    {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) --n;
        line[n] = 0;
        if (n == 0) continue;

        nbt *tag = nbt_parse(line);

        if (tag == NULL)
        {
            fprintf(stderr, "%s: init line %d is not canonical NBT\n", dir, ntiles);
            return 2;
        }

        /* te_load sets x, y, z off the tag; the kind comes off the id */
        const nbt *idv = nbt_get(tag, "id");
        const char *idname = nbt_string_value(idv);
        int te_kind = 0;

        if (strcmp(idname, "Furnace") == 0) te_kind = TE_FURNACE;
        else if (strcmp(idname, "Chest") == 0) te_kind = TE_CHEST;
        else if (strcmp(idname, "EnderChest") == 0) te_kind = TE_ENDER_CHEST;
        else if (strcmp(idname, "Hopper") == 0) te_kind = TE_HOPPER;
        else if (strcmp(idname, "MobSpawner") == 0) te_kind = TE_MOB_SPAWNER;
        else if (strcmp(idname, "Music") == 0) te_kind = TE_NOTE;
        else if (strcmp(idname, "DLDetector") == 0) te_kind = TE_DAYLIGHT_DETECTOR;
        else if (strcmp(idname, "Beacon") == 0) te_kind = TE_BEACON;
        else if (strcmp(idname, "Cauldron") == 0) te_kind = TE_BREWING_STAND;
        else if (strcmp(idname, "EnchantTable") == 0) te_kind = TE_ENCHANT_TABLE;
        else if (strcmp(idname, "Piston") == 0) te_kind = TE_PISTON;
        else
        {
            fprintf(stderr, "%s: init line %d names unknown kind %s\n", dir, ntiles, idname);
            return 2;
        }

        struct tile_entity *te = te_new(te_kind);
        te_load(te, tag);
        nbt_free(tag);

        if (ntiles == init_cap)
        {
            init_cap *= 2;
            init_order = realloc(init_order, (size_t)init_cap * sizeof *init_order);
            init_slot = realloc(init_slot, (size_t)init_cap * sizeof *init_slot);
        }

        init_order[ntiles] = te;

        /* the slot: the grid anchor this entity sits in; the piston entity
         * (over a BlockPistonMoving block) is 0xffff */
        if (te_kind == TE_PISTON)
            init_slot[ntiles] = 0xffff;
        else
        {
            int bx = (cx - radius) * 16 + 8, bz = (cz - radius) * 16 + 8;
            int col = (te->x - bx) / 13, row = (te->z - bz) / 13;
            init_slot[ntiles] = col + sx_n * row;
        }

        world_set_tile_entity(&w, te->x, te->y, te->z, te);
        ++ntiles;
    }

    gzclose(ig);

    /* the build's demand-created entities joined the world list
     * (Chunk.func_150806_e -> World.setTileEntity); each init line above
     * stands in for one of them and invalidated it, so the list keeps only
     * the init lines' entities, in their creation order */
    {
        int kept = 0;
        for (int i = 0; i < w.te_n; ++i)
            if (!w.te_list[i]->invalid) w.te_list[kept++] = w.te_list[i];
        w.te_n = kept;
    }

    /* the chest and ender chest fields writeToNBT does not carry: the probe
     * sets them directly, so their initial values come back from
     * lidinit.jsonl.gz (the same line shape as the per-tick ones, one per
     * entity that has any) */
    snprintf(path, sizeof path, "%s/lidinit.jsonl.gz", dir);
    gzFile lg0 = gzopen(path, "rb");
    if (lg0 == NULL)
    {
        perror(path);
        return 2;
    }

    int lidinit_n = 0;

    while (read_line_gz(lg0, line, sizeof line))
    {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) --n;
        line[n] = 0;
        if (n == 0) continue;

        nbt *tag = nbt_parse(line);

        if (tag == NULL)
        {
            fprintf(stderr, "%s: lidinit line %d is not canonical NBT\n", dir, lidinit_n);
            return 2;
        }

        int tx = nbt_int_local(tag, "x"), ty = nbt_int_local(tag, "y"), tz = nbt_int_local(tag, "z");
        struct chunk *tc = world_chunk(&w, tx >> 4, tz >> 4);
        struct tile_entity *te = tc != NULL ? te_find(&tc->tes, tx, ty, tz) : NULL;
        const char *id = nbt_string_value(nbt_get(tag, "id"));

        if (te == NULL || id == NULL
            || (strcmp(id, "Chest") != 0 && strcmp(id, "EnderChest") != 0))
        {
            fprintf(stderr, "%s: lidinit line %d names no chest at (%d,%d,%d)\n", dir, lidinit_n, tx, ty, tz);
            return 2;
        }

        te->u.chest.players_using = nbt_int_local(tag, "players");
        te->u.chest.lid = nbt_float_local(tag, "lid");
        te->u.chest.prev_lid = nbt_float_local(tag, "prev");
        te->u.chest.tick_counter = nbt_int_local(tag, "counter");
        nbt_free(tag);
        ++lidinit_n;
    }

    gzclose(lg0);

    /* the tick state, one record per tick */
    snprintf(path, sizeof path, "%s/ticks.bin", dir);
    size_t tlen;
    unsigned char *tbuf = probe_read_file(path, &tlen);

    if (tlen % TICK_BYTES != 0 || (int)(tlen / TICK_BYTES) != ticks + 1)
    {
        fprintf(stderr, "%s: ticks.bin is %zu bytes, the manifest says %d ticks\n", dir, tlen, ticks);
        return 2;
    }

    /* the write stream */
    snprintf(path, sizeof path, "%s/writes.bin.gz", dir);
    size_t wlen;
    unsigned char *wbuf = read_gz(path, &wlen);
    size_t woff = 0;

    /* the changed-NBT lines, in walk order */
    snprintf(path, sizeof path, "%s/nbt.jsonl.gz", dir);
    gzFile ng = gzopen(path, "rb");
    if (ng == NULL)
    {
        perror(path);
        return 2;
    }

    /* the chest and ender chest lid lines */
    snprintf(path, sizeof path, "%s/lid.jsonl.gz", dir);
    lid_g = gzopen(path, "rb");
    if (lid_g == NULL)
    {
        perror(path);
        return 2;
    }

    lid_pending = 0;
    lid_buf[0] = 0;

    /* the walk order */
    snprintf(path, sizeof path, "%s/order.bin.gz", dir);
    size_t olen;
    unsigned char *obuf = read_gz(path, &olen);
    size_t ooff = 0;

    /* the per-tick chunk hashes */
    snprintf(path, sizeof path, "%s/chunkhash.bin.gz", dir);
    size_t hlen;
    unsigned char *hbuf = read_gz(path, &hlen);
    size_t hoff = 0;

    unsigned char *hashbuf = malloc(CHUNK_BYTES);
    int fail = 0;
    long long total_writes = 0;
    long long total_changes = 0;

    for (int t = 0; t <= ticks && !fail; ++t)
    {
        const unsigned char *rec = tbuf + TICK_BYTES * (size_t)t;
        int skylight = rec[0];
        int64_t world_time = (int64_t)le64(rec + 1);
        int64_t total_time = (int64_t)le64(rec + 9);
        const float *cos_table = (const float *)(rec + 27);
        const unsigned char *tail = rec + TICK_BYTES - 10;
        uint32_t want_walk = tail[0] | tail[1] << 8;
        uint32_t want_nbt = le32(tail + 2);
        uint32_t want_writes = le32(tail + 6);

        if (woff + (size_t)want_writes * WRITE_BYTES > wlen)
        {
            fprintf(stderr, "%s: writes.bin.gz is short at tick %d\n", dir, t);
            return 2;
        }

        cur.base = wbuf + woff;
        cur.want = want_writes;
        cur.got = 0;
        cur.overflow = 0;
        cur.bad = 0;

        w.on_block = t == 0 ? NULL : on_write;
        w.on_block_ctx = &cur;

        if (t > 0)
        {
            tileticks_set_time(world_time, total_time, skylight, cos_table);
            tileticks_pass(&w);
        }

        w.on_block = NULL;

        /* the walk order: the world list's creation indices, in order */
        if (ooff + (size_t)want_walk * 2 > olen)
        {
            fprintf(stderr, "%s: order.bin.gz is short at tick %d\n", dir, t);
            return 2;
        }

        int order_bad = 0;

        for (uint32_t i = 0; i < want_walk && !order_bad; ++i)
        {
            int want_slot = obuf[ooff + 2 * i] | obuf[ooff + 2 * i + 1] << 8;

            if (i >= (uint32_t)w.te_n)
            {
                snprintf(diff_what, sizeof diff_what, "walk %u: the native list holds only %d", i + 1, w.te_n);
                order_bad = 1;
                break;
            }

            /* the entity at walk position i must be the one the probe
             * recorded: its creation index is the slot, and the test's own
             * init order gives the entity that index held */
            struct tile_entity *te = w.te_list[i];
            int got_slot = -1;

            for (int k = 0; k < ntiles; ++k)
            {
                if (init_order[k] == te) { got_slot = init_slot[k]; break; }
            }

            if (got_slot < 0)
            {
                /* Java's field_147482_g can hold the same object twice after
                 * a setTileEntity inside the pass (the furnace swap's
                 * func_149931_a); the recorded order then carries the same
                 * slot twice. A list duplicate of the previous entry is the
                 * same object: pass it with the previous entry's slot. */
                if (i > 0 && w.te_list[i - 1] == te)
                {
                    ooff += 2; /* this entry consumed its record already; skip */
                    continue;
                }

                snprintf(diff_what, sizeof diff_what, "walk %u: an entity at (%d,%d,%d) the init order does not hold",
                         i + 1, te->x, te->y, te->z);
                order_bad = 1;
                break;
            }

            if (got_slot != want_slot)
            {
                snprintf(diff_what, sizeof diff_what, "walk %u: want slot %d, got slot %d (entity at %d,%d,%d)", i + 1,
                         want_slot, got_slot, te->x, te->y, te->z);
                order_bad = 1;
                break;
            }
        }

        ooff += (size_t)want_walk * 2;

        /* the changed NBTs, in walk order */
        for (uint32_t i = 0; i < want_nbt && !fail; ++i)
        {
            if (!read_line_gz(ng, line, sizeof line))
            {
                printf("FAIL %s tick %d: nbt line %u missing\n", dir, t, i + 1);
                ++fail;
                break;
            }

            size_t n = strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) --n;
            line[n] = 0;
            if (n == 0) { --i; continue; }

            nbt *want_tag = nbt_parse(line);

            if (want_tag == NULL)
            {
                snprintf(diff_what, sizeof diff_what, "change %u: not canonical NBT", i + 1);
                fail = 1;
                break;
            }

            /* the line carries the position; the native entity at that
             * position is the one that changed (walk order ties two entities
             * of one slot together, but the position is unique) */
            int tx = nbt_int_local(want_tag, "x");
            int ty = nbt_int_local(want_tag, "y");
            int tz = nbt_int_local(want_tag, "z");
            struct chunk *tc = world_chunk(&w, tx >> 4, tz >> 4);
            struct tile_entity *te = tc ? te_find(&tc->tes, tx, ty, tz) : NULL;

            if (te == NULL)
            {
                snprintf(diff_what, sizeof diff_what, "change %u: no entity at (%d,%d,%d)", i + 1, tx, ty, tz);
                nbt_free(want_tag);
                fail = 1;
                break;
            }

            char *got = te_render(te);

            if (strcmp(got, line) != 0)
            {
                nbt *want_tag = nbt_parse(line);
                nbt *got_tag = nbt_parse(got);
                char why[256];

                if (want_tag && got_tag && nbt_diff(want_tag, got_tag, why, sizeof why))
                    snprintf(diff_what, sizeof diff_what, "entity %u (%s) at (%d,%d,%d): %s", i + 1,
                             nbt_string_value(nbt_get(want_tag, "id")), te->x, te->y, te->z, why);

                printf("FAIL %s tick %d: %s\n", dir, t, diff_what);
                printf("     want %s\n", line);
                printf("     got  %s\n", got);

                if (want_tag) nbt_free(want_tag);
                if (got_tag) nbt_free(got_tag);
                free(got);
                fail = 1;
                break;
            }

            free(got);
            ++total_changes;
        }

        if (fail) break;

        /* the write count and stream */
        if (cur.got != want_writes)
        {
            printf("FAIL %s tick %d: %u writes, the oracle recorded %u%s\n", dir, t, cur.got, want_writes,
                   cur.overflow ? ", " : "");
            ++fail;
            break;
        }

        if (cur.bad)
        {
            printf("FAIL %s tick %d: %s\n", dir, t, diff_what);
            ++fail;
            break;
        }

        if (order_bad)
        {
            printf("FAIL %s tick %d: walk order: %s\n", dir, t, diff_what);
            ++fail;
            break;
        }

        /* the lid lines: one per chest and ender chest of the walk, in the
         * same walk order, every tick (tick 0 included) */
        for (int i = 0; i < w.te_n && !fail; ++i)
        {
            char *want_lid = tileticks_lid_render(w.te_list[i], t);

            if (want_lid == NULL) continue;

            char *got_lid = lid_peek();

            if (got_lid == NULL)
            {
                printf("FAIL %s tick %d: the lid stream ends before the chest at (%d,%d,%d)\n", dir, t,
                       w.te_list[i]->x, w.te_list[i]->y, w.te_list[i]->z);
                ++fail;
                free(want_lid);
                break;
            }

            if (strcmp(got_lid, want_lid) != 0)
            {
                printf("FAIL %s tick %d: chest at (%d,%d,%d): lid line\n     want %s\n     got  %s\n", dir, t,
                       w.te_list[i]->x, w.te_list[i]->y, w.te_list[i]->z, got_lid, want_lid);
                ++fail;
                free(want_lid);
                break;
            }

            lid_take();
            free(want_lid);
        }

        if (fail) break;

        /* nothing of this tick's lines may be left over */
        char *extra = lid_peek();

        if (extra != NULL)
        {
            nbt *tag = nbt_parse(extra);
            int et = tag != NULL ? nbt_int_local(tag, "tick") : -1;

            if (tag != NULL) nbt_free(tag);

            if (et == t)
            {
                printf("FAIL %s tick %d: the oracle recorded a lid line the native walk has no entity for:\n     %s\n",
                       dir, t, extra);
                ++fail;
                break;
            }
        }

        total_writes += want_writes;

        /* the chunk hashes: one record per distinct chunk this tick wrote to,
         * in first-write order (consecutive repeats of one chunk collapsed).
         * The records' chunks come from the tick's write stream, which the
         * byte compare above has already matched. */
        const unsigned char *tick_writes = wbuf + woff;
        size_t wi = 0;
        int lastcx = 0, lastcz = 0, have_chunk = 0;

        woff += (size_t)want_writes * WRITE_BYTES;

        while (hoff + 12 <= hlen)
        {
            uint32_t rec_tick = le32(hbuf + hoff);
            if (rec_tick != (uint32_t)t) break;

            uint64_t want_hash = le64(hbuf + hoff + 4);
            int advanced = 0;

            while (wi < (size_t)want_writes)
            {
                const unsigned char *fw = tick_writes + WRITE_BYTES * wi;
                int wx = (int)le32(fw), wz = (int)le32(fw + 8);
                ++wi;

                if (!have_chunk || wx >> 4 != lastcx || wz >> 4 != lastcz)
                {
                    lastcx = wx >> 4;
                    lastcz = wz >> 4;
                    have_chunk = 1;
                    advanced = 1;
                    break;
                }
            }

            if (!advanced)
            {
                snprintf(diff_what, sizeof diff_what, "the oracle has %u writes for this tick, too few for its "
                         "hash records", want_writes);
                ++fail;
                break;
            }

            int fcx = lastcx, fcz = lastcz;
            uint64_t got_hash = hash_around(&w, fcx, fcz, hashbuf);

            if (got_hash != want_hash)
            {
                printf("FAIL %s tick %d: hash of the 3x3 chunks around (%d,%d) want %016llx got %016llx\n", dir, t,
                       fcx, fcz, (unsigned long long)want_hash, (unsigned long long)got_hash);
                ++fail;
                break;
            }

            hoff += 12;
        }

        if (fail) break;

        /* the walk count */
        if ((uint32_t)w.te_n != want_walk)
        {
            printf("FAIL %s tick %d: %d entities in the walk, the oracle recorded %u\n", dir, t, w.te_n, want_walk);
            ++fail;
            break;
        }
    }

    if (!fail)
    {
        snprintf(path, sizeof path, "%s/final.bin.gz", dir);
        fail = cmp_final(dir, path, &w, lcx, lcz, nchunks);
    }

    if (fail) probe_localize(argv[0], dir);

    if (!fail)
    {
        printf("PASS %s: %d ticks, %d tile entities, %lld writes, %lld nbt changes, %d chunks, seed %lld\n", dir,
               ticks, ntiles, total_writes, total_changes, nchunks, (long long)seed);
    }

    if (lid_g != NULL) gzclose(lid_g);
    free(init_order);
    free(init_slot);
    free(hashbuf);
    free(hbuf);
    free(obuf);
    free(wbuf);
    free(tbuf);
    free(lcx);
    free(lcz);
    free(manifest);
    world_free(&w);
    return fail;
}
