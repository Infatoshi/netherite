/* Loads the oracle's snapshot (see snapshot.h and the manifest next to the
 * files) into a struct world plus the lists the tape replay needs. */
#define _POSIX_C_SOURCE 200809L /* fmemopen */
#include "snapshot.h"

#include "blocks.h"
#include "gunzip.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

/* The layout Probe.fillChunkBytes writes: ids uint16, metas, sky, block light,
 * heightMap 256 int32, precipitationHeightMap 256 int32, heightMapMinimum
 * int32, section mask uint16, all little-endian. The cells are flat
 * x << 12 | z << 8 | y arrays; struct chunk keeps them in 16-block bands
 * (world.h), so a column's 16 cells of one band are one run of the layout. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define CHUNK_IDS_LE 1
#endif

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "snapshot: out of memory\n"); exit(2); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "snapshot: out of memory\n"); exit(2); }
    return q;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void path_join(char *out, size_t n, const char *dir, const char *name)
{
    snprintf(out, n, "%s/%s", dir, name);
}

/* A whole text file, malloc'd and NUL terminated, with its trailing newline
 * trimmed; NULL when it cannot be read. */
static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);

    char *buf = xmalloc((size_t)len + 1);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
    fclose(f);

    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) --len;
    buf[len] = 0;
    return buf;
}

/* A snapshot text file, or its gzip (NAME.gz: the keyframes and the any-tick
 * sweep write their big text files gzipped), as a FILE the line reader takes;
 * *buf holds the inflated copy (NULL for a plain file), which text_close
 * releases with it. */
static FILE *text_open(const char *path, uint8_t **buf)
{
    *buf = NULL;
    FILE *f = fopen(path, "rb");
    if (f) return f;
    char gz[1300];
    snprintf(gz, sizeof gz, "%s.gz", path);
    size_t len = 0;
    if (!gunzip_file(gz, buf, &len)) { free(*buf); *buf = NULL; return NULL; }
    f = len ? fmemopen(*buf, len, "rb") : fmemopen((void *)"", 1, "r");
    if (!f) { free(*buf); *buf = NULL; }
    return f;
}

static void text_close(FILE *f, uint8_t *buf)
{
    fclose(f);
    free(buf);
}

static int text_exists(const char *path)
{
    char gz[1300];
    snprintf(gz, sizeof gz, "%s.gz", path);
    return access(path, R_OK) == 0 || access(gz, R_OK) == 0;
}

static int parse_nbt_file(const char *dir, const char *name, nbt **out)
{
    char path[1200];
    path_join(path, sizeof path, dir, name);
    char *text = read_text(path);
    if (!text) { fprintf(stderr, "snapshot: cannot read %s\n", path); return 0; }
    *out = nbt_parse(text);
    if (!*out) { fprintf(stderr, "snapshot: %s is not canonical NBT text\n", path); free(text); return 0; }
    free(text);
    return 1;
}

/* -------------------------------------------------------------- the manifest */

static int jd(const struct jval *v, double *out)
{
    uint64_t bits;
    if (!json_double(v, &bits)) return 0;
    memcpy(out, &bits, sizeof *out);
    return 1;
}

static int jf(const struct jval *v, float *out)
{
    uint32_t bits;
    if (!json_float(v, &bits)) return 0;
    memcpy(out, &bits, sizeof *out);
    return 1;
}

static int ji(const struct jval *v, int *out)
{
    int64_t iv;
    if (!json_int(v, &iv)) return 0;
    *out = (int)iv;
    return 1;
}

/* Snapshot.dragonState's object: every key present, the ring only once the
 * dragon has ticked. */
static int snap_parse_dragon(const struct jval *o, struct snap_dragon *d)
{
    memset(d, 0, sizeof *d);
    const struct jval *t = json_get(o, "target"), *prev = json_get(o, "prev"), *bb = json_get(o, "bb");
    const struct jval *ring = json_get(o, "ring"), *parts = json_get(o, "parts");
    int ok = t && json_len(t) == 3 && prev && json_len(prev) == 3 && bb && json_len(bb) == 5 &&
             parts && json_len(parts) == 7;
    for (int i = 0; ok && i < 3; ++i)
        ok = jd(json_at(t, i), &d->target[i]) && jd(json_at(prev, i), &d->prev[i]);
    for (int i = 0; ok && i < 5; ++i) ok = jd(json_at(bb, i), &d->bb[i]);
    ok = ok && ji(json_get(o, "ringi"), &d->ring_index) && jf(json_get(o, "anim"), &d->anim) &&
         jf(json_get(o, "panim"), &d->prev_anim) && ji(json_get(o, "force"), &d->force) &&
         ji(json_get(o, "slowed"), &d->slowed) && ji(json_get(o, "hunt"), &d->hunt) &&
         ji(json_get(o, "deathTicks"), &d->death_ticks) && ji(json_get(o, "heal"), &d->heal) &&
         ji(json_get(o, "ticksExisted"), &d->ticks_existed) &&
         ji(json_get(o, "hurtResistantTime"), &d->hurt_resistant_time) &&
         jf(json_get(o, "lastDamage"), &d->last_damage) && jf(json_get(o, "prevHealth"), &d->prev_health) &&
         jf(json_get(o, "yawVelocity"), &d->yaw_velocity) && jf(json_get(o, "renderYaw"), &d->render_yaw) &&
         jf(json_get(o, "prevYaw"), &d->prev_yaw);
    if (ok && ring)
    {
        ok = json_len(ring) == 128;
        d->has_ring = 1;
        for (int i = 0; ok && i < 64; ++i)
            ok = jd(json_at(ring, i * 2), &d->ring[i][0]) && jd(json_at(ring, i * 2 + 1), &d->ring[i][1]);
    }
    for (int i = 0; ok && i < 7; ++i)
    {
        const struct jval *p = json_at(parts, i);
        ok = p && json_len(p) == 6 && ji(json_at(p, 0), &d->part_id[i]) && jd(json_at(p, 1), &d->part[i][0]) &&
             jd(json_at(p, 2), &d->part[i][1]) && jd(json_at(p, 3), &d->part[i][2]) &&
             jf(json_at(p, 4), &d->part_w[i]) && jf(json_at(p, 5), &d->part_h[i]);
    }
    return ok;
}

static int manifest_int(const struct jval *m, const char *key, int64_t *out)
{
    return json_int(json_get(m, key), out);
}

/* --------------------------------------------------------------- hex arrays */

static int hex_bytes(const char *s, uint8_t *out, int n)
{
    if (!s || (int)strlen(s) != 2 * n) return 0;

    for (int i = 0; i < n; ++i)
    {
        int hi = s[2 * i], lo = s[2 * i + 1];
        int a = hi >= '0' && hi <= '9' ? hi - '0' : (hi >= 'a' && hi <= 'f' ? hi - 'a' + 10 : -1);
        int b = lo >= '0' && lo <= '9' ? lo - '0' : (lo >= 'a' && lo <= 'f' ? lo - 'a' + 10 : -1);
        if (a < 0 || b < 0) return 0;
        out[i] = (uint8_t)(a << 4 | b);
    }

    return 1;
}

/* ------------------------------------------------------------------- chunks */

static int load_chunkstate(struct snapshot *s, const char *path)
{
    struct lines in;
    lines_init(&in);
    uint8_t *tbuf;
    FILE *f = text_open(path, &tbuf);
    if (!f) { fprintf(stderr, "snapshot: cannot read %s\n", path); lines_free(&in); return 0; }

    lines_file(&in, f);
    int cap = 0, n = 0;

    for (const char *line = lines_next(&in); line; line = lines_next(&in))
    {
        if (!line[0]) continue;
        struct jval *v = json_parse(xstrndup(line, strlen(line)));
        if (!v || v->kind != J_OBJ) { fprintf(stderr, "snapshot: bad chunkstate line %d\n", n + 1); json_free(v); text_close(f, tbuf); lines_free(&in); return 0; }

        if (n >= cap)
        {
            cap = n + 256;
            s->chunks = xrealloc(s->chunks, (size_t)cap * sizeof *s->chunks);
        }

        struct snap_chunk *c = &s->chunks[n];
        memset(c, 0, sizeof *c);
        int64_t iv;
        const struct jval *biome = json_get(v, "biome");
        const struct jval *cols = json_get(v, "skylightColumns");

        if (!json_int(json_get(v, "cx"), &iv)) { fprintf(stderr, "snapshot: chunkstate %d has no cx\n", n); json_free(v); text_close(f, tbuf); lines_free(&in); return 0; }
        c->cx = (int)iv;
        if (json_int(json_get(v, "dim"), &iv)) c->dim = (int)iv;
        if (!json_int(json_get(v, "cz"), &iv)) { fprintf(stderr, "snapshot: chunkstate %d has no cz\n", n); json_free(v); text_close(f, tbuf); lines_free(&in); return 0; }
        c->cz = (int)iv;

        if (!hex_bytes(biome ? biome->str : NULL, c->biome, SNAP_BIOME))
        {
            fprintf(stderr, "snapshot: chunk (%d,%d) has no 256 byte biome array\n", c->cx, c->cz);
            json_free(v); text_close(f, tbuf); lines_free(&in); return 0;
        }
        if (!hex_bytes(cols ? cols->str : NULL, c->skylight_columns, SNAP_COLUMNS))
        {
            fprintf(stderr, "snapshot: chunk (%d,%d) has no 256 byte skylightColumns\n", c->cx, c->cz);
            json_free(v); text_close(f, tbuf); lines_free(&in); return 0;
        }

        const char *flags[] = {"terrainPopulated", "lightPopulated", "populated", "modified", "hasEntities", "sendUpdates", "gap"};
        int *dst[] = {&c->terrain_populated, &c->light_populated, &c->populated, &c->modified, &c->has_entities, &c->send_updates, &c->gap};

        for (int i = 0; i < 7; ++i)
            if (json_int(json_get(v, flags[i]), &iv)) *dst[i] = (int)iv;

        if (json_int(json_get(v, "queuedLightChecks"), &iv)) c->queued_light_checks = (int)iv;
        if (json_int(json_get(v, "lastSaveTime"), &iv)) c->last_save_time = iv;
        if (json_int(json_get(v, "inhabitedTime"), &iv)) c->inhabited_time = iv;
        if (json_int(json_get(v, "saved"), &iv)) c->saved = (int)iv;

        const struct jval *sents = json_get(v, "ents");
        c->nents_saved = sents ? json_len(sents) : 0;
        if (c->nents_saved > 0)
        {
            c->ents = xmalloc((size_t)c->nents_saved * sizeof *c->ents);
            for (int i = 0; i < c->nents_saved; ++i)
            {
                const struct jval *t = json_at(sents, i);
                char *raw = json_raw(t);
                c->ents[i] = raw ? nbt_parse(raw) : NULL;
                free(raw);
                if (!c->ents[i])
                {
                    fprintf(stderr, "snapshot: chunk (%d,%d) saved entity %d is not canonical NBT\n", c->cx, c->cz, i);
                    json_free(v); text_close(f, tbuf); lines_free(&in); return 0;
                }
            }
        }

        const struct jval *sticks = json_get(v, "ticks");
        c->nsaved_ticks = sticks ? json_len(sticks) : 0;
        if (c->nsaved_ticks > 0)
        {
            c->saved_ticks = xmalloc((size_t)c->nsaved_ticks * sizeof *c->saved_ticks);
            for (int i = 0; i < c->nsaved_ticks; ++i)
            {
                struct snap_tick *t = &c->saved_ticks[i];
                memset(t, 0, sizeof *t);
                int64_t iv;
                t->dim = c->dim;
                const struct jval *to = json_at(sticks, i);
                if (json_int(json_get(to, "x"), &iv)) t->x = (int)iv;
                if (json_int(json_get(to, "y"), &iv)) t->y = (int)iv;
                if (json_int(json_get(to, "z"), &iv)) t->z = (int)iv;
                if (json_int(json_get(to, "id"), &iv)) t->id = (int)iv;
                if (json_int(json_get(to, "pri"), &iv)) t->priority = (int)iv;
                if (json_int(json_get(to, "t"), &iv)) t->scheduled = iv;
                if (json_int(json_get(to, "eid"), &iv)) t->entry = iv;
            }
        }

        const struct jval *tiles = json_get(v, "tiles");
        c->ntiles = tiles ? json_len(tiles) : 0;
        if (c->ntiles < 0) { fprintf(stderr, "snapshot: chunk (%d,%d) has no tiles list\n", c->cx, c->cz); json_free(v); text_close(f, tbuf); lines_free(&in); return 0; }

        if (c->ntiles)
        {
            c->tiles = xmalloc((size_t)c->ntiles * sizeof *c->tiles);
            for (int i = 0; i < c->ntiles; ++i)
            {
                const struct jval *t = json_at(tiles, i);
                char *rt = json_raw(json_at(json_get(v, "tilesrt"), i));
                c->tiles[i].rt = rt ? json_parse(rt) : NULL;
                c->tiles[i].text = json_raw(t);
                c->tiles[i].tag = c->tiles[i].text ? nbt_parse(c->tiles[i].text) : NULL;
                if (!c->tiles[i].tag)
                {
                    fprintf(stderr, "snapshot: chunk (%d,%d) tile %d is not canonical NBT\n", c->cx, c->cz, i);
                    json_free(v); text_close(f, tbuf); lines_free(&in); return 0;
                }
            }
        }

        /* the chests' state their NBT does not carry (Snapshot.chestState) */
        const char *cs = json_str(json_get(v, "chests"));
        if (cs != NULL)
        {
            int cap = 0;
            for (const char *p = cs; *p; ++p) cap += *p == ',';
            c->chests = xmalloc((size_t)(cap + 1) * sizeof *c->chests);
            c->nchests = 0;
            for (const char *p = cs;;)
            {
                char *end;
                long q = strtol(p, &end, 10);
                if (end == p) break;
                c->chests[c->nchests++] = (int)q;
                if (*end != ',') break;
                p = end + 1;
            }
            if (c->nchests % 9 != 0)
            {
                fprintf(stderr, "snapshot: chunk (%d,%d) has a malformed chests list\n", c->cx, c->cz);
                json_free(v); text_close(f, tbuf); lines_free(&in); return 0;
            }
            c->nchests /= 9;
        }

        json_free(v);
        ++n;
    }

    text_close(f, tbuf);
    lines_free(&in);
    s->nchunks = n;
    return 1;
}

/* One record's header: cx, cz, the chunk's own hash and its 3x3 hash. */
static int chunk_record(struct snapshot *s, int n, const unsigned char *head, const char *path)
{
    int cx = (int)(head[0] | head[1] << 8 | head[2] << 16 | (unsigned)head[3] << 24);
    int cz = (int)(head[4] | head[5] << 8 | head[6] << 16 | (unsigned)head[7] << 24);
    uint64_t hash = 0, near = 0;

    for (int i = 0; i < 8; ++i)
    {
        hash |= (uint64_t)head[8 + i] << (8 * i);
        near |= (uint64_t)head[16 + i] << (8 * i);
    }

    struct snap_chunk *c = &s->chunks[n];
    if (c->cx != cx || c->cz != cz)
    {
        fprintf(stderr, "snapshot: chunk %d is (%d,%d) in %s but (%d,%d) in chunkstate.jsonl\n", n, cx, cz, path, c->cx, c->cz);
        return 0;
    }

    c->hash = hash;
    c->near = near;
    return 1;
}

/* chunks.bin.gz, in chunkstate.jsonl order: cx, cz, hash, near, chunk bytes.
 * Each record is read into one scratch buffer and stored into a new chunk
 * (bands with a nonzero cell only), which load_world inserts; the bytes are
 * not kept: snapshot_chunk_want writes them back from the chunk. */
static int load_chunk_bytes(struct snapshot *s, const char *path)
{
    struct gunzip *z = gunzip_open_crc(path, !s->nocrc);
    if (!z) { fprintf(stderr, "snapshot: cannot read %s\n", path); return 0; }

    unsigned char head[24];
    int n = 0;
    uint8_t *buf = xmalloc(SNAP_CHUNK_BYTES);

    while (gunzip_read(z, head, 24) == 24)
    {
        if (n >= s->nchunks)
        {
            fprintf(stderr, "snapshot: %s has more chunks than chunkstate.jsonl\n", path);
            gunzip_close(z);
            free(buf);
            return 0;
        }
        if (!chunk_record(s, n, head, path)) { gunzip_close(z); free(buf); return 0; }

        if (gunzip_read(z, buf, SNAP_CHUNK_BYTES) != SNAP_CHUNK_BYTES)
        {
            fprintf(stderr, "snapshot: %s ends inside a chunk\n", path);
            gunzip_close(z);
            free(buf);
            return 0;
        }

        struct chunk *c = chunk_new();
        c->cx = s->chunks[n].cx;
        c->cz = s->chunks[n].cz;
        snapshot_chunk_from_bytes(c, buf);
        s->chunks[n].chunk = c;
        ++n;
    }

    gunzip_close(z);
    free(buf);

    if (n != s->nchunks)
    {
        fprintf(stderr, "snapshot: %s holds %d chunks, chunkstate.jsonl %d\n", path, n, s->nchunks);
        return 0;
    }

    return 1;
}

/* chunks.idx (or saved-chunks.idx): the blob form of chunks.bin.gz, one
 * "cx cz hash near blob" line per chunk in chunkstate order, the blob a gzip
 * of the chunk bytes at a path relative to the snapshot's directory. */
static int load_chunk_idx(struct snapshot *s, const char *dir, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "snapshot: cannot read %s\n", path); return 0; }
    char line[1400], blob[1200], full[2400];
    int n = 0;
    while (fgets(line, sizeof line, f))
    {
        int cx, cz;
        unsigned long long hash, near;
        if (sscanf(line, "%d %d %llx %llx %1199s", &cx, &cz, &hash, &near, blob) != 5) continue;
        if (n >= s->nchunks)
        {
            fprintf(stderr, "snapshot: %s has more chunks than chunkstate.jsonl\n", path);
            fclose(f);
            return 0;
        }
        struct snap_chunk *sc = &s->chunks[n];
        if (sc->cx != cx || sc->cz != cz)
        {
            fprintf(stderr, "snapshot: chunk %d is (%d,%d) in %s but (%d,%d) in chunkstate.jsonl\n", n, cx, cz, path, sc->cx, sc->cz);
            fclose(f);
            return 0;
        }
        sc->hash = hash;
        sc->near = near;
        snprintf(full, sizeof full, "%s/%s", dir, blob);
        uint8_t *buf = NULL;
        size_t len = 0;
        if (!gunzip_file_crc(full, &buf, &len, !s->nocrc) || len != SNAP_CHUNK_BYTES)
        {
            fprintf(stderr, "snapshot: blob %s is missing or not one chunk\n", full);
            free(buf);
            fclose(f);
            return 0;
        }
        struct chunk *c = chunk_new();
        c->cx = cx;
        c->cz = cz;
        snapshot_chunk_from_bytes(c, buf);
        free(buf);
        sc->chunk = c;
        ++n;
    }
    fclose(f);
    if (n != s->nchunks)
    {
        fprintf(stderr, "snapshot: %s holds %d chunks, chunkstate.jsonl %d\n", path, n, s->nchunks);
        return 0;
    }
    return 1;
}

/* One dimension's chunks from the snapshot's own bytes, inserted into w
 * (whose dim and seed the caller sets). Tiles go to the same world's tile
 * lists. saved: the region-file chunks (saved:1) instead of the loaded ones. */
static int load_world(struct snapshot *s, struct world *w, int saved)
{
    for (int i = 0; i < s->nchunks; ++i)
    {
        struct snap_chunk *sc = &s->chunks[i];
        if (sc->dim != w->dim || sc->saved != saved) continue;
        /* the recorded bytes replace everything the generator would write
         * but the biome array, which the chunkstate carries: no terrain is
         * generated for a chunk the snapshot supplies */
        struct chunk *c;

        /* the record was read into sc->chunk already; a repeated (cx, cz)
         * overwrites the loaded chunk's cells and maps, as a second load
         * would */
        struct chunk *have = world_chunk(w, sc->cx, sc->cz);

        if (have != NULL)
        {
            struct chunk *rec = sc->chunk;

            chunk_clear_cells(have);
            for (int k = 0; k < 16; ++k)
            {
                chunk_sec_put(have, k, chunk_sec_at(rec, k));
                rec->band[k] = 0;
            }
            memcpy(have->height, rec->height, sizeof have->height);
            memcpy(have->precip, rec->precip, sizeof have->precip);
            have->height_min = rec->height_min;
            have->mask = rec->mask;
            chunk_free(rec);
            c = have;
        }
        else c = world_insert_chunk_at(w, sc->chunk, sc->biome);
        sc->chunk = NULL;
        sc->live = c;

        /* ExtendedBlockStorage.tickRefCount: the count of random-ticking blocks
         * per section, which the snapshot's byte layout does not carry (it is
         * derived state), so it is rebuilt from the ids the way
         * Chunk.func_150807_a maintained it */
        chunk_count_ticking(c);
        c->inhabited_time = sc->inhabited_time;
        memcpy(c->update_skylight_columns, sc->skylight_columns, SNAP_COLUMNS);
        c->gap_lighting_updated = (uint8_t)sc->gap;

        /* The chunk's own tick state, which the byte layout does not carry:
         * queuedLightChecks is enqueueRelightChecks' cursor (4096 for a chunk
         * that has been through the generator, so the pass returns at once),
         * isLightPopulated gates Chunk.func_150809_p's light populate, and the
         * rest are the flags the world tick and the unload queue read. */
        c->queued_light_checks = sc->queued_light_checks;
        c->terrain_populated = (uint8_t)(sc->terrain_populated != 0);
        c->light_populated = (uint8_t)(sc->light_populated != 0);
        c->populated = (uint8_t)(sc->populated != 0);

        /* Chunk.readChunkFromNBT installs its tile entities in the world's
         * update list. The snapshot's chunkstate carries their full NBT;
         * leaving it in the parsed snapshot alone made every saved furnace
         * and spawner inert in a replay. */
        for (int t = 0; t < sc->ntiles; ++t)
        {
            const nbt *tag = sc->tiles[t].tag;
            int x = (int)nbt_int_value(nbt_get(tag, "x"));
            int y = (int)nbt_int_value(nbt_get(tag, "y"));
            int z = (int)nbt_int_value(nbt_get(tag, "z"));
            int block = world_get_block(w, x, y, z);
            int kind = te_kind_of_block(block);
            if (!kind) continue;
            struct tile_entity *te = te_new(kind);
            te->block = block;
            te_load(te, tag);
            snap_tile_runtime(te, sc->tiles[t].rt);
            for (int k = 0; te->kind == TE_CHEST && k < sc->nchests; ++k)
            {
                const int *q = &sc->chests[9 * k];
                if (q[0] != x || q[1] != y || q[2] != z) continue;
                struct te_chest *ch = &te->u.chest;
                ch->adjacent_checked = q[3];
                for (int b = 0; b < 4; ++b) ch->adj[b] = (q[4] >> b) & 1;
                ch->tick_counter = q[5];
                ch->players_using = q[6];
                memcpy(&ch->lid, &q[7], sizeof ch->lid);
                memcpy(&ch->prev_lid, &q[8], sizeof ch->prev_lid);
            }
            world_set_tile_entity(w, x, y, z, te);
        }
    }

    world_owed_last(w);
    return 1;
}

/* ----------------------------------------------------------------- entities */

/* clientents.jsonl, one parsed object per line; absent in older snapshots. */
static void load_client_ents(struct snapshot *s, const char *path)
{
    uint8_t *tbuf;
    FILE *f = text_open(path, &tbuf);
    if (!f) return;
    s->has_client_ents = 1;
    struct lines in;
    lines_init(&in);
    lines_file(&in, f);
    int cap = 0;
    for (const char *line = lines_next(&in); line; line = lines_next(&in))
    {
        if (!line[0]) continue;
        struct jval *v = json_parse(xstrndup(line, strlen(line)));
        if (!v) continue;
        if (s->nclient_ents == cap)
        {
            cap = cap ? cap * 2 : 64;
            s->client_ents = xrealloc(s->client_ents, (size_t)cap * sizeof *s->client_ents);
        }
        s->client_ents[s->nclient_ents++] = v;
    }
    text_close(f, tbuf);
    lines_free(&in);
}

static int rt_int(const struct jval *o, const char *key, int *out)
{
    int64_t v;
    if (!json_int(json_get(o, key), &v)) return 0;
    *out = (int)v;
    return 1;
}

static void rt_float(const struct jval *o, const char *key, float *out)
{
    uint32_t b;
    if (json_float(json_get(o, key), &b)) memcpy(out, &b, sizeof *out);
}

/* A tile entity's fields NBT does not carry (Snapshot.java's tilesrt):
 * TileEntityFurnace.currentItemBurnTime (a load recomputes it from the fuel
 * slot), a chest's viewer count, sync counter and lid, an enchanting
 * table's book. */
void snap_tile_runtime(struct tile_entity *te, const struct jval *rt)
{
    const char *cls = rt ? json_str(json_get(rt, "cls")) : NULL;
    if (!cls) return;
    if (!strncmp(cls, "str:", 4)) cls += 4;
    int v;
    if (!strcmp(cls, "TileEntityFurnace"))
    {
        if (rt_int(rt, "field_145963_i", &v)) te->u.furnace.fuel_total = v;
    }
    else if (!strcmp(cls, "TileEntityChest"))
    {
        if (rt_int(rt, "field_145987_o", &v)) te->u.chest.players_using = v;
        if (rt_int(rt, "field_145983_q", &v)) te->u.chest.tick_counter = v;
        rt_float(rt, "field_145989_m", &te->u.chest.lid);
        rt_float(rt, "field_145986_n", &te->u.chest.prev_lid);
    }
    else if (!strcmp(cls, "TileEntityEnchantmentTable"))
    {
        /* the book's state updateEntity carries from tick to tick: a
         * rejoin's table has turned toward the player since its load, and
         * the spread's climb past 0.5 decides the static Random's draws
         * (pfc-enchjoin-f59) */
        struct te_enchant *e = &te->u.enchant;
        if (rt_int(rt, "field_145926_a", &v)) e->book_spread = (float)v;
        rt_float(rt, "field_145933_i", &e->book_rotation_2);
        rt_float(rt, "field_145931_j", &e->prev_book_rotation);
        rt_float(rt, "field_145932_k", &e->flip);
        rt_float(rt, "field_145929_l", &e->flipT);
        rt_float(rt, "field_145930_m", &e->tRot);
        rt_float(rt, "field_145927_n", &e->ot);
        rt_float(rt, "field_145928_o", &e->book_rotation);
        rt_float(rt, "field_145924_q", &e->otilde);
    }
    else if (!strcmp(cls, "TileEntityEnderChest"))
    {
        if (rt_int(rt, "field_145973_j", &v)) te->u.chest.players_using = v;
        if (rt_int(rt, "field_145974_k", &v)) te->u.chest.tick_counter = v;
        rt_float(rt, "field_145972_a", &te->u.chest.lid);
        rt_float(rt, "field_145975_i", &te->u.chest.prev_lid);
    }
}

static int load_entities(struct snapshot *s, const char *path, struct snap_entity **ents, int *nents)
{
    struct lines in;
    lines_init(&in);
    uint8_t *tbuf;
    FILE *f = text_open(path, &tbuf);
    if (!f) { fprintf(stderr, "snapshot: cannot read %s\n", path); lines_free(&in); return 0; }

    lines_file(&in, f);
    int cap = 0, n = 0;

    for (const char *line = lines_next(&in); line; line = lines_next(&in))
    {
        if (!line[0]) continue;
        struct jval *v = json_parse(xstrndup(line, strlen(line)));
        if (!v || v->kind != J_OBJ) { fprintf(stderr, "snapshot: bad entity line %d\n", n + 1); json_free(v); text_close(f, tbuf); lines_free(&in); return 0; }

        if (n >= cap)
        {
            cap = n + 128;
            *ents = xrealloc(*ents, (size_t)cap * sizeof **ents);
        }

        struct snap_entity *e = &(*ents)[n];
        memset(e, 0, sizeof *e);
        int64_t iv;

        if (json_int(json_get(v, "dim"), &iv)) e->dim = (int)iv;
        if (json_int(json_get(v, "id"), &iv)) e->id = (int)iv;
        if (json_int(json_get(v, "rand"), &iv)) e->rand_state = (uint64_t)iv;
        if (json_int(json_get(v, "rand0"), &iv)) { e->rand_born = (uint64_t)iv; e->has_born = 1; }
        {
            uint64_t bits;
            if (json_double(json_get(v, "gauss"), &bits)) { memcpy(&e->gauss, &bits, sizeof e->gauss); e->has_gauss = 1; }
        }
        {
            /* "f:" and 8 hex digits: the raw float bits */
            const char *sz = json_str(json_get(v, "size"));
            if (sz && sz[0] == 'f' && sz[1] == ':')
            {
                uint32_t fb = (uint32_t)strtoul(sz + 2, NULL, 16);
                memcpy(&e->width, &fb, sizeof e->width);
            }
        }
        {
            const struct jval *pj = json_get(v, "path");
            int64_t pidx;
            if (pj && json_str(pj))
            {
                const char *s = json_str(pj);
                e->has_path = 1;
                e->path_n = 0;
                if (*s == 'i' && s[1] == 'a' && s[2] == ':') s += 3;
                int cap = 0;
                while (*s)
                {
                    char *end;
                    long x = strtol(s, &end, 10);
                    if (end == s || *end != ',') break;
                    s = end + 1;
                    long y = strtol(s, &end, 10);
                    if (end == s || *end != ',') break;
                    s = end + 1;
                    long z = strtol(s, &end, 10);
                    if (end == s) break;
                    s = (*end == ',') ? end + 1 : end;
                    if (e->path_n == cap)
                    {
                        cap = cap ? cap * 2 : 64;
                        e->path_pts = xrealloc(e->path_pts, (size_t)cap * sizeof *e->path_pts);
                    }
                    e->path_pts[e->path_n][0] = (int)x;
                    e->path_pts[e->path_n][1] = (int)y;
                    e->path_pts[e->path_n][2] = (int)z;
                    ++e->path_n;
                }
            }
            if (json_int(json_get(v, "pathi"), &pidx)) e->path_index = (int)pidx;
            uint64_t sbits;
            if (json_double(json_get(v, "pathspeed"), &sbits)) memcpy(&e->path_speed, &sbits, sizeof e->path_speed);
        }
        {
            const char *ws = json_str(json_get(v, "watch"));
            if (ws)
            {
                /* "watch": "<isPlayer>,<lookTime>" */
                const char *comma = strchr(ws, ',');
                if (comma)
                {
                    e->has_watch = 1;
                    e->watch_is_player = (int)strtol(ws, NULL, 10);
                    e->watch_look_time = (int)strtol(comma + 1, NULL, 10);
                }
            }
        }
        if (json_int(json_get(v, "player"), &iv)) e->player = (int)iv;
        if (json_int(json_get(v, "weather"), &iv)) e->weather = (int)iv;
        if (json_int(json_get(v, "age"), &iv)) { e->age = (int)iv; e->has_age = 1; }
        if (json_int(json_get(v, "lsf"), &iv)) e->lsf = (int)iv;
        {
            const char *cs = json_str(json_get(v, "crystal"));
            if (cs && sscanf(cs, "%d,%d", &e->crystal_rotation, &e->crystal_health) == 2) e->has_crystal = 1;
            const char *ts = json_str(json_get(v, "throwable"));
            if (ts && sscanf(ts, "%d,%d,%d,%d", &e->ticks_in_air, &e->ticks_in_ground, &e->thrower, &e->ticks_existed) == 4)
                e->has_throwable = 1;
            const char *ks = json_str(json_get(v, "trk"));
            const char *kd = json_str(json_get(v, "trkd"));
            if (ks && kd && sscanf(ks, "%d,%d,%d,%d,%d,%d,%d,%d,%d", &e->trk[0], &e->trk[1], &e->trk[2], &e->trk[3], &e->trk[4],
                                   &e->trk[5], &e->trk[6], &e->trk[7], &e->trk[8]) == 9)
            {
                unsigned long long b[6];
                if (sscanf(kd, "%llx,%llx,%llx,%llx,%llx,%llx", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6)
                {
                    for (int k = 0; k < 6; ++k) memcpy(&e->trkd[k], &b[k], sizeof e->trkd[k]);
                    e->has_trk = 1;
                }
            }
            const char *vs = json_str(json_get(v, "vil"));
            if (vs && sscanf(vs, "%d,%d,%d", &e->vil[0], &e->vil[1], &e->vil[2]) >= 2) e->has_vil = 1;
            const struct jval *dj = json_get(v, "dragon");
            if (dj && !snap_parse_dragon(dj, e->dragon = xrealloc(NULL, sizeof *e->dragon)))
            {
                fprintf(stderr, "snapshot: entity %d has a malformed dragon state\n", n);
                free(e->dragon); json_free(v); text_close(f, tbuf); lines_free(&in); return 0;
            }
        }
        const char *cls = json_str(json_get(v, "class"));
        e->cls = cls ? xstrndup(cls, strlen(cls)) : xstrndup("?", 1);

        {
            char *rt = json_raw(json_get(v, "rt"));
            e->rt = rt ? json_parse(rt) : NULL;
            char *trk = json_raw(json_get(v, "trkrt"));
            e->trk_rt = trk ? json_parse(trk) : NULL;
            e->cidx = json_int(json_get(v, "cidx"), &iv) ? (int)iv : -1;
        }
        const struct jval *tag = json_get(v, "nbt");
        e->text = json_raw(tag);
        e->tag = e->text ? nbt_parse(e->text) : NULL;
        if (!e->tag) { fprintf(stderr, "snapshot: entity %d has no canonical NBT\n", n); json_free(v); text_close(f, tbuf); lines_free(&in); return 0; }

        if (e->player) ++s->players;
        json_free(v);
        ++n;
    }

    text_close(f, tbuf);
    lines_free(&in);

    *nents = n;

    /* writeEntity's "dim" is the worldServers array index (d in
     * Snapshot.writeEntities), not the dimension id; remap it through the
     * manifest's server.dims, which records one dimension id per index. */
    {
        const struct jval *srv = json_get(s->manifest_json, "server");
        const struct jval *dims = srv ? json_get(srv, "dims") : NULL;
        int ndims = dims ? (int)json_len(dims) : 0;
        for (int i = 0; i < *nents; ++i)
        {
            if ((*ents)[i].dim >= 0 && (*ents)[i].dim < ndims)
            {
                int64_t id = 0;
                if (json_int(json_at(dims, (*ents)[i].dim), &id))
                    (*ents)[i].dim = (int)id;
            }
        }
    }
    return 1;
}

/* -------------------------------------------------------------------- ticks */

/* The fields of one pending-tick line read without building a JSON tree: a
 * flat object on one line whose values are plain integers or strings with no
 * escapes. v[k] and bit k of *have hold what json_int would read from key k
 * (a number, or the integer after a one-letter type prefix); TK_LIST reads 1
 * for "thisTick". 0 when the line has any other shape: the caller then parses
 * it with json_parse. */
enum { TK_DIM, TK_X, TK_Y, TK_Z, TK_ID, TK_PRI, TK_T, TK_EID, TK_CX, TK_CZ, TK_LIST, TK_N };

static int tick_key(const char *k, size_t n)
{
    switch (n)
    {
        case 1: return k[0] == 'x' ? TK_X : k[0] == 'y' ? TK_Y : k[0] == 'z' ? TK_Z : k[0] == 't' ? TK_T : -1;
        case 2:
            if (k[0] == 'i' && k[1] == 'd') return TK_ID;
            if (k[0] == 'c' && k[1] == 'x') return TK_CX;
            if (k[0] == 'c' && k[1] == 'z') return TK_CZ;
            return -1;
        case 3:
            if (k[0] == 'd' && k[1] == 'i' && k[2] == 'm') return TK_DIM;
            if (k[0] == 'p' && k[1] == 'r' && k[2] == 'i') return TK_PRI;
            if (k[0] == 'e' && k[1] == 'i' && k[2] == 'd') return TK_EID;
            return -1;
        case 4: return k[0] == 'l' && k[1] == 'i' && k[2] == 's' && k[3] == 't' ? TK_LIST : -1;
        default: return -1;
    }
}

/* strtoll(s, NULL, 10) for the text a tick line holds: an optional minus and
 * up to 18 digits read here, anything else (a sign or space first, more
 * digits) by strtoll itself */
static int64_t tick_dec(const char *s)
{
    const char *p = s + (*s == '-');
    int64_t v = 0;
    int n = 0;

    while (p[n] >= '0' && p[n] <= '9' && n < 19) v = v * 10 + (p[n] - '0'), ++n;
    if (n == 0 || n > 18) return strtoll(s, NULL, 10);
    return *s == '-' ? -v : v;
}

static int tick_fields(const char *p, int64_t *v, unsigned *have)
{
    *have = 0;
    if (*p++ != '{') return 0;
    if (*p == '}') return p[1] == 0;

    for (;;)
    {
        if (*p++ != '"') return 0;
        const char *k = p;
        while (*p && *p != '"' && *p != '\\') ++p;
        if (*p != '"') return 0;
        int key = tick_key(k, (size_t)(p - k));
        ++p;
        if (*p++ != ':') return 0;

        if (key >= 0 && (*have & 1u << key)) return 0;

        if (*p == '"')
        {
            const char *s = ++p;
            while (*p && *p != '"' && *p != '\\') ++p;
            if (*p != '"') return 0;
            size_t n = (size_t)(p - s);
            ++p;

            if (key == TK_LIST)
            {
                v[key] = n == 8 && memcmp(s, "thisTick", 8) == 0;
                *have |= 1u << key;
            }
            else if (key >= 0 && (s[0] == 'b' || s[0] == 's' || s[0] == 'i' || s[0] == 'l'))
            {
                if (n < 2) return 0;
                v[key] = tick_dec(s + 2);
                *have |= 1u << key;
            }
        }
        else
        {
            /* an integer, as strtod and the cast to int64_t read it: exact
             * up to 15 digits */
            const char *s = p;
            if (*p == '-') ++p;
            const char *d = p;
            while (*p >= '0' && *p <= '9') ++p;
            if (p == d || p - d > 15) return 0;
            if (*p != ',' && *p != '}') return 0;
            if (key == TK_LIST) return 0;
            if (key >= 0)
            {
                v[key] = tick_dec(s);
                *have |= 1u << key;
            }
        }

        if (*p == ',') { ++p; continue; }
        if (*p == '}') return p[1] == 0;
        return 0;
    }
}

/* The two lines the oracle writes, keys in order: a pending tick
 * {"dim":D,"eid":"l:E","id":I,"list":"L","pri":P,"t":"l:T","x":X,"y":Y,"z":Z}
 * and a seed tick {"cx":A,"cz":B,"dim":D,"id":I,"pri":P,"t":"l:T","x":X,
 * "y":Y,"z":Z}, read piece by piece; 0 (and tick_fields reads the line) at
 * the first byte that differs from that shape. Each value is what
 * tick_fields makes of it. */
#define TICK_LIT(p, s) ((p) != NULL && memcmp((p), (s), sizeof(s) - 1) == 0 ? (p) + sizeof(s) - 1 : NULL)

/* an unquoted integer (up to 15 digits, as tick_fields takes them) or, with
 * typed, the digits of a "l:" or "i:" value (up to 18) and its closing quote */
static const char *tick_num(const char *p, int64_t *v, int typed)
{
    if (p == NULL) return NULL;
    int neg = *p == '-';
    const char *d = p + neg;
    int64_t x = 0;
    int n = 0;
    while (d[n] >= '0' && d[n] <= '9' && n < 19) x = x * 10 + (d[n] - '0'), ++n;
    if (n == 0 || n > (typed ? 18 : 15)) return NULL;
    *v = neg ? -x : x;
    d += n;
    if (typed) return *d == '"' ? d + 1 : NULL;
    return *d == ',' || *d == '}' ? d : NULL;
}

static int tick_fast(const char *p, int64_t *v, unsigned *have)
{
    if (p[0] == '{' && p[1] == '"' && p[2] == 'd')
    {
        p = tick_num(TICK_LIT(p, "{\"dim\":"), &v[TK_DIM], 0);
        p = tick_num(TICK_LIT(p, ",\"eid\":\"l:"), &v[TK_EID], 1);
        p = tick_num(TICK_LIT(p, ",\"id\":"), &v[TK_ID], 0);
        p = TICK_LIT(p, ",\"list\":\"");
        if (p == NULL) return 0;
        if (memcmp(p, "pending\"", 8) == 0) { v[TK_LIST] = 0; p += 8; }
        else if (memcmp(p, "thisTick\"", 9) == 0) { v[TK_LIST] = 1; p += 9; }
        else return 0;
        *have = 1u << TK_DIM | 1u << TK_EID | 1u << TK_ID | 1u << TK_LIST;
    }
    else if (p[0] == '{' && p[1] == '"' && p[2] == 'c')
    {
        p = tick_num(TICK_LIT(p, "{\"cx\":"), &v[TK_CX], 0);
        p = tick_num(TICK_LIT(p, ",\"cz\":"), &v[TK_CZ], 0);
        p = tick_num(TICK_LIT(p, ",\"dim\":"), &v[TK_DIM], 0);
        p = tick_num(TICK_LIT(p, ",\"id\":"), &v[TK_ID], 0);
        *have = 1u << TK_CX | 1u << TK_CZ | 1u << TK_DIM | 1u << TK_ID;
    }
    else return 0;

    p = tick_num(TICK_LIT(p, ",\"pri\":"), &v[TK_PRI], 0);
    p = tick_num(TICK_LIT(p, ",\"t\":\"l:"), &v[TK_T], 1);
    p = tick_num(TICK_LIT(p, ",\"x\":"), &v[TK_X], 0);
    p = tick_num(TICK_LIT(p, ",\"y\":"), &v[TK_Y], 0);
    p = tick_num(TICK_LIT(p, ",\"z\":"), &v[TK_Z], 0);
    if (p == NULL || p[0] != '}' || p[1] != 0) return 0;
    *have |= 1u << TK_PRI | 1u << TK_T | 1u << TK_X | 1u << TK_Y | 1u << TK_Z;
    return 1;
}

static int load_ticks(struct snapshot *s, const char *path)
{
    struct lines in;
    lines_init(&in);

    struct gunzip *g = gunzip_open_crc(path, !s->nocrc);
    if (!g) { fprintf(stderr, "snapshot: cannot read %s\n", path); lines_free(&in); return 0; }

    lines_gunzip(&in, g);
    int cap = 0, n = 0, m = 0, mcap = 0;

    for (const char *line = lines_next(&in); line; line = lines_next(&in))
    {
        if (!line[0]) continue;

        struct snap_tick t;
        memset(&t, 0, sizeof t);
        int64_t iv, f[TK_N];
        unsigned have;
        struct jval *v = NULL;

        if (tick_fast(line, f, &have) || tick_fields(line, f, &have))
        {
            if (have & 1u << TK_LIST) t.list = (int)f[TK_LIST];
            if (have & 1u << TK_DIM) t.dim = (int)f[TK_DIM];
            if (have & 1u << TK_X) t.x = (int)f[TK_X];
            if (have & 1u << TK_Y) t.y = (int)f[TK_Y];
            if (have & 1u << TK_Z) t.z = (int)f[TK_Z];
            if (have & 1u << TK_ID) t.id = (int)f[TK_ID];
            if (have & 1u << TK_PRI) t.priority = (int)f[TK_PRI];
            if (have & 1u << TK_T) t.scheduled = f[TK_T];
            if (have & 1u << TK_EID) t.entry = f[TK_EID];
        }
        else
        {
            v = json_parse(xstrndup(line, strlen(line)));
            if (!v || v->kind != J_OBJ) { fprintf(stderr, "snapshot: bad tick line %d\n", n + m + 1); json_free(v); gunzip_close(g); lines_free(&in); return 0; }

            const char *list = json_str(json_get(v, "list"));
            t.list = list && !strcmp(list, "thisTick") ? 1 : 0;
            if (json_int(json_get(v, "dim"), &iv)) t.dim = (int)iv;
            if (json_int(json_get(v, "x"), &iv)) t.x = (int)iv;
            if (json_int(json_get(v, "y"), &iv)) t.y = (int)iv;
            if (json_int(json_get(v, "z"), &iv)) t.z = (int)iv;
            if (json_int(json_get(v, "id"), &iv)) t.id = (int)iv;
            if (json_int(json_get(v, "pri"), &iv)) t.priority = (int)iv;
            if (json_int(json_get(v, "t"), &iv)) t.scheduled = iv;
            if (json_int(json_get(v, "eid"), &iv)) t.entry = iv;
        }

        if (t.list)
        {
            if (m >= mcap)
            {
                mcap = m + 64;
                s->this_tick = xrealloc(s->this_tick, (size_t)mcap * sizeof *s->this_tick);
            }
            s->this_tick[m++] = t;
        }
        else
        {
            if (n >= cap)
            {
                cap = cap ? 2 * cap : 4096;
                s->ticks = xrealloc(s->ticks, (size_t)cap * sizeof *s->ticks);
            }
            s->ticks[n++] = t;
        }

        json_free(v);
    }

    gunzip_close(g);
    lines_free(&in);
    s->nticks = n;
    s->nthis = m;
    return 1;
}

static int load_seed_ticks(struct snapshot *s, const char *path)
{
    struct lines in;
    lines_init(&in);

    struct gunzip *g = gunzip_open_crc(path, !s->nocrc);
    if (!g) { fprintf(stderr, "snapshot: cannot read %s\n", path); lines_free(&in); return 0; }

    lines_gunzip(&in, g);
    int cap = 0, n = 0;

    for (const char *line = lines_next(&in); line; line = lines_next(&in))
    {
        if (!line[0]) continue;

        struct snap_seed_tick t;
        memset(&t, 0, sizeof t);
        int64_t iv, f[TK_N];
        unsigned have;
        struct jval *v = NULL;

        if (tick_fast(line, f, &have) || tick_fields(line, f, &have))
        {
            if (have & 1u << TK_DIM) t.dim = (int)f[TK_DIM];
            if (have & 1u << TK_CX) t.cx = (int)f[TK_CX];
            if (have & 1u << TK_CZ) t.cz = (int)f[TK_CZ];
            if (have & 1u << TK_X) t.x = (int)f[TK_X];
            if (have & 1u << TK_Y) t.y = (int)f[TK_Y];
            if (have & 1u << TK_Z) t.z = (int)f[TK_Z];
            if (have & 1u << TK_ID) t.id = (int)f[TK_ID];
            if (have & 1u << TK_PRI) t.priority = (int)f[TK_PRI];
            if (have & 1u << TK_T) t.t = f[TK_T];
        }
        else
        {
            v = json_parse(xstrndup(line, strlen(line)));
            if (!v || v->kind != J_OBJ) { fprintf(stderr, "snapshot: bad seed tick line %d\n", n + 1); json_free(v); gunzip_close(g); lines_free(&in); return 0; }

            if (json_int(json_get(v, "dim"), &iv)) t.dim = (int)iv;
            if (json_int(json_get(v, "cx"), &iv)) t.cx = (int)iv;
            if (json_int(json_get(v, "cz"), &iv)) t.cz = (int)iv;
            if (json_int(json_get(v, "x"), &iv)) t.x = (int)iv;
            if (json_int(json_get(v, "y"), &iv)) t.y = (int)iv;
            if (json_int(json_get(v, "z"), &iv)) t.z = (int)iv;
            if (json_int(json_get(v, "id"), &iv)) t.id = (int)iv;
            if (json_int(json_get(v, "pri"), &iv)) t.priority = (int)iv;
            if (json_int(json_get(v, "t"), &iv)) t.t = iv;
        }

        if (n >= cap)
        {
            cap = cap ? 2 * cap : 4096;
            s->seed_ticks = xrealloc(s->seed_ticks, (size_t)cap * sizeof *s->seed_ticks);
        }
        s->seed_ticks[n++] = t;

        json_free(v);
    }

    gunzip_close(g);
    lines_free(&in);
    s->nseed_ticks = n;
    return 1;
}

/* seedents.jsonl.gz: one line per unloaded overworld chunk, cx, cz and its
 * region Entities. */
static int load_seed_ents(struct snapshot *s, const char *path)
{
    struct lines in;
    lines_init(&in);

    struct gunzip *g = gunzip_open_crc(path, !s->nocrc);
    if (!g) { fprintf(stderr, "snapshot: cannot read %s\n", path); lines_free(&in); return 0; }

    lines_gunzip(&in, g);
    int cap = 0, n = 0;

    for (const char *line = lines_next(&in); line; line = lines_next(&in))
    {
        if (!line[0]) continue;

        struct jval *v = json_parse(xstrndup(line, strlen(line)));
        int64_t iv;
        if (!v || v->kind != J_OBJ) { fprintf(stderr, "snapshot: bad seed entity line %d\n", n + 1); json_free(v); gunzip_close(g); lines_free(&in); return 0; }

        struct snap_seed_chunk c;
        memset(&c, 0, sizeof c);
        if (json_int(json_get(v, "cx"), &iv)) c.cx = (int)iv;
        if (json_int(json_get(v, "cz"), &iv)) c.cz = (int)iv;
        const struct jval *ents = json_get(v, "ents");
        c.nents = ents ? json_len(ents) : 0;
        if (c.nents > 0)
        {
            c.ents = xmalloc((size_t)c.nents * sizeof *c.ents);
            for (int i = 0; i < c.nents; ++i)
            {
                char *raw = json_raw(json_at(ents, i));
                c.ents[i] = raw ? nbt_parse(raw) : NULL;
                free(raw);
                if (!c.ents[i])
                {
                    fprintf(stderr, "snapshot: seed chunk (%d,%d) entity %d is not canonical NBT\n", c.cx, c.cz, i);
                    for (int k = 0; k < i; ++k) nbt_free(c.ents[k]);
                    free(c.ents);
                    json_free(v); gunzip_close(g); lines_free(&in); return 0;
                }
            }
        }

        if (n >= cap)
        {
            cap = cap ? 2 * cap : 1024;
            s->seed_chunks = xrealloc(s->seed_chunks, (size_t)cap * sizeof *s->seed_chunks);
        }
        s->seed_chunks[n++] = c;
        json_free(v);
    }

    gunzip_close(g);
    lines_free(&in);
    s->nseed_chunks = n;
    return 1;
}

/* ------------------------------------------------------------------- loader */

int snapshot_load(struct snapshot *s, const char *dir)
{
    return snapshot_load_crc(s, dir, 1);
}

int snapshot_load_crc(struct snapshot *s, const char *dir, int check_crc)
{
    memset(s, 0, sizeof *s);
    s->nocrc = !check_crc;
    snprintf(s->dir, sizeof s->dir, "%s", dir);

    char path[1200];

    path_join(path, sizeof path, dir, "manifest.json");
    s->manifest = read_text(path);
    if (!s->manifest) { fprintf(stderr, "snapshot: cannot read %s\n", path); return 0; }
    s->manifest_json = json_parse(xstrndup(s->manifest, strlen(s->manifest)));

    if (!s->manifest_json || s->manifest_json->kind != J_OBJ)
    {
        fprintf(stderr, "snapshot: %s is not a JSON object\n", path);
        return 0;
    }

    const char *kind = json_str(json_get(s->manifest_json, "kind"));
    if (!kind || strcmp(kind, "netherite-snapshot"))
    {
        fprintf(stderr, "snapshot: %s is not a netherite snapshot\n", path);
        return 0;
    }

    if (!manifest_int(s->manifest_json, "seed", &s->seed) || !manifest_int(s->manifest_json, "tick", &s->tick))
    {
        fprintf(stderr, "snapshot: %s has no seed or tick\n", path);
        return 0;
    }
    {
        int64_t v = 1;
        manifest_int(s->manifest_json, "v", &v);
        s->version = (int)v;
    }

    path_join(path, sizeof path, dir, "chunkstate.jsonl");
    if (!load_chunkstate(s, path)) return 0;

    path_join(path, sizeof path, dir, "chunks.bin.gz");
    if (access(path, R_OK) != 0)
    {
        path_join(path, sizeof path, dir, "chunks.idx");
        if (!load_chunk_idx(s, dir, path)) return 0;
    }
    else if (!load_chunk_bytes(s, path)) return 0;

    /* The Nether's and the End's region-file chunks (saved:1), when the
     * snapshot has them: the same listing and byte format, appended to
     * s->chunks after the loaded ones. */
    path_join(path, sizeof path, dir, "saved-chunkstate.jsonl");
    if (text_exists(path))
    {
        struct snapshot *extra = xmalloc(sizeof *extra);
        memset(extra, 0, sizeof *extra);
        extra->seed = s->seed;
        extra->tick = s->tick;
        extra->version = s->version;
        if (!load_chunkstate(extra, path)) { snapshot_free(extra); free(extra); return 0; }
        path_join(path, sizeof path, dir, "saved-chunks.bin.gz");
        int ok;
        if (access(path, R_OK) != 0)
        {
            path_join(path, sizeof path, dir, "saved-chunks.idx");
            ok = load_chunk_idx(extra, dir, path);
        }
        else ok = load_chunk_bytes(extra, path);
        if (!ok) { snapshot_free(extra); free(extra); return 0; }
        int base = s->nchunks;
        s->chunks = xrealloc(s->chunks, (size_t)(base + extra->nchunks) * sizeof *s->chunks);
        memcpy(s->chunks + base, extra->chunks, (size_t)extra->nchunks * sizeof *s->chunks);
        s->nchunks = base + extra->nchunks;
        free(extra->chunks);
        free(extra);
    }

    world_init(&s->world, s->seed);
    s->world.dim = 0;

    for (int d = -1; d <= 1; d += 2)
    {
        int has = 0, saved = 0;
        for (int i = 0; i < s->nchunks; ++i)
        {
            if (s->chunks[i].dim != d) continue;
            if (s->chunks[i].saved) saved = 1;
            else has = 1;
        }
        /* the loaded world exists even when empty: a region chunk's 3x3
         * hash reads its neighbours there */
        struct world *lw = d == -1 ? &s->hell_world : &s->end_world;
        world_init(lw, s->seed);
        lw->dim = d;
        if (has)
        {
            if (!load_world(s, lw, 0)) return 0;
            if (d == -1) s->hell_loaded = 1;
            else s->end_loaded = 1;
        }
        if (saved)
        {
            struct world *w = d == -1 ? (s->hell_saved = xmalloc(sizeof *w)) : (s->end_saved = xmalloc(sizeof *w));
            world_init(w, s->seed);
            w->dim = d;
            if (!load_world(s, w, 1)) return 0;
        }
    }

    if (!load_world(s, &s->world, 0)) return 0;

    path_join(path, sizeof path, dir, "entities.jsonl");
    if (!load_entities(s, path, &s->ents, &s->nents)) return 0;
    path_join(path, sizeof path, dir, "ghosts.jsonl");
    if (text_exists(path) && !load_entities(s, path, &s->ghosts, &s->nghosts)) return 0;
    path_join(path, sizeof path, dir, "clientents.jsonl");
    load_client_ents(s, path);

    path_join(path, sizeof path, dir, "ticks.jsonl.gz");
    if (!load_ticks(s, path)) return 0;

    /* v3: the saved pending ticks of the unloaded chunks. A snapshot from
     * before the field exists leaves the list empty, which is what those
     * snapshots' replays assumed. */
    path_join(path, sizeof path, dir, "seedticks.jsonl.gz");
    s->has_seedticks = access(path, R_OK) == 0;
    if (s->has_seedticks && !load_seed_ticks(s, path)) return 0;

    /* the unloaded overworld chunks' region Entities (lane/seedgen); an
     * older snapshot rebuilds them from the seed world */
    path_join(path, sizeof path, dir, "seedents.jsonl.gz");
    s->has_seedents = access(path, R_OK) == 0;
    if (s->has_seedents && !load_seed_ents(s, path)) return 0;

    if (!parse_nbt_file(dir, "worldinfo.nbt", &s->worldinfo)) return 0;
    if (!parse_nbt_file(dir, "worldstate.nbt", &s->worldstate)) return 0;
    if (!parse_nbt_file(dir, "det.nbt", &s->det)) return 0;
    if (!parse_nbt_file(dir, "clientworld.nbt", &s->clientworld)) return 0;
    if (!parse_nbt_file(dir, "player_client.nbt", &s->player_client)) return 0;
    if (!parse_nbt_file(dir, "player_server.nbt", &s->player_server)) return 0;

    path_join(path, sizeof path, dir, "stats.json");
    s->stats_json = access(path, R_OK) == 0 ? read_text(path) : NULL;

    return 1;
}

void snapshot_drop_runtime(struct snapshot *s)
{
    for (int i = 0; i < s->nclient_ents; ++i) json_free(s->client_ents[i]);
    free(s->client_ents);
    s->client_ents = NULL;
    s->nclient_ents = 0;
    for (int g = 0; g < 2; ++g)
    {
        struct snap_entity *ents = g ? s->ghosts : s->ents;
        int n = g ? s->nghosts : s->nents;
        for (int i = 0; i < n; ++i)
        {
            json_free(ents[i].rt);
            json_free(ents[i].trk_rt);
            ents[i].rt = ents[i].trk_rt = NULL;
        }
    }
}

void snapshot_free(struct snapshot *s)
{
    for (int i = 0; i < s->nclient_ents; ++i) json_free(s->client_ents[i]);
    free(s->client_ents);
    s->client_ents = NULL;
    s->nclient_ents = 0;
    for (int i = 0; i < s->nchunks; ++i)
    {
        for (int t = 0; t < s->chunks[i].ntiles; ++t)
        {
            nbt_free(s->chunks[i].tiles[t].tag);
            free(s->chunks[i].tiles[t].text);
            json_free(s->chunks[i].tiles[t].rt);
        }
        free(s->chunks[i].tiles);
        free(s->chunks[i].chests);
        for (int t = 0; t < s->chunks[i].nents_saved; ++t) nbt_free(s->chunks[i].ents[t]);
        free(s->chunks[i].ents);
        free(s->chunks[i].saved_ticks);
        free(s->chunks[i].bytes);
        chunk_free(s->chunks[i].chunk);
    }

    free(s->chunks);

    for (int g = 0; g < 2; ++g)
    {
        struct snap_entity *ents = g ? s->ghosts : s->ents;
        int n = g ? s->nghosts : s->nents;
        for (int i = 0; i < n; ++i)
        {
            nbt_free(ents[i].tag);
            free(ents[i].text);
            json_free(ents[i].rt);
            json_free(ents[i].trk_rt);
            free(ents[i].dragon);
            free(ents[i].path_pts);
            free(ents[i].cls);
        }
        free(ents);
    }
    free(s->ticks);
    free(s->this_tick);
    free(s->seed_ticks);
    for (int i = 0; i < s->nseed_chunks; ++i)
    {
        for (int k = 0; k < s->seed_chunks[i].nents; ++k) nbt_free(s->seed_chunks[i].ents[k]);
        free(s->seed_chunks[i].ents);
    }
    free(s->seed_chunks);
    world_free(&s->world);
    world_free(&s->hell_world);
    world_free(&s->end_world);
    if (s->hell_saved) { world_free(s->hell_saved); free(s->hell_saved); }
    if (s->end_saved) { world_free(s->end_saved); free(s->end_saved); }
    nbt_free(s->worldinfo);
    nbt_free(s->worldstate);
    nbt_free(s->det);
    nbt_free(s->clientworld);
    nbt_free(s->player_client);
    nbt_free(s->player_server);
    free(s->stats_json);
    json_free(s->manifest_json);
    free(s->manifest);
    memset(s, 0, sizeof *s);
}

/* -------------------------------------------------------------------- bytes */

void snapshot_chunk_bytes(const struct chunk *c, uint8_t *out)
{
#ifdef CHUNK_IDS_LE
    chunk_cells_out(c, (uint16_t *)(void *)out, out + 2 * CHUNK_CELLS, out + 3 * CHUNK_CELLS, out + 4 * CHUNK_CELLS);
#else
    for (int k = 0; k < CHUNK_CELLS; ++k)
    {
        int id = chunk_cell_id(c, k);
        out[2 * k] = (uint8_t)id;
        out[2 * k + 1] = (uint8_t)(id >> 8);
        out[2 * CHUNK_CELLS + k] = (uint8_t)chunk_cell_meta(c, k);
        out[3 * CHUNK_CELLS + k] = (uint8_t)chunk_cell_sky(c, k);
        out[4 * CHUNK_CELLS + k] = (uint8_t)chunk_cell_blocklight(c, k);
    }
#endif
    snapshot_chunk_tail(c, out + 5 * CHUNK_CELLS);
}

/* The layout after the cells: heightMap, precipitationHeightMap,
 * heightMapMinimum, the mask, into out[SNAP_CHUNK_TAIL]. */
void snapshot_chunk_tail(const struct chunk *c, uint8_t *out)
{
    int o = 0;

    for (int k = 0; k < 256; ++k)
    {
        int32_t h = c->height[k], p = c->precip[k];
        for (int b = 0; b < 4; ++b)
        {
            out[o + b] = (uint8_t)(h >> (8 * b));
            out[o + 1024 + b] = (uint8_t)(p >> (8 * b));
        }
        o += 4;
    }

    int32_t m = c->height_min;
    for (int b = 0; b < 4; ++b) out[o + 1024 + b] = (uint8_t)(m >> (8 * b));
    out[o + 1028] = (uint8_t)c->mask;
    out[o + 1029] = (uint8_t)(c->mask >> 8);
}

void snapshot_chunk_from_bytes(struct chunk *c, const uint8_t *p)
{
#ifdef CHUNK_IDS_LE
    chunk_cells_in(c, (const uint16_t *)(const void *)p, p + 2 * CHUNK_CELLS, p + 3 * CHUNK_CELLS, p + 4 * CHUNK_CELLS);
#else
    static uint16_t ids[CHUNK_CELLS];
    for (int k = 0; k < CHUNK_CELLS; ++k) ids[k] = (uint16_t)(p[2 * k] | p[2 * k + 1] << 8);
    chunk_cells_in(c, ids, p + 2 * CHUNK_CELLS, p + 3 * CHUNK_CELLS, p + 4 * CHUNK_CELLS);
#endif

    int o = 5 * CHUNK_CELLS;

    for (int k = 0; k < 256; ++k)
    {
        c->height[k] = (int32_t)(p[o] | p[o + 1] << 8 | p[o + 2] << 16 | (unsigned)p[o + 3] << 24);
        c->precip[k] = (int32_t)(p[o + 1024] | p[o + 1025] << 8 | p[o + 1026] << 16 | (unsigned)p[o + 1027] << 24);
        o += 4;
    }

    c->height_min = (int32_t)(p[o + 1024] | p[o + 1025] << 8 | p[o + 1026] << 16 | (unsigned)p[o + 1027] << 24);
    c->mask = (uint16_t)(p[o + 1028] | p[o + 1029] << 8);
}

const uint8_t *snapshot_chunk_want(const struct snap_chunk *sc, uint8_t *buf)
{
    if (sc->bytes != NULL) return sc->bytes;
    if (sc->live == NULL) return NULL;
    snapshot_chunk_bytes(sc->live, buf);
    return buf;
}

/* FNV-1a 64 continued over n bytes. Most chunk bytes are zero (air, empty
 * light and metadata: about 93% of them), and a zero byte only multiplies the
 * state by the prime, so a run of k of them is one multiply by P^k. The chain
 * takes each nonzero byte with the run after it, (h ^ b) * P^(1 + run), which
 * is the same arithmetic modulo 2^64 as byte by byte. P^m for m < 2^20 is
 * pow_lo[m & 1023] * pow_hi[m >> 10]. */
static uint64_t pow_lo[1024], pow_hi[1024];

static uint64_t fnv_pow(size_t m)
{
    return pow_lo[m & 1023] * pow_hi[m >> 10];
}

/* before main: the same for every environment */
__attribute__((constructor)) static void fnv_pow_init(void)
{
    uint64_t x = 1, y = 1;

    for (int i = 0; i < 1024; ++i) { pow_lo[i] = x; x *= FNV_PRIME; }
    for (int i = 0; i < 1024; ++i) { pow_hi[i] = y; y *= x; }
}

/* The chain over a stream of pieces: every byte before pos is in h, b is the
 * byte at pos, every byte from pos + 1 to at (exclusive) is zero. A stream
 * starts at pos 0 with b 0, which is the same as starting with its first
 * byte pending. */
struct fnv_rec;
struct fnv_stream {
    uint64_t h, b;
    size_t pos, at;
    struct fnv_rec *rec;   /* record each step instead of taking it */
};

/* A chunk's chain recorded (snapshot_chunk_hash_around_all): the steps
 * h = (h ^ b[i]) * m[i] fnv_chunk takes, which give its chain from any
 * starting h without reading the chunk again. */
struct fnv_rec {
    uint8_t *b;
    uint64_t *m;
    size_t n, cap;
};

__attribute__((noinline)) static void fnv_rec_grow(struct fnv_rec *r)
{
    r->cap = r->cap ? 2 * r->cap : 65536;
    r->b = xrealloc(r->b, r->cap);
    r->m = xrealloc(r->m, r->cap * sizeof *r->m);
}

static inline void fnv_rec_push(struct fnv_rec *r, uint8_t b, uint64_t m)
{
    if (r->n == r->cap) fnv_rec_grow(r);
    r->b[r->n] = b;
    r->m[r->n] = m;
    ++r->n;
}

static inline void fnv_step(struct fnv_stream *f, uint64_t mult)
{
    if (f->rec != NULL) fnv_rec_push(f->rec, (uint8_t)f->b, mult);
    else f->h = (f->h ^ f->b) * mult;
}

static void fnv_feed(struct fnv_stream *f, const uint8_t *p, size_t n)
{
    size_t i = 0;

    for (; i + 8 <= n; i += 8)
    {
        uint64_t w;

        memcpy(&w, p + i, 8);
        if (w == 0) continue;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uint64_t nz = (((w & 0x7f7f7f7f7f7f7f7fULL) + 0x7f7f7f7f7f7f7f7fULL) | w) & 0x8080808080808080ULL;

        do
        {
            size_t q = f->at + i + ((size_t)__builtin_ctzll(nz) >> 3);
            fnv_step(f, fnv_pow(q - f->pos));
            f->b = p[q - f->at];
            f->pos = q;
            nz &= nz - 1;
        } while (nz);
#else
        for (size_t q = i; q < i + 8; ++q)
            if (p[q]) { fnv_step(f, fnv_pow(f->at + q - f->pos)); f->b = p[q]; f->pos = f->at + q; }
#endif
    }

    for (; i < n; ++i)
        if (p[i]) { fnv_step(f, fnv_pow(f->at + i - f->pos)); f->b = p[i]; f->pos = f->at + i; }

    f->at += n;
}

static uint64_t fnv_end(struct fnv_stream *f)
{
    fnv_step(f, fnv_pow(f->at - f->pos));
    return f->h;
}

uint64_t snapshot_fnv(uint64_t h, const uint8_t *p, size_t n)
{
    if (n == 0) return h;
    if (n >= (size_t)1 << 20)
    {
        for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * FNV_PRIME;
        return h;
    }

    struct fnv_stream f = {h, 0, 0, 0, NULL};

    fnv_feed(&f, p, n);
    return fnv_end(&f);
}

/* One nonzero byte at stream position q (q past every byte fed so far). */
static inline void fnv_put(struct fnv_stream *f, size_t q, uint64_t b)
{
    fnv_step(f, fnv_pow(q - f->pos));
    f->b = b;
    f->pos = q;
}

/* A band column's bytes in the layout, from f->at: the ids as 16 little-endian
 * pairs (field 0) or one nibble per byte (metas, sky, block light); only the
 * nonzero ones touch the chain. f->at is left for the caller to advance. */
static void fnv_band_column(struct fnv_stream *f, const struct chunk_sec *sec, int field, int col)
{
    int o = col << 4;

    if (field == 0)
    {
        const uint8_t *lo = sec->ids + o;
        for (int y = 0; y < 16; ++y)
        {
            int hi = sec->ids_hi ? nibble_get(sec->ids_hi, o + y) : 0;
            if (lo[y]) fnv_put(f, f->at + 2 * (size_t)y, lo[y]);
            if (hi) fnv_put(f, f->at + 2 * (size_t)y + 1, (uint64_t)hi);
        }
        return;
    }

    const uint8_t *nb = chunk_sec_nib(sec, field == 1 ? NIB_METAS : field == 2 ? NIB_SKY : NIB_BLOCK) + (o >> 1);
    uint64_t word;
    memcpy(&word, nb, 8);
    if (word == 0) return;
    for (int k = 0; k < 8; ++k)
    {
        if (nb[k] == 0) continue;
        if (nb[k] & 15) fnv_put(f, f->at + 2 * (size_t)k, nb[k] & 15);
        if (nb[k] >> 4) fnv_put(f, f->at + 2 * (size_t)k + 1, nb[k] >> 4);
    }
}

/* The chain over one chunk's layout, read from its bands: a band with no
 * storage is a run of zeros. */
static uint64_t fnv_chunk_s(struct fnv_stream *fs, const struct chunk *c);

static uint64_t fnv_chunk(uint64_t h, const struct chunk *c)
{
    struct fnv_stream f = {h, 0, 0, 0, NULL};
    return fnv_chunk_s(&f, c);
}

static uint64_t fnv_chunk_s(struct fnv_stream *fs, const struct chunk *c)
{
#define f (*fs)
    const struct chunk_sec *secs[16];

    for (int s = 0; s < 16; ++s) secs[s] = chunk_sec_at(c, s);
    for (int field = 0; field < 4; ++field)
    {
        size_t w = field == 0 ? 32 : 16;

        for (int col = 0; col < 256; ++col)
            for (int s = 0; s < 16; ++s)
            {
                const struct chunk_sec *sec = secs[s];

                if (sec != NULL) fnv_band_column(&f, sec, field, col);
                f.at += w;
            }
    }

    uint8_t tail[SNAP_CHUNK_TAIL];

    snapshot_chunk_tail(c, tail);
    fnv_feed(&f, tail, SNAP_CHUNK_TAIL);
    return fnv_end(&f);
#undef f
}

/* FNV-1a 64 continued over a chunk's bytes in each of n lanes, or one zero
 * byte for a NULL chunk. */
static void fnv_lanes(uint64_t *h, const struct chunk *const *c, int n)
{
    for (int l = 0; l < n; ++l)
        h[l] = c[l] == NULL ? h[l] * FNV_PRIME : fnv_chunk(h[l], c[l]);
}

uint64_t snapshot_chunk_hash(const struct chunk *c)
{
    uint64_t h = FNV_OFFSET;
    fnv_lanes(&h, &c, 1);
    return h;
}

void snapshot_chunk_hash_n(const struct chunk *const *c, int n, uint64_t *out)
{
    for (int i = 0; i < n; i += SNAP_HASH_LANES)
    {
        int k = n - i < SNAP_HASH_LANES ? n - i : SNAP_HASH_LANES;

        for (int l = 0; l < k; ++l) out[i + l] = FNV_OFFSET;

        fnv_lanes(out + i, c + i, k);
    }
}

uint64_t snapshot_chunk_hash_around(struct world *w, int chx, int chz)
{
    uint64_t h;
    snapshot_chunk_hash_around_n(&w, &chx, &chz, 1, &h);
    return h;
}

void snapshot_chunk_hash_around_n(struct world *const *w, const int *chx, const int *chz, int n, uint64_t *out)
{
    for (int i = 0; i < n; i += SNAP_HASH_LANES)
    {
        int k = n - i < SNAP_HASH_LANES ? n - i : SNAP_HASH_LANES;

        for (int l = 0; l < k; ++l) out[i + l] = FNV_OFFSET;

        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
            {
                const struct chunk *c[SNAP_HASH_LANES];

                for (int l = 0; l < k; ++l) c[l] = world_chunk(w[i + l], chx[i + l] + dx, chz[i + l] + dz);

                fnv_lanes(out + i, c, k);
            }
    }
}

/* The chain from h through a recorded chunk. */
static uint64_t fnv_rec_apply(const struct fnv_rec *r, uint64_t h)
{
    for (size_t i = 0; i < r->n; ++i) h = (h ^ r->b[i]) * r->m[i];
    return h;
}

struct around_key {
    struct world *w;
    int x, z, i;
};

static int around_cmp(const void *a, const void *b)
{
    const struct around_key *p = a, *q = b;
    if (p->w != q->w) return (uintptr_t)p->w < (uintptr_t)q->w ? -1 : 1;
    if (p->x != q->x) return p->x < q->x ? -1 : 1;
    if (p->z != q->z) return p->z < q->z ? -1 : 1;
    return p->i < q->i ? -1 : p->i > q->i;
}

struct around_slot {
    const struct chunk *c;
    int cx, used;
    struct fnv_rec rec;
};

void snapshot_chunk_hash_around_all(struct world *const *w, const int *chx, const int *chz, int n, uint64_t *out,
                                    const struct chunk *const *own, uint64_t *own_out)
{
    struct around_key *k = xmalloc((size_t)(n ? n : 1) * sizeof *k);
    for (int i = 0; i < n; ++i) k[i] = (struct around_key){w[i], chx[i], chz[i], i};
    qsort(k, (size_t)n, sizeof *k, around_cmp);

    /* the recorded chunks of columns x - 1 .. x + 1 of the current world:
     * windows come in (world, x, z) order, so a column left behind is not
     * read again */
    int nslot = 0, capslot = 64;
    struct around_slot *slot = calloc((size_t)capslot, sizeof *slot);
    if (slot == NULL) { fprintf(stderr, "snapshot: out of memory\n"); exit(1); }
    struct world *cur = NULL;
    uint8_t *own_done = calloc((size_t)(n ? n : 1), 1);
    if (own_done == NULL) { fprintf(stderr, "snapshot: out of memory\n"); exit(1); }

    for (int j = 0; j < n; ++j)
    {
        const struct around_key *q = &k[j];

        for (int t = 0; t < nslot; ++t)
            if (slot[t].used && (q->w != cur || slot[t].cx < q->x - 1)) { slot[t].used = 0; slot[t].c = NULL; }
        cur = q->w;

        uint64_t h = FNV_OFFSET;

        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
            {
                const struct chunk *c = world_chunk(q->w, q->x + dx, q->z + dz);

                if (c == NULL) { h *= FNV_PRIME; continue; }

                struct around_slot *sl = NULL;
                for (int t = 0; t < nslot && sl == NULL; ++t)
                    if (slot[t].used && slot[t].c == c) sl = &slot[t];
                if (sl == NULL)
                {
                    for (int t = 0; t < nslot && sl == NULL; ++t)
                        if (!slot[t].used) sl = &slot[t];
                    if (sl == NULL)
                    {
                        if (nslot == capslot)
                        {
                            slot = xrealloc(slot, (size_t)capslot * 2 * sizeof *slot);
                            memset(slot + capslot, 0, (size_t)capslot * sizeof *slot);
                            capslot *= 2;
                        }
                        sl = &slot[nslot++];
                    }
                    sl->c = c;
                    sl->cx = q->x + dx;
                    sl->used = 1;
                    sl->rec.n = 0;

                    struct fnv_stream f = {0, 0, 0, 0, &sl->rec};
                    fnv_chunk_s(&f, c);
                }
                h = fnv_rec_apply(&sl->rec, h);
                /* the center's own hash from the same record, when own[i]
                 * is that chunk */
                if (dx == 0 && dz == 0 && own != NULL && own[q->i] == c)
                {
                    own_out[q->i] = fnv_rec_apply(&sl->rec, FNV_OFFSET);
                    own_done[q->i] = 1;
                }
            }

        out[q->i] = h;
    }

    for (int t = 0; t < nslot; ++t)
    {
        free(slot[t].rec.b);
        free(slot[t].rec.m);
    }
    free(slot);
    free(k);

    /* an own chunk that is no window's center (a saved chunk) */
    for (int i = 0; own != NULL && i < n; ++i)
        if (!own_done[i]) own_out[i] = own[i] == NULL ? FNV_OFFSET * FNV_PRIME : fnv_chunk(FNV_OFFSET, own[i]);
    free(own_done);
}

/* The eight regions of the layout, for the first differing byte. */
static const struct { const char *name; int off, len; } REGIONS[] = {
    {"ids", 0, 2 * CHUNK_CELLS},
    {"metas", 2 * CHUNK_CELLS, CHUNK_CELLS},
    {"skylight", 3 * CHUNK_CELLS, CHUNK_CELLS},
    {"blocklight", 4 * CHUNK_CELLS, CHUNK_CELLS},
    {"heightMap", 5 * CHUNK_CELLS, 1024},
    {"precipitationHeightMap", 5 * CHUNK_CELLS + 1024, 1024},
    {"heightMapMinimum", 5 * CHUNK_CELLS + 2048, 4},
    {"mask", 5 * CHUNK_CELLS + 2052, 2},
};

int snapshot_chunk_diff(const uint8_t *want, const uint8_t *got, char *out, size_t outn)
{
    if (memcmp(want, got, SNAP_CHUNK_BYTES) == 0) return 0;

    for (size_t r = 0; r < sizeof REGIONS / sizeof REGIONS[0]; ++r)
    {
        for (int i = 0; i < REGIONS[r].len; ++i)
        {
            if (want[REGIONS[r].off + i] == got[REGIONS[r].off + i]) continue;
            if (!strcmp(REGIONS[r].name, "ids"))
            {
                int cell = i / 2, base = cell * 2;
                unsigned wi = (unsigned)want[base] | (unsigned)want[base + 1] << 8;
                unsigned gi = (unsigned)got[base] | (unsigned)got[base + 1] << 8;
                snprintf(out, outn, "ids x=%d z=%d y=%d want %u got %u", cell >> 12, cell >> 8 & 15, cell & 255, wi, gi);
            }
            else
                snprintf(out, outn, "%s byte %d want %u got %u", REGIONS[r].name, i, want[REGIONS[r].off + i], got[REGIONS[r].off + i]);
            return 1;
        }
    }

    return 0;
}
