/* The 1.7.10 chunk mesher, ported from RenderBlocks and Tessellator. See
 * render_blocks.h for the contract; the comments below name the Java method each
 * block reproduces, and the arithmetic follows it operation by operation,
 * including the float/double split (Java computes geometry in double and casts
 * to float only where the Tessellator stores it) and the order of the
 * multiplications. -ffp-contract=off keeps C from fusing anything.
 */
#include "render_blocks.h"

#include "render_blocks_int.h"

#include "blocks.h"
#include "world.h"
#include "tileentity.h"
#include "jrand.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ helpers */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int32_t rd32(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    int32_t r;
    memcpy(&r, &v, 4);
    return r;
}

static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/** Float.floatToRawIntBits: the bit pattern, never a conversion. */
static int32_t fbits(float f)
{
    int32_t v;
    memcpy(&v, &f, 4);
    return v;
}

static float ffrom(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static void die(const char *what, const char *dir)
{
    fprintf(stderr, "render_blocks: %s%s%s\n", what, dir ? " " : "", dir ? dir : "");
}

/** Material index by name, or -1. */
static int material_by_name(const char *name)
{
    for (int i = 0; i < (int)(sizeof MATERIALS / sizeof MATERIALS[0]); ++i)
    {
        if (strcmp(MATERIALS[i].name, name) == 0) return i;
    }
    return -1;
}

/* Java's `(int)` on a float/double truncates toward zero, like C. */

static int read_graphics(const char *dir, int *fancy, int *ao)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)len + 1);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len)
    { fclose(f); free(b); die("short manifest.json", dir); return -1; }
    fclose(f);
    b[len] = 0;
    char *p = strstr(b, "\"graphics\":\"fancy:");
    int ok = p && sscanf(p, "\"graphics\":\"fancy:%d ao:%d", fancy, ao) == 2
        && (*fancy == 0 || *fancy == 1) && (*ao == 0 || *ao == 2);
    free(b);
    if (!ok) { die("unsupported graphics options in", dir); return -1; }
    return 0;
}

/* ------------------------------------------------------------- table.bin */

int rb_table_load(struct rb_table *t, const char *dir)
{
    memset(t, 0, sizeof *t);
    if (read_graphics(dir, &t->fancy, &t->ao) != 0) return -1;
    char path[1200];
    snprintf(path, sizeof path, "%s/table.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)len);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); die("short table.bin", dir); return -1; }
    fclose(f);

    size_t o = 0;
    if (len < 28 || rd32(b) != 0x3142544d) { free(b); die("table.bin magic", dir); return -1; }
    int ver = rd32(b + 4), ids = rd32(b + 8), metas = rd32(b + 12), sides = rd32(b + 16);
    int nicon = rd32(b + 20), nspecial = rd32(b + 24);
    o = 28;
    if (ver != 1 || ids != RB_IDS || metas != RB_METAS || sides != RB_SIDES || nspecial != RB_SPECIALS)
    {
        free(b);
        die("table.bin is not this build's layout", dir);
        return -1;
    }

    t->n_icons = nicon;
    t->icon_name = calloc((size_t)nicon, sizeof *t->icon_name);
    for (int i = 0; i < nicon; ++i)
    {
        int n = rd16(b + o);
        o += 2;
        t->icon_name[i] = malloc((size_t)n + 1);
        memcpy(t->icon_name[i], b + o, (size_t)n);
        t->icon_name[i][n] = 0;
        o += (size_t)n;
    }

    t->n_special = nspecial;
    t->special = calloc((size_t)nspecial, sizeof *t->special);
    for (int i = 0; i < nspecial; ++i)
    {
        int n = rd16(b + o);
        o += 2;
        t->special[i] = malloc((size_t)n + 1);
        memcpy(t->special[i], b + o, (size_t)n);
        t->special[i][n] = 0;
        o += (size_t)n;
    }

    t->icon_index = malloc((size_t)ids * metas * sides * 2);
    memcpy(t->icon_index, b + o, (size_t)ids * metas * sides * 2);
    o += (size_t)ids * metas * sides * 2;

    t->grass_map = malloc(65536 * 4);
    memcpy(t->grass_map, b + o, 65536 * 4);
    o += 65536 * 4;
    t->foliage_map = malloc(65536 * 4);
    memcpy(t->foliage_map, b + o, 65536 * 4);
    o += 65536 * 4;

    t->props = malloc((size_t)ids * sizeof *t->props);
    for (int i = 0; i < ids; ++i)
    {
        memcpy(&t->props[i].can_block_grass, b + o, 8);
        t->props[i].ao_light = ffrom((uint32_t)rd32(b + o + 8));
        o += 12;
    }

    double *bounds = malloc((size_t)ids * metas * 6 * sizeof *bounds);
    for (int i = 0; i < ids * metas * 6; ++i)
    {
        uint64_t v = rd64(b + o);
        memcpy(&bounds[i], &v, 8);
        o += 8;
    }
    t->bounds = bounds;

    t->biomes = malloc(RB_BIOMES * sizeof *t->biomes);
    for (int i = 0; i < RB_BIOMES; ++i)
    {
        struct rb_biome *bm = &t->biomes[i];
        bm->temperature = ffrom((uint32_t)rd32(b + o));
        bm->rainfall = ffrom((uint32_t)rd32(b + o + 4));
        bm->water = rd32(b + o + 8);
        bm->grass_const = rd32(b + o + 12);
        bm->foliage_const = rd32(b + o + 16);
        bm->grass_kind = b[o + 20];
        bm->foliage_kind = b[o + 21];
        memcpy(&bm->base, b + o + 22, 2);
        o += 32;
    }

    t->sin_table = malloc(65536 * sizeof *t->sin_table);
    for (int i = 0; i < 65536; ++i) t->sin_table[i] = ffrom((uint32_t)rd32(b + o + 4 * i));
    o += 65536 * 4;

    if (o != (size_t)len) { free(b); die("table.bin has trailing bytes", dir); return -1; }
    free(b);
    return 0;
}

void rb_table_free(struct rb_table *t)
{
    if (!t->icon_name) return;
    for (int i = 0; i < t->n_icons; ++i) free(t->icon_name[i]);
    for (int i = 0; i < t->n_special; ++i) free(t->special[i]);
    free(t->icon_name);
    free(t->special);
    free(t->icon_index);
    free(t->grass_map);
    free(t->foliage_map);
    free(t->props);
    free((void *)t->bounds);
    free(t->biomes);
    free(t->sin_table);
    memset(t, 0, sizeof *t);
}

/* ------------------------------------------------------------- atlas.json */

/** atlas.json and manifest.json are compact Gson output of a fixed shape; the
 * value starts right after the key's colon. */
static const char *json_find(const char *json, const char *key, const char *from)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(from, pat);
    return p ? p + strlen(pat) : NULL;
}

int rb_atlas_load(struct rb_atlas *a, const char *dir)
{
    memset(a, 0, sizeof *a);
    char path[1200];
    snprintf(path, sizeof path, "%s/atlas.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = malloc((size_t)len + 1);
    if (!json || fread(json, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(json); die("short atlas.json", dir); return -1; }
    json[len] = 0;
    fclose(f);

    const char *p = json_find(json, "sprites", json);
    if (!p) { free(json); die("atlas.json has no sprites", dir); return -1; }
    a->n = 0;
    a->name = NULL;
    a->uv = NULL;
    int cap = 0;

    for (;;)
    {
        const char *e = strstr(p, "{\"name\":\"");
        if (!e) break;
        e += 9;
        const char *q = strchr(e, '"');
        if (!q) break;
        int n = (int)(q - e);

        if (a->n == cap)
        {
            cap = cap ? cap * 2 : 64;
            a->name = realloc(a->name, (size_t)cap * sizeof *a->name);
            a->uv = realloc(a->uv, (size_t)cap * sizeof *a->uv);
        }

        a->name[a->n] = malloc((size_t)n + 1);
        memcpy(a->name[a->n], e, (size_t)n);
        a->name[a->n][n] = 0;

        double u[4] = {0, 0, 0, 0};
        static const char *keys[4] = {"minU", "maxU", "minV", "maxV"};
        for (int k = 0; k < 4; ++k)
        {
            const char *kp = json_find(json, keys[k], q);
            if (!kp) { free(json); die("atlas.json sprite has no uv", dir); return -1; }
            u[k] = (double)strtol(kp, NULL, 10);
        }
        a->uv[a->n].min_u = ffrom((uint32_t)(int64_t)u[0]);
        a->uv[a->n].max_u = ffrom((uint32_t)(int64_t)u[1]);
        a->uv[a->n].min_v = ffrom((uint32_t)(int64_t)u[2]);
        a->uv[a->n].max_v = ffrom((uint32_t)(int64_t)u[3]);
        ++a->n;
        p = q;
    }

    free(json);
    if (a->n == 0) { die("atlas.json has no sprites", dir); return -1; }
    return 0;
}

void rb_atlas_free(struct rb_atlas *a)
{
    for (int i = 0; i < a->n; ++i) free(a->name[i]);
    free(a->name);
    free(a->uv);
    memset(a, 0, sizeof *a);
}

/* ------------------------------------------------------------- chunks.bin */

int rb_world_load(struct rb_world *w, const char *dir)
{
    memset(w, 0, sizeof *w);
    char path[1200];
    snprintf(path, sizeof path, "%s/chunks.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    w->stride = RB_CHUNK_RECORD;
    long chunks = len / (long)w->stride;
    if (chunks <= 0 || (long)chunks * (long)w->stride != len)
    {
        fclose(f);
        die("chunks.bin is not a whole number of chunks:", dir);
        return -1;
    }
    w->data = malloc((size_t)len);
    if (!w->data || fread(w->data, 1, (size_t)len, f) != (size_t)len) { fclose(f); die("short chunks.bin", dir); return -1; }
    fclose(f);
    for (w->rows = 1; w->rows * w->rows < chunks; w->rows += 2) {}
    if (w->rows * w->rows != chunks || w->rows % 2 != 1)
    {
        die("chunks.bin is not a square grid", dir);
        rb_world_free(w);
        return -1;
    }
    w->margin = w->rows / 2;

    /* the scene's tile entities, if the probe recorded any (a flower pot's
     * contents); every other field of the format is spare for now */
    snprintf(path, sizeof path, "%s/tileentities.bin", dir);
    FILE *tf = fopen(path, "rb");

    if (tf)
    {
        fseek(tf, 0, SEEK_END);
        long tlen = ftell(tf);
        fseek(tf, 0, SEEK_SET);
        int32_t *teb = malloc((size_t)tlen);

        if (!teb || fread(teb, 1, (size_t)tlen, tf) != (size_t)tlen) { fclose(tf); free(teb); die("short tileentities.bin", dir); return -1; }
        fclose(tf);

        if (tlen < 4 || tlen != 4 + 20LL * (long)teb[0])
        {
            free(teb);
            die("tileentities.bin is not whole records", dir);
            return -1;
        }

        w->te_n = teb[0];
        w->te = malloc((size_t)w->te_n * sizeof *w->te);

        for (int i = 0; i < w->te_n; ++i)
        {
            w->te[i].x = teb[1 + i * 5 + 0];
            w->te[i].y = teb[1 + i * 5 + 1];
            w->te[i].z = teb[1 + i * 5 + 2];
            w->te[i].block = teb[1 + i * 5 + 3];
            w->te[i].data = teb[1 + i * 5 + 4];
        }

        free(teb);
    }

    return 0;
}

void rb_world_free(struct rb_world *w)
{
    free(w->data);
    free(w->te);
    memset(w, 0, sizeof *w);
}

int rb_manifest_load(const char *dir, int *radius, int *cx, int *cz, int *px, int *py, int *pz)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    static char json[1 << 18];
    size_t n = fread(json, 1, sizeof json - 1, f);
    json[n] = 0;
    fclose(f);

    const char *r = json_find(json, "radius_chunks", json);
    const char *pc = json_find(json, "player_chunk", json);
    const char *pb = json_find(json, "player_block", json);
    const char *gr = json_find(json, "graphics", json);
    if (!r || !pc || !pb || !gr) { die("manifest.json lacks a field", dir); return -1; }
    *radius = (int)strtol(r, NULL, 10);
    *cx = (int)strtol(pc + 1, NULL, 10);
    *cz = (int)strtol(strchr(pc + 1, ',') + 1, NULL, 10);
    *px = (int)strtol(pb + 1, NULL, 10);
    *py = (int)strtol(strchr(pb + 1, ',') + 1, NULL, 10);
    *pz = (int)strtol(strchr(strchr(pb + 1, ',') + 1, ',') + 1, NULL, 10);

    if (!strstr(gr, "fancy:0") && !strstr(gr, "fancy:1")) { die("unknown fancy graphics option:", dir); return -1; }
    if (!strstr(gr, "ao:0") && !strstr(gr, "ao:2")) { die("unknown ambient occlusion option:", dir); return -1; }
    return 0;
}

/* --------------------------------------------------------- world accessors */

static const uint8_t *chunk_at(const struct rb_world *w, int cx, int cz)
{
    int dx = cx - (w->origin_cx - w->margin);
    int dz = cz - (w->origin_cz - w->margin);
    if (dx < 0 || dx >= w->rows || dz < 0 || dz >= w->rows) return NULL;
    return w->data + ((size_t)dx * (size_t)w->rows + (size_t)dz) * w->stride + 8;
}

/* the chunk-window form: *in says whether (cx, cz) is inside the window; an
 * inside slot with no chunk reads as a record of zeros */
static const struct chunk *chunk_in(const struct rb_world *w, int cx, int cz, int *in)
{
    int dx = cx - (w->origin_cx - w->margin);
    int dz = cz - (w->origin_cz - w->margin);
    *in = !(dx < 0 || dx >= w->rows || dz < 0 || dz >= w->rows);
    return *in ? w->chunks[(size_t)dx * (size_t)w->rows + (size_t)dz] : NULL;
}

#define CELL_OF(x, y, z) (((x) & 15) << 12 | ((z) & 15) << 8 | (y))

#if defined(__NVPTX__)
/* the device mesher's band of (x, y, z) (y in 0..255), NULL outside its
 * table: one lookup where chunk_in and chunk_sec_at take three dependent
 * loads; the answers are the same */
static const struct rb_band *band_of(const struct rb_world *w, int x, int y, int z)
{
    unsigned dx = (unsigned)(x - w->band_x0), dz = (unsigned)(z - w->band_z0), ds = (unsigned)(y - w->band_y0);
    return w->bands && dx < 48 && dz < 48 && ds < 48 ? &w->bands[((dx >> 4) * 3 + (dz >> 4)) * 3 + (ds >> 4)] : NULL;
}
#endif

#if defined(__NVPTX__)
/* the table mode's cache entry of (x, y, z), NULL outside it */
static const uint32_t *cache_of(const struct rb_world *w, int x, int y, int z)
{
    unsigned dx = (unsigned)(x - w->cache_x0), dy = (unsigned)(y - w->cache_y0), dz = (unsigned)(z - w->cache_z0);
    return w->cache && dx < RB_CACHE && dy < RB_CACHE && dz < RB_CACHE ? &w->cache[(dx * RB_CACHE + dz) * RB_CACHE + dy] : NULL;
}
#endif

int rb_world_block(const struct rb_world *w, int x, int y, int z)
{
    if (y < 0 || y >= 256) return 0;
#if defined(__NVPTX__)
    const uint32_t *e = cache_of(w, x, y, z);
    if (e) return (int)(*e & 4095);
    const struct rb_band *b = band_of(w, x, y, z);
    if (b) return b->band ? chunk_sec_id(b->band, SEC_XYZ(x & 15, y, z & 15)) : 0;
#endif
    if (w->chunks)
    {
        int in;
        const struct chunk *k = chunk_in(w, x >> 4, z >> 4, &in);
        return k ? chunk_cell_id(k, CELL_OF(x, y, z)) : 0;
    }
    const uint8_t *c = chunk_at(w, x >> 4, z >> 4);
    if (!c) return 0;
    size_t i = (size_t)(((x & 15) << 12) | ((z & 15) << 8) | y);
    return c[2 * i] | (c[2 * i + 1] << 8);
}

int rb_world_meta(const struct rb_world *w, int x, int y, int z)
{
    if (y < 0 || y >= 256) return 0;
#if defined(__NVPTX__)
    const uint32_t *e = cache_of(w, x, y, z);
    if (e) return (int)(*e >> 12 & 15);
    const struct rb_band *b = band_of(w, x, y, z);
    if (b) return b->band ? nibble_get(chunk_sec_metas(b->band), SEC_XYZ(x & 15, y, z & 15)) : 0;
#endif
    if (w->chunks)
    {
        int in;
        const struct chunk *k = chunk_in(w, x >> 4, z >> 4, &in);
        return k ? chunk_cell_meta(k, CELL_OF(x, y, z)) : 0;
    }
    const uint8_t *c = chunk_at(w, x >> 4, z >> 4);
    if (!c) return 0;
    return c[RB_CHUNK_META + (((x & 15) << 12) | ((z & 15) << 8) | y)];
}

/** Chunk.getSavedLightValue: an absent section is sky-15 only below the height map. */
static int saved_light(const struct rb_world *w, int is_sky, int x, int y, int z)
{
    if (y < 0) return 0;
    if (y > 255) y = 255;
#if defined(__NVPTX__)
    const uint32_t *e = cache_of(w, x, y, z);
    if (e) return (int)(*e >> (is_sky ? 16 : 20) & 15);
    const struct rb_band *b = band_of(w, x, y, z);
    if (b && b->kind != 2)
    {
        if (b->kind == 0) return is_sky ? 15 : 0;
        return b->band ? nibble_get(chunk_sec_nib(b->band, is_sky ? NIB_SKY : NIB_BLOCK), SEC_XYZ(x & 15, y, z & 15)) : 0;
    }
#endif
    if (w->chunks)
    {
        int in;
        const struct chunk *k = chunk_in(w, x >> 4, z >> 4, &in);
        if (!in) return 0;
        /* a zero record: no section, a height map of 0 */
        if (!k) return is_sky ? 15 : 0;
        if (k->mask & (1 << (y >> 4)))
            return is_sky ? chunk_cell_sky(k, CELL_OF(x, y, z)) : chunk_cell_blocklight(k, CELL_OF(x, y, z));
        if (!is_sky) return 0;
        return y >= k->height[((z & 15) << 4) | (x & 15)] ? 15 : 0;
    }
    const uint8_t *c = chunk_at(w, x >> 4, z >> 4);
    if (!c) return 0;
    size_t i = (size_t)(((x & 15) << 12) | ((z & 15) << 8) | y);
    int mask = rd16(c + RB_CHUNK_BYTES - 2);
    if (mask & (1 << (y >> 4)))
    {
        return c[(is_sky ? RB_CHUNK_SKY : RB_CHUNK_BLOCK) + i];
    }
    if (!is_sky) return 0;
    int hm = rd32(c + RB_CHUNK_HEIGHT + (((z & 15) << 4) | (x & 15)) * 4);
    return y >= hm ? 15 : 0;
}

int rb_world_sky(const struct rb_world *w, int x, int y, int z) { return saved_light(w, 1, x, y, z); }
int rb_world_blocklight(const struct rb_world *w, int x, int y, int z) { return saved_light(w, 0, x, y, z); }

int rb_world_biome(const struct rb_world *w, int x, int z)
{
#if defined(__NVPTX__)
    unsigned dx = (unsigned)(x - w->cache_x0), dz = (unsigned)(z - w->cache_z0);
    if (w->bcache && dx < RB_CACHE && dz < RB_CACHE) return w->bcache[dx * RB_CACHE + dz];
#endif
    if (w->chunks)
    {
        int in;
        const struct chunk *k = chunk_in(w, x >> 4, z >> 4, &in);
        return k ? k->biome[((z & 15) << 4) | (x & 15)] : 0;
    }
    const uint8_t *c = chunk_at(w, x >> 4, z >> 4);
    if (!c) return 0;
    return c[RB_CHUNK_BIOMES + (((z & 15) << 4) | (x & 15))];
}

/* World.getPrecipitationHeight over Chunk.getPrecipitationHeight: the chunk's
 * cached column, or the height the client computes and caches itself, from the
 * top filled section down to the first block that blocks movement or is a
 * liquid. An unloaded chunk is -1. */
int rb_world_precipitation_height(const struct rb_world *w, int x, int z)
{
    int col = ((z & 15) << 4) | (x & 15);
    const struct chunk *k = NULL;
    const uint8_t *c = NULL;
    int32_t cached;
    int mask;
    if (w->chunks)
    {
        int in;
        k = chunk_in(w, x >> 4, z >> 4, &in);
        if (!in) return -1;
        if (!k) return 0;    /* a zero record's cached height */
        cached = k->precip[col];
        mask = k->mask;
    }
    else
    {
        c = chunk_at(w, x >> 4, z >> 4);
        if (!c) return -1;
        memcpy(&cached, c + RB_CHUNK_HEIGHT + 1024 + col * 4, 4);
        mask = rd16(c + RB_CHUNK_BYTES - 2);
    }
    if (cached != -999) return cached;

    int s = 15;
    while (s >= 0 && !(mask & (1 << s))) --s;
    int y = (s < 0 ? 0 : s * 16) + 15;
    int found = -1;

    while (y > 0 && found == -1)
    {
        size_t i = (size_t)(((x & 15) << 12) | ((z & 15) << 8) | y);
        int id = k ? chunk_cell_id(k, CELL_OF(x, y, z)) : c[2 * i] | (c[2 * i + 1] << 8);
        const struct material_def *m = &MATERIALS[BLOCKS[id].material];

        if (!m->blocks_movement && !m->is_liquid) --y;
        else found = y + 1;
    }

    return found;
}

int rb_world_tile_entity(const struct rb_world *w, int x, int y, int z, int *block, int *data)
{
    if (w->live != NULL)
    {
        /* TileEntityFlowerPot's item (an ItemBlock: its id is the block's)
         * and data */
        const struct tile_entity *te = world_tile_entity(w->live, x, y, z);
        if (te == NULL || te->invalid || te->kind != TE_FLOWER_POT || te->u.pot.item <= 0) return 0;
        *block = te->u.pot.item;
        *data = te->u.pot.data;
        return 1;
    }
    for (int i = 0; i < w->te_n; ++i)
    {
        if (w->te[i].x == x && w->te[i].y == y && w->te[i].z == z)
        {
            *block = w->te[i].block;
            *data = w->te[i].data;
            return 1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------ tessellator */

int rb_tess_init(struct rb_tess *t, int cap)
{
    memset(t, 0, sizeof *t);
    t->cap = cap;
    t->raw = malloc((size_t)cap * sizeof *t->raw);
    t->dbg = malloc((size_t)cap * 4 * sizeof *t->dbg);
    return t->raw && t->dbg ? 0 : -1;
}

void rb_tess_free(struct rb_tess *t)
{
    free(t->raw);
    free(t->dbg);
    memset(t, 0, sizeof *t);
}

void rb_tess_start_quads(struct rb_tess *t)
{
    t->n = 0;
    t->vertex_count = 0;
    t->has_texture = t->has_color = t->has_brightness = t->has_normals = 0;
}

void rb_tess_set_translation(struct rb_tess *t, double x, double y, double z)
{
    t->xoff = x;
    t->yoff = y;
    t->zoff = z;
}

static void tess_set_texture_uv(struct rb_tess *t, double u, double v)
{
    t->has_texture = 1;
    t->u = u;
    t->v = v;
}

void tess_set_brightness(struct rb_tess *t, int32_t b)
{
    t->has_brightness = 1;
    t->brightness = b;
}

/** Tessellator.setColorRGBA: clamp, then pack little endian as the oracle does. */
static void tess_set_color_rgba(struct rb_tess *t, int32_t r, int32_t g, int32_t b, int32_t a)
{
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (a > 255) a = 255;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (a < 0) a = 0;
    t->has_color = 1;
    t->color = (int32_t)(((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r);
}

void tess_set_color_opaque(struct rb_tess *t, int32_t r, int32_t g, int32_t b)
{
    tess_set_color_rgba(t, r, g, b, 255);
}

void tess_set_color_opaque_f(struct rb_tess *t, float r, float g, float b)
{
    tess_set_color_opaque(t, (int32_t)(r * 255.0F), (int32_t)(g * 255.0F), (int32_t)(b * 255.0F));
}

void tess_add_vertex(struct rb_tess *t, double x, double y, double z)
{
    /* no buffer: the vertices are only counted (the device mesher's first
     * pass sizes each section's block of its mesh arena this way) */
    if (t->raw == NULL) { t->n += 8; ++t->vertex_count; return; }
    if (t->n + 8 > t->cap) { t->dropped = 1; return; }
    int32_t *v = t->raw + t->n;
    if (t->has_texture)
    {
        v[3] = fbits((float)t->u);
        v[4] = fbits((float)t->v);
    }
    if (t->has_brightness) v[7] = t->brightness;
    if (t->has_color) v[5] = t->color;
    if (t->has_normals) v[6] = t->normal;
    v[0] = fbits((float)(x + t->xoff));
    v[1] = fbits((float)(y + t->yoff));
    v[2] = fbits((float)(z + t->zoff));
    if (t->dbg)
    {
        int32_t *d = t->dbg + ((size_t)t->n / 8) * 4;
        d[0] = t->cur_id;
        d[1] = t->cur_meta;
        d[2] = t->cur_rt;
        d[3] = ((t->cur_x + 2048) << 20) | ((t->cur_y & 255) << 12) | ((t->cur_z + 2048) & 4095);
    }
    t->n += 8;
    ++t->vertex_count;
}

void tess_add_vertex_with_uv(struct rb_tess *t, double x, double y, double z, double u, double v)
{
    tess_set_texture_uv(t, u, v);
    tess_add_vertex(t, x, y, z);
}

/* ------------------------------------------------------------------ icons */

const char *icon_name(const struct rb_table *tab, int idx)
{
    return idx == RB_NO_ICON ? "" : tab->icon_name[idx];
}

/** The icon index this (id, meta, side) resolves to, for the grass_side test. */
int table_icon_index(const struct rb_mesher *m, int id, int meta, int side)
{
    return m->tab->icon_index[(id * RB_METAS + (meta & 15)) * RB_SIDES + side];
}

int icon_is(const struct rb_mesher *m, int idx, const char *name)
{
    const char *n = icon_name(m->tab, idx);
    return n[0] != 0 && strcmp(n, name) == 0;
}


const struct rb_uv *table_icon(const struct rb_mesher *m, int id, int meta, int side)
{
    int idx = m->tab->icon_index[(id * RB_METAS + (meta & 15)) * RB_SIDES + side];

    if (idx == RB_NO_ICON)
    {
        /* RenderBlocks.getIconSafe: a null icon is the atlas's missingno */
        if (m->has_missingno) return &m->missingno;
        fprintf(stderr, "render_blocks: block %d meta %d side %d has no icon\n", id, meta, side);
        exit(2);
    }

    return &m->icon[idx];
}

/** RenderBlocks.getBlockIconFromSideAndMetadata, no override texture. */
const struct rb_uv *icon_from_side_meta(const struct rb_mesher *m, int id, int side, int meta)
{
    return table_icon(m, id, meta, side);
}

const struct rb_uv *icon_from_side(const struct rb_mesher *m, int id, int side)
{
    return table_icon(m, id, 0, side);
}

/**
 * RenderBlocks.getBlockIcon(Block, IBlockAccess, x, y, z, side): Block.getIcon
 * overridden where the world decides, which for these blocks is grass. The
 * result is an icon index, negative for one of the fixed special icons.
 */
static int block_icon_world_index(struct rb_mesher *m, int id, int x, int y, int z, int side)
{
    int meta = rb_world_meta(m->w, x, y, z);

    if (id == 2 && strcmp(BLOCKS[2].class_name, "BlockGrass") == 0)
    {
        if (side == 1) return table_icon_index(m, id, meta, 1);
        if (side == 0) return table_icon_index(m, 3, 0, 0);   /* Blocks.dirt.getBlockTextureFromSide(0) */
        int above = rb_world_block(m->w, x, y + 1, z);
        int mat = BLOCKS[above].material;
        if (mat == m->mat_snow || mat == m->mat_crafted_snow) return -1 - RB_ICON_GRASS_SNOWED;
        return table_icon_index(m, id, meta, side);
    }

    /* the blocks whose getIcon(side, meta) reads mutable state (the anvil's
     * current part, the piston base's bounds) or is not what table.bin was
     * able to record; render_blocks3.c decides */
    int rb3 = rb3_block_icon_index(m, id, x, y, z, side);

    if (rb3 != RB3_NOT_MINE) return rb3;

    return table_icon_index(m, id, meta, side);
}

static const struct rb_uv *uv_of(const struct rb_mesher *m, int code)
{
    if (code >= 0) return &m->icon[code];

    int idx = -1 - code;

    if (idx < RB_SPECIALS) return &m->special[idx];
    idx -= RB_SPECIALS;

    if (idx < RB2_SPECIALS) return &m->special2[idx];
    return &m->special3[idx - RB2_SPECIALS];
}

const struct rb_uv *block_icon_world(struct rb_mesher *m, int id, int x, int y, int z, int side)
{
    return uv_of(m, block_icon_world_index(m, id, x, y, z, side));
}

/** The icon name test the fancy grass overlay makes (getIconName().equals). */
static int icon_code_is(struct rb_mesher *m, int code, const char *name)
{
    if (!m->tab->fancy) return 0; /* RenderBlocks.fancyGrass follows fancyGraphics */
    /* the render_blocks2 and render_blocks3 icon codes are never grass_side,
     * the only name this test asks for */
    if (code < 0 && -1 - code >= RB_SPECIALS) return 0;

    if (code < 0) return strcmp(m->tab->special[-1 - code], name) == 0;

    return icon_is(m, code, name);
}

/* -------------------------------------------------------------- light/ao */

/** ChunkCache.getSkyBlockTypeBrightness. */
static int sky_type_brightness(struct rb_mesher *m, int is_sky, int x, int y, int z)
{
    if (is_sky && m->w->no_sky) return 0;
    if (y < 0) y = 0;
    if (y >= 256) y = 255;
    int id = rb_world_block(m->w, x, y, z);

    if (m->tab->props[id].neighbor)
    {
        int a = saved_light(m->w, is_sky, x, y + 1, z);
        int b = saved_light(m->w, is_sky, x + 1, y, z);
        int c = saved_light(m->w, is_sky, x - 1, y, z);
        int d = saved_light(m->w, is_sky, x, y, z + 1);
        int e = saved_light(m->w, is_sky, x, y, z - 1);
        int v = a;
        if (b > v) v = b;
        if (c > v) v = c;
        if (d > v) v = d;
        if (e > v) v = e;
        return v;
    }

    return saved_light(m->w, is_sky, x, y, z);
}

/** ChunkCache.getLightBrightnessForSkyBlocks. */
static int light_brightness(struct rb_mesher *m, int x, int y, int z, int light_value)
{
    int sky = sky_type_brightness(m, 1, x, y, z);
    int blk = sky_type_brightness(m, 0, x, y, z);
    if (blk < light_value) blk = light_value;
    return (sky << 20) | (blk << 4);
}

/** BlockLiquid.getBlockBrightness: the max of this block and the one above. */
static int liquid_brightness(struct rb_mesher *m, int x, int y, int z)
{
    int a = light_brightness(m, x, y, z, 0);
    int b = light_brightness(m, x, y + 1, z, 0);
    int ab = a & 255, bb = b & 255;
    int as = (a >> 16) & 255, bs = (b >> 16) & 255;
    return (ab > bb ? ab : bb) | (as > bs ? as : bs) << 16;
}

/** Block.getBlockBrightness(blockAccess, x, y, z) for the block being rendered. */
int block_brightness(struct rb_mesher *m, int sender, int x, int y, int z)
{
    if (BLOCKS[sender].render_type == 4) return liquid_brightness(m, x, y, z);
    int id = rb_world_block(m->w, x, y, z);
    int r = light_brightness(m, x, y, z, m->tab->props[id].light);

    if (r == 0 && m->tab->props[id].is_slab)
    {
        int below = rb_world_block(m->w, x, y - 1, z);
        return light_brightness(m, x, y - 1, z, m->tab->props[below].light);
    }

    return r;
}

float ao_light_of(struct rb_mesher *m, int x, int y, int z)
{
    return m->tab->props[rb_world_block(m->w, x, y, z)].ao_light;
}

int can_block_grass(struct rb_mesher *m, int x, int y, int z)
{
    return m->tab->props[rb_world_block(m->w, x, y, z)].can_block_grass;
}

int opaque_cube(struct rb_mesher *m, int x, int y, int z)
{
    return m->tab->props[rb_world_block(m->w, x, y, z)].opaque_cube;
}

/** RenderBlocks.getAoBrightness. */
int32_t get_ao_brightness(int32_t a, int32_t b, int32_t c, int32_t d)
{
    if (a == 0) a = d;
    if (b == 0) b = d;
    if (c == 0) c = d;
    return ((a + b + c + d) >> 2) & 16711935;
}

/** RenderBlocks.mixAoBrightness: the partial-bounds blend, in the sky<<16|block form. */
int32_t mix_ao_brightness(int32_t p1, int32_t p2, int32_t p3, int32_t p4,
                                 double w1, double w2, double w3, double w4)
{
    int32_t g = (int32_t)((double)((p1 >> 16) & 255) * w1 + (double)((p2 >> 16) & 255) * w2
        + (double)((p3 >> 16) & 255) * w3 + (double)((p4 >> 16) & 255) * w4) & 255;
    int32_t b = (int32_t)((double)(p1 & 255) * w1 + (double)(p2 & 255) * w2
        + (double)(p3 & 255) * w3 + (double)(p4 & 255) * w4) & 255;
    return (g << 16) | b;
}

/* ---------------------------------------------------------------- colours */

static float clamp01(float f)
{
    return f < 0.0F ? 0.0F : (f > 1.0F ? 1.0F : f);
}

/** ColorizerGrass.getGrassColor / ColorizerFoliage.getFoliageColor. */
static int32_t colormap_lookup(const uint32_t *map, double temperature, double humidity)
{
    double h = humidity * temperature;
    int a = (int)((1.0 - temperature) * 255.0);
    int b = (int)((1.0 - h) * 255.0);
    return (int32_t)map[(b << 8) | a];
}

#if defined(__NVPTX__)
double rb_column_noise(const struct rb_mesher *m, int which, int x, int z)
{
    const struct rb_world *w = m->w;
    unsigned dx = (unsigned)(x - w->cache_x0), dz = (unsigned)(z - w->cache_z0);
    if (w->ncache && dx < RB_CACHE && dz < RB_CACHE) return w->ncache[(which * RB_CACHE + dx) * RB_CACHE + dz];
    return which ? perlin_point(m->perlin_b, (double)x * 0.0225, (double)z * 0.0225)
                 : perlin_point(m->perlin_a, (double)x * 1.0 / 8.0, (double)z * 1.0 / 8.0);
}
#endif

/** BiomeGenBase.getFloatTemperature. */
static float biome_temperature(struct rb_mesher *m, int slot, int x, int y, int z)
{
    const struct rb_biome *b = &m->tab->biomes[slot & 255];

    if (y > 64)
    {
#if defined(__NVPTX__)
        float n = (float)rb_column_noise(m, 0, x, z) * 4.0F;
#else
        float n = (float)perlin_point(m->perlin_a, (double)x * 1.0 / 8.0, (double)z * 1.0 / 8.0) * 4.0F;
#endif
        return b->temperature - (n + (float)y - 64.0F) * 0.05F / 30.0F;
    }

    return b->temperature;
}

float rb_biome_temperature(struct rb_mesher *m, int x, int y, int z)
{
    return biome_temperature(m, rb_world_biome(m->w, x, z), x, y, z);
}

/** The biome colour of one row at one point, by the row's kind. */
static int32_t biome_color(struct rb_mesher *m, int slot, int grass, int x, int y, int z)
{
    const struct rb_biome *b = &m->tab->biomes[slot & 255];
    int kind = grass ? b->grass_kind : b->foliage_kind;

    if (kind == RB_KIND_MUTATED) return biome_color(m, b->base, grass, x, y, y);
    if (kind == RB_KIND_CONST) return grass ? b->grass_const : b->foliage_const;

    if (kind == RB_KIND_SWAMP)
    {
#if defined(__NVPTX__)
        double d = rb_column_noise(m, 1, x, z);
#else
        double d = perlin_point(m->perlin_b, (double)x * 0.0225, (double)z * 0.0225);
#endif
        return d < -0.1 ? 5011004 : 6975545;
    }

    float t = clamp01(biome_temperature(m, slot, x, y, z));
    float h = clamp01(b->rainfall);

    if (grass)
    {
        int32_t c = colormap_lookup(m->tab->grass_map, (double)t, (double)h);
        return kind == RB_KIND_ROOFED ? (((c & 16711422) + 2634760 + 2) >> 1) : c;
    }

    return colormap_lookup(m->tab->foliage_map, (double)t, (double)h);
}

/** BlockGrass.colorMultiplier: the 3x3 average of the biome grass colours. */
static int32_t grass_color_multiplier(struct rb_mesher *m, int x, int y, int z)
{
    int a = 0, b = 0, c = 0;

    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int32_t col = biome_color(m, rb_world_biome(m->w, x + dx, z + dz), 1, x + dx, y, z + dz);
            a += (col & 16711680) >> 16;
            b += (col & 65280) >> 8;
            c += col & 255;
        }
    }

    return ((a / 9) & 255) << 16 | ((b / 9) & 255) << 8 | ((c / 9) & 255);
}

/** BlockLeaves.colorMultiplier: the same average over the foliage colours. */
static int32_t foliage_color_multiplier(struct rb_mesher *m, int x, int y, int z)
{
    int a = 0, b = 0, c = 0;

    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int32_t col = biome_color(m, rb_world_biome(m->w, x + dx, z + dz), 0, x + dx, y, z + dz);
            a += (col & 16711680) >> 16;
            b += (col & 65280) >> 8;
            c += col & 255;
        }
    }

    return ((a / 9) & 255) << 16 | ((b / 9) & 255) << 8 | ((c / 9) & 255);
}

/** The wrapper the live particle colour reads: BlockLeaves.colorMultiplier. */
int32_t rb_foliage_color_multiplier(struct rb_mesher *m, int x, int y, int z)
{
    return foliage_color_multiplier(m, x, y, z);
}

/** BlockLiquid.colorMultiplier: water takes the 3x3 average of the biome water colours. */
static int32_t water_color_multiplier(struct rb_mesher *m, int x, int y, int z)
{
    int a = 0, b = 0, c = 0;

    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int32_t col = m->tab->biomes[rb_world_biome(m->w, x + dx, z + dz) & 255].water;
            a += (col & 16711680) >> 16;
            b += (col & 65280) >> 8;
            c += col & 255;
        }
    }

    return ((a / 9) & 255) << 16 | ((b / 9) & 255) << 8 | ((c / 9) & 255);
}

/**
 * Block.colorMultiplier with the overrides the recorded blocks have: grass,
 * leaves (with the pine and birch constants BlockOldLeaf returns), tall grass
 * and reeds (the biome grass colour), double plants (variants 2 and 3), water
 * (the biome water colour), lava and everything else white.
 */
int32_t color_multiplier(struct rb_mesher *m, int id, int x, int y, int z)
{
    switch (id)
    {
        case 2:   return grass_color_multiplier(m, x, y, z);        /* BlockGrass */
        case 18:                                                    /* BlockOldLeaf */
        {
            int meta = rb_world_meta(m->w, x, y, z);
            if ((meta & 3) == 1) return 6396257;   /* ColorizerFoliage.getFoliageColorPine() */
            if ((meta & 3) == 2) return 8431445;   /* ColorizerFoliage.getFoliageColorBirch() */
            return foliage_color_multiplier(m, x, y, z);
        }
        case 161: return foliage_color_multiplier(m, x, y, z);      /* BlockNewLeaf */
        case 8:
        case 9:   return water_color_multiplier(m, x, y, z);        /* water */
        case 31:                                                    /* BlockTallGrass */
        {
            int meta = rb_world_meta(m->w, x, y, z);
            return meta == 0 ? 16777215 : biome_color(m, rb_world_biome(m->w, x, z), 1, x, y, z);
        }
        case 83:   return biome_color(m, rb_world_biome(m->w, x, z), 1, x, y, z);   /* BlockReed */
        case 106: return biome_color(m, rb_world_biome(m->w, x, z), 0, x, y, z);   /* BlockVine */
        case 104:                                                   /* BlockStem */
        case 105:
        {
            int meta = rb_world_meta(m->w, x, y, z);
            return meta * 32 << 16 | (255 - meta * 8) << 8 | meta * 4;   /* getRenderColor */
        }
        case 175:                                                   /* BlockDoublePlant */
        {
            int meta = rb_world_meta(m->w, x, y, z);
            int variant = (meta & 8) != 0 ? rb_world_meta(m->w, x, y - 1, z) & 7 : meta & 7;
            return variant != 2 && variant != 3 ? 16777215
                : biome_color(m, rb_world_biome(m->w, x, z), 1, x, y, z);
        }
        default:  return 16777215;
    }
}

/* ------------------------------------------------------------- block state */

void set_bounds(struct rb_mesher *m, int id, int meta)
{
    const double *b = m->tab->bounds + ((size_t)id * RB_METAS + (meta & 15)) * 6;
    m->min_x = b[0];
    m->min_y = b[1];
    m->min_z = b[2];
    m->max_x = b[3];
    m->max_y = b[4];
    m->max_z = b[5];
    /* gameSettings.ambientOcclusion is pinned at 2 */
    m->partial = m->min_x > 0.0 || m->max_x < 1.0 || m->min_y > 0.0 || m->max_y < 1.0
        || m->min_z > 0.0 || m->max_z < 1.0;
}

/**
 * Block.shouldSideBeRendered, the super path a slab falls back to, with this
 * slab's bounds in m as the caller left them.
 */
int slab_super_should_side(struct rb_mesher *m, int id, int x, int y, int z, int side)
{
    (void)id;
    if (side == 0 && m->min_y > 0.0) return 1;
    if (side == 1 && m->max_y < 1.0) return 1;
    if (side == 2 && m->min_z > 0.0) return 1;
    if (side == 3 && m->max_z < 1.0) return 1;
    if (side == 4 && m->min_x > 0.0) return 1;
    if (side == 5 && m->max_x < 1.0) return 1;
    return !opaque_cube(m, x, y, z);
}

/**
 * Block.shouldSideBeRendered and the overrides the recorded blocks have:
 * ice hides a face against its own block, glass and stained glass against
 * their own block of the same metadata (and always draw it against any block
 * of another metadata), liquids hide it against their own material, snow
 * always draws its top. Leaves draw every face with fancy
 * graphics (BlockLeavesBase.field_150121_P is true then). BlockSlab's override
 * (the single halves) is the branch above BlockSlab.shouldSideBeRendered.
 */
/* The block classes the mesh asks for by name, per block id, built once
 * from BLOCKS' class and material names: the name tests ran per cell and
 * per face (a third of a section's meshing) */
enum {
    SC_SLAB = 1,        /* BlockStoneSlab, BlockWoodSlab */
    SC_FENCE = 2,       /* BlockFence */
    SC_DIODE = 4,       /* BlockRedstoneDiode, BlockRedstoneRepeater, BlockRedstoneComparator */
    SC_WALL = 8,        /* BlockWall */
    SC_PANE = 16,       /* BlockPane, BlockStainedGlassPane */
    SC_PORTAL = 32,     /* BlockPortal */
    SC_AIR = 64,        /* Material.air */
    SC_ANVIL = RB_CLASS_ANVIL, SC_PISTON_BASE = RB_CLASS_PISTON_BASE,
};
static uint16_t block_class[4096];
static pthread_once_t block_class_once = PTHREAD_ONCE_INIT;

static void block_class_build(void)
{
    for (int id = 0; id < 4096; ++id)
    {
        const char *c = BLOCKS[id].class_name;
        int k = 0;
        if (c)
        {
            if (!strcmp(c, "BlockStoneSlab") || !strcmp(c, "BlockWoodSlab")) k |= SC_SLAB;
            if (!strcmp(c, "BlockFence")) k |= SC_FENCE;
            if (!strcmp(c, "BlockRedstoneDiode") || !strcmp(c, "BlockRedstoneRepeater") ||
                !strcmp(c, "BlockRedstoneComparator"))
                k |= SC_DIODE;
            if (!strcmp(c, "BlockWall")) k |= SC_WALL;
            if (!strcmp(c, "BlockPane") || !strcmp(c, "BlockStainedGlassPane")) k |= SC_PANE;
            if (!strcmp(c, "BlockPortal")) k |= SC_PORTAL;
            if (!strcmp(c, "BlockAnvil")) k |= SC_ANVIL;
            if (!strcmp(c, "BlockPistonBase")) k |= SC_PISTON_BASE;
        }
        if (!strcmp(MATERIALS[BLOCKS[id].material].name, "air")) k |= SC_AIR;
        block_class[id] = (uint16_t)k;
    }
}

static int block_class_of(int id)
{
    pthread_once(&block_class_once, block_class_build);
    return block_class[id & 4095];
}

int rb_block_class(int id)
{
    return block_class_of(id);
}

int should_side_be_rendered(struct rb_mesher *m, int id, int x, int y, int z, int side)
{
    /* (x, y, z) is the neighbouring block, as RenderBlocks passes it */
    int nid = rb_world_block(m->w, x, y, z);
    int nmeta = rb_world_meta(m->w, x, y, z);
    const int cls = block_class_of(id);

    /* BlockSlab.shouldSideBeRendered. field_150004_a is false for the single
     * halves (BlockStoneSlab 44, BlockWoodSlab 126), true for the double slabs
     * and they take the super path. */
    if ((cls & SC_SLAB) && (id == 44 || id == 126))
    {
        if (side != 1 && side != 0 && !slab_super_should_side(m, id, x, y, z, side)) return 0;

        static const int OPP[6] = {1, 0, 3, 2, 5, 4};
        static const int OX[6] = {0, 0, 0, 0, -1, 1};
        static const int OY[6] = {-1, 1, 0, 0, 0, 0};
        static const int OZ[6] = {0, 0, -1, 1, 0, 0};
        int mx = x + OX[OPP[side]], my = y + OY[OPP[side]], mz = z + OZ[OPP[side]];
        int var9 = (rb_world_meta(m->w, mx, my, mz) & 8) != 0;
        int slab_n = nid == 43 || nid == 44 || nid == 125 || nid == 126;
        int up_n = (nmeta & 8) != 0;

        if (var9)
        {
            if (side == 0) return 1;
            if (side == 1 && slab_super_should_side(m, id, x, y, z, side)) return 1;
            return !slab_n || !up_n;
        }
        else
        {
            if (side == 1) return 1;
            if (side == 0 && slab_super_should_side(m, id, x, y, z, side)) return 1;
            return !slab_n || up_n;
        }
    }

    /* BlockFence.shouldSideBeRendered: true for every face */
    if (cls & SC_FENCE) return 1;

    /* BlockRedstoneDiode.shouldSideBeRendered: nothing against the floor or
     * the ceiling */
    if (cls & SC_DIODE)
    {
        return side != 0 && side != 1;
    }

    /* BlockWall.shouldSideBeRendered: the bottom face takes the super path */
    if ((cls & SC_WALL) && side != 0) return 1;

    /* the always-true overrides of the mesh3 blocks */
    if (id == 107 || id == 122 || id == 145 || id == 154) return 1;

    /* BlockPane.shouldSideBeRendered: nothing against its own block */
    if (cls & SC_PANE)
    {
        if (nid == id) return 0;
    }

    /* BlockPortal.shouldSideBeRendered */
    if (cls & SC_PORTAL)
    {
        int var6 = 0;

        if (nid == id)
        {
            var6 = nmeta & 3;   /* BlockPortal.func_149999_b */

            if (var6 == 0) return 0;
            if (var6 == 2 && side != 5 && side != 4) return 0;
            if (var6 == 1 && side != 3 && side != 2) return 0;
        }

        int var7 = rb_world_block(m->w, x - 1, y, z) == id && rb_world_block(m->w, x - 2, y, z) != id;
        int var8 = rb_world_block(m->w, x + 1, y, z) == id && rb_world_block(m->w, x + 2, y, z) != id;
        int var9 = rb_world_block(m->w, x, y, z - 1) == id && rb_world_block(m->w, x, y, z - 2) != id;
        int var10 = rb_world_block(m->w, x, y, z + 1) == id && rb_world_block(m->w, x, y, z + 2) != id;
        int var11 = var7 || var8 || var6 == 1;
        int var12 = var9 || var10 || var6 == 2;

        if (var11 && side == 4) return 1;
        if (var11 && side == 5) return 1;
        if (var12 && side == 2) return 1;
        return var12 && side == 3;
    }

    switch (id)
    {
        case 78:                                        /* BlockSnow */
            if (side == 1) return 1;
            break;
        case 20:                                        /* BlockGlass: BlockBreakable, the glass branch */
        case 95:                                        /* BlockStainedGlass */
        {
            static const int OX[6] = {0, 0, 0, 0, -1, 1};
            static const int OY[6] = {-1, 1, 0, 0, 0, 0};
            static const int OZ[6] = {0, 0, -1, 1, 0, 0};
            /* a neighbour of another metadata (any block) always shows the
             * face; otherwise the same block hides it */
            if (nmeta != rb_world_meta(m->w, x - OX[side], y - OY[side], z - OZ[side])) return 1;
            if (nid == id) return 0;
            break;
        }
        case 79:                                        /* BlockIce: BlockBreakable, field_149996_a false */
            if (nid == id) return 0;
            break;
        case 18:
        case 161:                                       /* BlockLeavesBase */
            if (!m->tab->fancy && nid == id) return 0;
            break;
        case 8:
        case 9:
        case 10:
        case 11:                                        /* BlockLiquid */
            if (BLOCKS[nid].material == BLOCKS[id].material) return 0;
            if (side == 1) return 1;
            break;
        default:
            break;
    }

    if (side == 0 && m->min_y > 0.0) return 1;
    if (side == 1 && m->max_y < 1.0) return 1;
    if (side == 2 && m->min_z > 0.0) return 1;
    if (side == 3 && m->max_z < 1.0) return 1;
    if (side == 4 && m->min_x > 0.0) return 1;
    if (side == 5 && m->max_x < 1.0) return 1;
    return !opaque_cube(m, x, y, z);
}

/* -------------------------------------------------------------- the faces */

float interp_u(const struct rb_uv *uv, double u)
{
    float d = uv->max_u - uv->min_u;
    return uv->min_u + d * (float)u / 16.0F;
}

float interp_v(const struct rb_uv *uv, double v)
{
    float d = uv->max_v - uv->min_v;
    return uv->min_v + d * ((float)v / 16.0F);
}

/** RenderBlocks.renderFaceYNeg */
void render_face_y_neg(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic)
{
    if (m->override) ic = m->override;   /* RenderBlocks.hasOverrideBlockTexture */

    struct rb_tess *t = m->t;
    double u0 = (double)interp_u(ic, m->min_x * 16.0);
    double u1 = (double)interp_u(ic, m->max_x * 16.0);
    double v0 = (double)interp_v(ic, m->min_z * 16.0);
    double v1 = (double)interp_v(ic, m->max_z * 16.0);

    if (m->min_x < 0.0 || m->max_x > 1.0) { u0 = (double)ic->min_u; u1 = (double)ic->max_u; }
    if (m->min_z < 0.0 || m->max_z > 1.0) { v0 = (double)ic->min_v; v1 = (double)ic->max_v; }

    double a = u1, b = u0, c = v0, d = v1;

    if (m->uv_bottom == 2)
    {
        u0 = (double)interp_u(ic, m->min_z * 16.0);
        v0 = (double)interp_v(ic, 16.0 - m->max_x * 16.0);
        u1 = (double)interp_u(ic, m->max_z * 16.0);
        v1 = (double)interp_v(ic, 16.0 - m->min_x * 16.0);
        c = v0; d = v1; a = u0; b = u1; v0 = v1; v1 = c;
    }
    else if (m->uv_bottom == 1)
    {
        u0 = (double)interp_u(ic, 16.0 - m->max_z * 16.0);
        v0 = (double)interp_v(ic, m->min_x * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->min_z * 16.0);
        v1 = (double)interp_v(ic, m->max_x * 16.0);
        a = u1; b = u0; u0 = u1; u1 = b; c = v1; d = v0;
    }
    else if (m->uv_bottom == 3)
    {
        u0 = (double)interp_u(ic, 16.0 - m->min_x * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->max_x * 16.0);
        v0 = (double)interp_v(ic, 16.0 - m->min_z * 16.0);
        v1 = (double)interp_v(ic, 16.0 - m->max_z * 16.0);
        a = u1; b = u0; c = v0; d = v1;
    }

    double x0 = x + m->min_x, x1 = x + m->max_x;
    double yy = y + m->min_y;
    double z0 = z + m->min_z, z1 = z + m->max_z;

    if (m->render_from_inside) { x0 = x + m->max_x; x1 = x + m->min_x; }

    if (m->enable_ao)
    {
        tess_set_color_opaque_f(t, m->r_tl, m->g_tl, m->b_tl);
        tess_set_brightness(t, m->bright_tl);
        tess_add_vertex_with_uv(t, x0, yy, z1, b, d);
        tess_set_color_opaque_f(t, m->r_bl, m->g_bl, m->b_bl);
        tess_set_brightness(t, m->bright_bl);
        tess_add_vertex_with_uv(t, x0, yy, z0, u0, v0);
        tess_set_color_opaque_f(t, m->r_br, m->g_br, m->b_br);
        tess_set_brightness(t, m->bright_br);
        tess_add_vertex_with_uv(t, x1, yy, z0, a, c);
        tess_set_color_opaque_f(t, m->r_tr, m->g_tr, m->b_tr);
        tess_set_brightness(t, m->bright_tr);
        tess_add_vertex_with_uv(t, x1, yy, z1, u1, v1);
    }
    else
    {
        tess_add_vertex_with_uv(t, x0, yy, z1, b, d);
        tess_add_vertex_with_uv(t, x0, yy, z0, u0, v0);
        tess_add_vertex_with_uv(t, x1, yy, z0, a, c);
        tess_add_vertex_with_uv(t, x1, yy, z1, u1, v1);
    }
}

/** RenderBlocks.renderFaceYPos */
void render_face_y_pos(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic)
{
    if (m->override) ic = m->override;   /* RenderBlocks.hasOverrideBlockTexture */

    struct rb_tess *t = m->t;
    double u0 = (double)interp_u(ic, m->min_x * 16.0);
    double u1 = (double)interp_u(ic, m->max_x * 16.0);
    double v0 = (double)interp_v(ic, m->min_z * 16.0);
    double v1 = (double)interp_v(ic, m->max_z * 16.0);

    if (m->min_x < 0.0 || m->max_x > 1.0) { u0 = (double)ic->min_u; u1 = (double)ic->max_u; }
    if (m->min_z < 0.0 || m->max_z > 1.0) { v0 = (double)ic->min_v; v1 = (double)ic->max_v; }

    double a = u1, b = u0, c = v0, d = v1;

    if (m->uv_top == 1)
    {
        u0 = (double)interp_u(ic, m->min_z * 16.0);
        v0 = (double)interp_v(ic, 16.0 - m->max_x * 16.0);
        u1 = (double)interp_u(ic, m->max_z * 16.0);
        v1 = (double)interp_v(ic, 16.0 - m->min_x * 16.0);
        c = v0; d = v1; a = u0; b = u1; v0 = v1; v1 = c;
    }
    else if (m->uv_top == 2)
    {
        u0 = (double)interp_u(ic, 16.0 - m->max_z * 16.0);
        v0 = (double)interp_v(ic, m->min_x * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->min_z * 16.0);
        v1 = (double)interp_v(ic, m->max_x * 16.0);
        a = u1; b = u0; u0 = u1; u1 = b; c = v1; d = v0;
    }
    else if (m->uv_top == 3)
    {
        u0 = (double)interp_u(ic, 16.0 - m->min_x * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->max_x * 16.0);
        v0 = (double)interp_v(ic, 16.0 - m->min_z * 16.0);
        v1 = (double)interp_v(ic, 16.0 - m->max_z * 16.0);
        a = u1; b = u0; c = v0; d = v1;
    }

    double x0 = x + m->min_x, x1 = x + m->max_x;
    double yy = y + m->max_y;
    double z0 = z + m->min_z, z1 = z + m->max_z;

    if (m->render_from_inside) { x0 = x + m->max_x; x1 = x + m->min_x; }

    if (m->enable_ao)
    {
        tess_set_color_opaque_f(t, m->r_tl, m->g_tl, m->b_tl);
        tess_set_brightness(t, m->bright_tl);
        tess_add_vertex_with_uv(t, x1, yy, z1, u1, v1);
        tess_set_color_opaque_f(t, m->r_bl, m->g_bl, m->b_bl);
        tess_set_brightness(t, m->bright_bl);
        tess_add_vertex_with_uv(t, x1, yy, z0, a, c);
        tess_set_color_opaque_f(t, m->r_br, m->g_br, m->b_br);
        tess_set_brightness(t, m->bright_br);
        tess_add_vertex_with_uv(t, x0, yy, z0, u0, v0);
        tess_set_color_opaque_f(t, m->r_tr, m->g_tr, m->b_tr);
        tess_set_brightness(t, m->bright_tr);
        tess_add_vertex_with_uv(t, x0, yy, z1, b, d);
    }
    else
    {
        tess_add_vertex_with_uv(t, x1, yy, z1, u1, v1);
        tess_add_vertex_with_uv(t, x1, yy, z0, a, c);
        tess_add_vertex_with_uv(t, x0, yy, z0, u0, v0);
        tess_add_vertex_with_uv(t, x0, yy, z1, b, d);
    }
}

/** RenderBlocks.renderFaceZNeg */
void render_face_z_neg(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic)
{
    if (m->override) ic = m->override;   /* RenderBlocks.hasOverrideBlockTexture */

    struct rb_tess *t = m->t;
    double u0 = (double)interp_u(ic, m->min_x * 16.0);
    double u1 = (double)interp_u(ic, m->max_x * 16.0);

    if (m->f152631f)
    {
        u1 = (double)interp_u(ic, (1.0 - m->min_x) * 16.0);
        u0 = (double)interp_u(ic, (1.0 - m->max_x) * 16.0);
    }

    double v0 = (double)interp_v(ic, 16.0 - m->max_y * 16.0);
    double v1 = (double)interp_v(ic, 16.0 - m->min_y * 16.0);

    if (m->flip_texture) { double s = u0; u0 = u1; u1 = s; }
    if (m->min_x < 0.0 || m->max_x > 1.0) { u0 = (double)ic->min_u; u1 = (double)ic->max_u; }
    if (m->min_y < 0.0 || m->max_y > 1.0) { v0 = (double)ic->min_v; v1 = (double)ic->max_v; }

    double a = u1, b = u0, c = v0, d = v1;

    if (m->uv_east == 2)
    {
        u0 = (double)interp_u(ic, m->min_y * 16.0);
        u1 = (double)interp_u(ic, m->max_y * 16.0);
        v0 = (double)interp_v(ic, 16.0 - m->min_x * 16.0);
        v1 = (double)interp_v(ic, 16.0 - m->max_x * 16.0);
        c = v0; d = v1; a = u0; b = u1; v0 = v1; v1 = c;
    }
    else if (m->uv_east == 1)
    {
        u0 = (double)interp_u(ic, 16.0 - m->max_y * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->min_y * 16.0);
        v0 = (double)interp_v(ic, m->max_x * 16.0);
        v1 = (double)interp_v(ic, m->min_x * 16.0);
        a = u1; b = u0; u0 = u1; u1 = b; c = v1; d = v0;
    }
    else if (m->uv_east == 3)
    {
        u0 = (double)interp_u(ic, 16.0 - m->min_x * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->max_x * 16.0);
        v0 = (double)interp_v(ic, m->max_y * 16.0);
        v1 = (double)interp_v(ic, m->min_y * 16.0);
        a = u1; b = u0; c = v0; d = v1;
    }

    double x0 = x + m->min_x, x1 = x + m->max_x;
    double y0 = y + m->min_y, y1 = y + m->max_y;
    double zz = z + m->min_z;

    if (m->render_from_inside) { x0 = x + m->max_x; x1 = x + m->min_x; }

    if (m->enable_ao)
    {
        tess_set_color_opaque_f(t, m->r_tl, m->g_tl, m->b_tl);
        tess_set_brightness(t, m->bright_tl);
        tess_add_vertex_with_uv(t, x0, y1, zz, a, c);
        tess_set_color_opaque_f(t, m->r_bl, m->g_bl, m->b_bl);
        tess_set_brightness(t, m->bright_bl);
        tess_add_vertex_with_uv(t, x1, y1, zz, u0, v0);
        tess_set_color_opaque_f(t, m->r_br, m->g_br, m->b_br);
        tess_set_brightness(t, m->bright_br);
        tess_add_vertex_with_uv(t, x1, y0, zz, b, d);
        tess_set_color_opaque_f(t, m->r_tr, m->g_tr, m->b_tr);
        tess_set_brightness(t, m->bright_tr);
        tess_add_vertex_with_uv(t, x0, y0, zz, u1, v1);
    }
    else
    {
        tess_add_vertex_with_uv(t, x0, y1, zz, a, c);
        tess_add_vertex_with_uv(t, x1, y1, zz, u0, v0);
        tess_add_vertex_with_uv(t, x1, y0, zz, b, d);
        tess_add_vertex_with_uv(t, x0, y0, zz, u1, v1);
    }
}

/** RenderBlocks.renderFaceZPos */
void render_face_z_pos(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic)
{
    if (m->override) ic = m->override;   /* RenderBlocks.hasOverrideBlockTexture */

    struct rb_tess *t = m->t;
    double u0 = (double)interp_u(ic, m->min_x * 16.0);
    double u1 = (double)interp_u(ic, m->max_x * 16.0);
    double v0 = (double)interp_v(ic, 16.0 - m->max_y * 16.0);
    double v1 = (double)interp_v(ic, 16.0 - m->min_y * 16.0);

    if (m->flip_texture) { double s = u0; u0 = u1; u1 = s; }
    if (m->min_x < 0.0 || m->max_x > 1.0) { u0 = (double)ic->min_u; u1 = (double)ic->max_u; }
    if (m->min_y < 0.0 || m->max_y > 1.0) { v0 = (double)ic->min_v; v1 = (double)ic->max_v; }

    double a = u1, b = u0, c = v0, d = v1;

    if (m->uv_west == 1)
    {
        u0 = (double)interp_u(ic, m->min_y * 16.0);
        v1 = (double)interp_v(ic, 16.0 - m->min_x * 16.0);
        u1 = (double)interp_u(ic, m->max_y * 16.0);
        v0 = (double)interp_v(ic, 16.0 - m->max_x * 16.0);
        c = v0; d = v1; a = u0; b = u1; v0 = v1; v1 = c;
    }
    else if (m->uv_west == 2)
    {
        u0 = (double)interp_u(ic, 16.0 - m->max_y * 16.0);
        v0 = (double)interp_v(ic, m->min_x * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->min_y * 16.0);
        v1 = (double)interp_v(ic, m->max_x * 16.0);
        a = u1; b = u0; u0 = u1; u1 = b; c = v1; d = v0;
    }
    else if (m->uv_west == 3)
    {
        u0 = (double)interp_u(ic, 16.0 - m->min_x * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->max_x * 16.0);
        v0 = (double)interp_v(ic, m->max_y * 16.0);
        v1 = (double)interp_v(ic, m->min_y * 16.0);
        a = u1; b = u0; c = v0; d = v1;
    }

    double x0 = x + m->min_x, x1 = x + m->max_x;
    double y0 = y + m->min_y, y1 = y + m->max_y;
    double zz = z + m->max_z;

    if (m->render_from_inside) { x0 = x + m->max_x; x1 = x + m->min_x; }

    if (m->enable_ao)
    {
        tess_set_color_opaque_f(t, m->r_tl, m->g_tl, m->b_tl);
        tess_set_brightness(t, m->bright_tl);
        tess_add_vertex_with_uv(t, x0, y1, zz, u0, v0);
        tess_set_color_opaque_f(t, m->r_bl, m->g_bl, m->b_bl);
        tess_set_brightness(t, m->bright_bl);
        tess_add_vertex_with_uv(t, x0, y0, zz, b, d);
        tess_set_color_opaque_f(t, m->r_br, m->g_br, m->b_br);
        tess_set_brightness(t, m->bright_br);
        tess_add_vertex_with_uv(t, x1, y0, zz, u1, v1);
        tess_set_color_opaque_f(t, m->r_tr, m->g_tr, m->b_tr);
        tess_set_brightness(t, m->bright_tr);
        tess_add_vertex_with_uv(t, x1, y1, zz, a, c);
    }
    else
    {
        tess_add_vertex_with_uv(t, x0, y1, zz, u0, v0);
        tess_add_vertex_with_uv(t, x0, y0, zz, b, d);
        tess_add_vertex_with_uv(t, x1, y0, zz, u1, v1);
        tess_add_vertex_with_uv(t, x1, y1, zz, a, c);
    }
}

/** RenderBlocks.renderFaceXNeg */
void render_face_x_neg(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic)
{
    if (m->override) ic = m->override;   /* RenderBlocks.hasOverrideBlockTexture */

    struct rb_tess *t = m->t;
    double u0 = (double)interp_u(ic, m->min_z * 16.0);
    double u1 = (double)interp_u(ic, m->max_z * 16.0);
    double v0 = (double)interp_v(ic, 16.0 - m->max_y * 16.0);
    double v1 = (double)interp_v(ic, 16.0 - m->min_y * 16.0);

    if (m->flip_texture) { double s = u0; u0 = u1; u1 = s; }
    if (m->min_z < 0.0 || m->max_z > 1.0) { u0 = (double)ic->min_u; u1 = (double)ic->max_u; }
    if (m->min_y < 0.0 || m->max_y > 1.0) { v0 = (double)ic->min_v; v1 = (double)ic->max_v; }

    double a = u1, b = u0, c = v0, d = v1;

    if (m->uv_north == 1)
    {
        u0 = (double)interp_u(ic, m->min_y * 16.0);
        v0 = (double)interp_v(ic, 16.0 - m->max_z * 16.0);
        u1 = (double)interp_u(ic, m->max_y * 16.0);
        v1 = (double)interp_v(ic, 16.0 - m->min_z * 16.0);
        c = v0; d = v1; a = u0; b = u1; v0 = v1; v1 = c;
    }
    else if (m->uv_north == 2)
    {
        u0 = (double)interp_u(ic, 16.0 - m->max_y * 16.0);
        v0 = (double)interp_v(ic, m->min_z * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->min_y * 16.0);
        v1 = (double)interp_v(ic, m->max_z * 16.0);
        a = u1; b = u0; u0 = u1; u1 = b; c = v1; d = v0;
    }
    else if (m->uv_north == 3)
    {
        u0 = (double)interp_u(ic, 16.0 - m->min_z * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->max_z * 16.0);
        v0 = (double)interp_v(ic, m->max_y * 16.0);
        v1 = (double)interp_v(ic, m->min_y * 16.0);
        a = u1; b = u0; c = v0; d = v1;
    }

    double xx = x + m->min_x;
    double y0 = y + m->min_y, y1 = y + m->max_y;
    double z0 = z + m->min_z, z1 = z + m->max_z;

    if (m->render_from_inside) { z0 = z + m->max_z; z1 = z + m->min_z; }

    if (m->enable_ao)
    {
        tess_set_color_opaque_f(t, m->r_tl, m->g_tl, m->b_tl);
        tess_set_brightness(t, m->bright_tl);
        tess_add_vertex_with_uv(t, xx, y1, z1, a, c);
        tess_set_color_opaque_f(t, m->r_bl, m->g_bl, m->b_bl);
        tess_set_brightness(t, m->bright_bl);
        tess_add_vertex_with_uv(t, xx, y1, z0, u0, v0);
        tess_set_color_opaque_f(t, m->r_br, m->g_br, m->b_br);
        tess_set_brightness(t, m->bright_br);
        tess_add_vertex_with_uv(t, xx, y0, z0, b, d);
        tess_set_color_opaque_f(t, m->r_tr, m->g_tr, m->b_tr);
        tess_set_brightness(t, m->bright_tr);
        tess_add_vertex_with_uv(t, xx, y0, z1, u1, v1);
    }
    else
    {
        tess_add_vertex_with_uv(t, xx, y1, z1, a, c);
        tess_add_vertex_with_uv(t, xx, y1, z0, u0, v0);
        tess_add_vertex_with_uv(t, xx, y0, z0, b, d);
        tess_add_vertex_with_uv(t, xx, y0, z1, u1, v1);
    }
}

/** RenderBlocks.renderFaceXPos */
void render_face_x_pos(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic)
{
    if (m->override) ic = m->override;   /* RenderBlocks.hasOverrideBlockTexture */

    struct rb_tess *t = m->t;
    double u0 = (double)interp_u(ic, m->min_z * 16.0);
    double u1 = (double)interp_u(ic, m->max_z * 16.0);

    if (m->f152631f)
    {
        u1 = (double)interp_u(ic, (1.0 - m->min_z) * 16.0);
        u0 = (double)interp_u(ic, (1.0 - m->max_z) * 16.0);
    }

    double v0 = (double)interp_v(ic, 16.0 - m->max_y * 16.0);
    double v1 = (double)interp_v(ic, 16.0 - m->min_y * 16.0);

    if (m->flip_texture) { double s = u0; u0 = u1; u1 = s; }
    if (m->min_z < 0.0 || m->max_z > 1.0) { u0 = (double)ic->min_u; u1 = (double)ic->max_u; }
    if (m->min_y < 0.0 || m->max_y > 1.0) { v0 = (double)ic->min_v; v1 = (double)ic->max_v; }

    double a = u1, b = u0, c = v0, d = v1;

    if (m->uv_south == 2)
    {
        u0 = (double)interp_u(ic, m->min_y * 16.0);
        v0 = (double)interp_v(ic, 16.0 - m->min_z * 16.0);
        u1 = (double)interp_u(ic, m->max_y * 16.0);
        v1 = (double)interp_v(ic, 16.0 - m->max_z * 16.0);
        c = v0; d = v1; a = u0; b = u1; v0 = v1; v1 = c;
    }
    else if (m->uv_south == 1)
    {
        u0 = (double)interp_u(ic, 16.0 - m->max_y * 16.0);
        v0 = (double)interp_v(ic, m->max_z * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->min_y * 16.0);
        v1 = (double)interp_v(ic, m->min_z * 16.0);
        a = u1; b = u0; u0 = u1; u1 = b; c = v1; d = v0;
    }
    else if (m->uv_south == 3)
    {
        u0 = (double)interp_u(ic, 16.0 - m->min_z * 16.0);
        u1 = (double)interp_u(ic, 16.0 - m->max_z * 16.0);
        v0 = (double)interp_v(ic, m->max_y * 16.0);
        v1 = (double)interp_v(ic, m->min_y * 16.0);
        a = u1; b = u0; c = v0; d = v1;
    }

    double xx = x + m->max_x;
    double y0 = y + m->min_y, y1 = y + m->max_y;
    double z0 = z + m->min_z, z1 = z + m->max_z;

    if (m->render_from_inside) { z0 = z + m->max_z; z1 = z + m->min_z; }

    if (m->enable_ao)
    {
        tess_set_color_opaque_f(t, m->r_tl, m->g_tl, m->b_tl);
        tess_set_brightness(t, m->bright_tl);
        tess_add_vertex_with_uv(t, xx, y0, z1, b, d);
        tess_set_color_opaque_f(t, m->r_bl, m->g_bl, m->b_bl);
        tess_set_brightness(t, m->bright_bl);
        tess_add_vertex_with_uv(t, xx, y0, z0, u1, v1);
        tess_set_color_opaque_f(t, m->r_br, m->g_br, m->b_br);
        tess_set_brightness(t, m->bright_br);
        tess_add_vertex_with_uv(t, xx, y1, z0, a, c);
        tess_set_color_opaque_f(t, m->r_tr, m->g_tr, m->b_tr);
        tess_set_brightness(t, m->bright_tr);
        tess_add_vertex_with_uv(t, xx, y1, z1, u0, v0);
    }
    else
    {
        tess_add_vertex_with_uv(t, xx, y0, z1, b, d);
        tess_add_vertex_with_uv(t, xx, y0, z0, u1, v1);
        tess_add_vertex_with_uv(t, xx, y1, z0, a, c);
        tess_add_vertex_with_uv(t, xx, y1, z1, u0, v0);
    }
}

/* ------------------------------------------- the standard block paths */

void set_face_color(struct rb_mesher *m, float r, float g, float b)
{
    m->r_tl = m->r_bl = m->r_br = m->r_tr = r;
    m->g_tl = m->g_bl = m->g_br = m->g_tr = g;
    m->b_tl = m->b_bl = m->b_br = m->b_tr = b;
}

/** colorRedTopLeft *= var9 ... colorRedTopRight *= var12, per channel. */
void scale_face_colors(struct rb_mesher *m, float tl, float bl, float br, float tr)
{
    m->r_tl *= tl; m->g_tl *= tl; m->b_tl *= tl;
    m->r_bl *= bl; m->g_bl *= bl; m->b_bl *= bl;
    m->r_br *= br; m->g_br *= br; m->b_br *= br;
    m->r_tr *= tr; m->g_tr *= tr; m->b_tr *= tr;
}

/** The fancy grass overlay scales every corner by the block colour again. */
static void overlay_scale(struct rb_mesher *m, float r, float g, float b)
{
    m->r_tl *= r; m->r_bl *= r; m->r_br *= r; m->r_tr *= r;
    m->g_tl *= g; m->g_bl *= g; m->g_br *= g; m->g_tr *= g;
    m->b_tl *= b; m->b_bl *= b; m->b_br *= b; m->b_tr *= b;
}

#define BL(x, y, z) block_brightness(m, id, (x), (y), (z))
#define AL(x, y, z) ao_light_of(m, (x), (y), (z))
#define CG(x, y, z) can_block_grass(m, (x), (y), (z))
#define IOC(x, y, z) opaque_cube(m, (x), (y), (z))

/** RenderBlocks.renderStandardBlockWithAmbientOcclusion */
int render_standard_block_ao(struct rb_mesher *m, int id, int x, int y, int z,
                                    float p5, float p6, float p7)
{
    struct rb_tess *t = m->t;
    m->enable_ao = 1;
    int drawn = 0;
    float f9 = 0.0F, f10 = 0.0F, f11 = 0.0F, f12 = 0.0F;
    int gc = 1;
    int base = block_brightness(m, id, x, y, z);
    int32_t b20;
    float f21;

    tess_set_brightness(t, 983055);

    if (icon_is(m, table_icon_index(m, id, 0, 1), "grass_top")) gc = 0;

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y - 1, z, 0))
    {
        if (m->min_y <= 0.0) --y;

        m->b_xy_nn = BL(x - 1, y, z);
        m->b_yz_nn = BL(x, y, z - 1);
        m->b_yz_np = BL(x, y, z + 1);
        m->b_xy_pn = BL(x + 1, y, z);
        m->l_xy_nn = AL(x - 1, y, z);
        m->l_yz_nn = AL(x, y, z - 1);
        m->l_yz_np = AL(x, y, z + 1);
        m->l_xy_pn = AL(x + 1, y, z);
        int v16 = CG(x + 1, y - 1, z);
        int v17 = CG(x - 1, y - 1, z);
        int v18 = CG(x, y - 1, z + 1);
        int v19 = CG(x, y - 1, z - 1);

        if (!v19 && !v17) { m->l_xyz_nnn = m->l_xy_nn; m->b_xyz_nnn = m->b_xy_nn; }
        else { m->l_xyz_nnn = AL(x - 1, y, z - 1); m->b_xyz_nnn = BL(x - 1, y, z - 1); }

        if (!v18 && !v17) { m->l_xyz_nnp = m->l_xy_nn; m->b_xyz_nnp = m->b_xy_nn; }
        else { m->l_xyz_nnp = AL(x - 1, y, z + 1); m->b_xyz_nnp = BL(x - 1, y, z + 1); }

        if (!v19 && !v16) { m->l_xyz_pnn = m->l_xy_pn; m->b_xyz_pnn = m->b_xy_pn; }
        else { m->l_xyz_pnn = AL(x + 1, y, z - 1); m->b_xyz_pnn = BL(x + 1, y, z - 1); }

        if (!v18 && !v16) { m->l_xyz_pnp = m->l_xy_pn; m->b_xyz_pnp = m->b_xy_pn; }
        else { m->l_xyz_pnp = AL(x + 1, y, z + 1); m->b_xyz_pnp = BL(x + 1, y, z + 1); }

        if (m->min_y <= 0.0) ++y;

        b20 = base;
        if (m->min_y <= 0.0 || !IOC(x, y - 1, z)) b20 = BL(x, y - 1, z);

        f21 = AL(x, y - 1, z);
        f9 = (m->l_xyz_nnp + m->l_xy_nn + m->l_yz_np + f21) / 4.0F;
        f12 = (m->l_yz_np + f21 + m->l_xyz_pnp + m->l_xy_pn) / 4.0F;
        f11 = (f21 + m->l_yz_nn + m->l_xy_pn + m->l_xyz_pnn) / 4.0F;
        f10 = (m->l_xy_nn + m->l_xyz_nnn + f21 + m->l_yz_nn) / 4.0F;
        m->bright_tl = get_ao_brightness(m->b_xyz_nnp, m->b_xy_nn, m->b_yz_np, b20);
        m->bright_tr = get_ao_brightness(m->b_yz_np, m->b_xyz_pnp, m->b_xy_pn, b20);
        m->bright_br = get_ao_brightness(m->b_yz_nn, m->b_xy_pn, m->b_xyz_pnn, b20);
        m->bright_bl = get_ao_brightness(m->b_xy_nn, m->b_xyz_nnn, m->b_yz_nn, b20);

        if (gc) set_face_color(m, p5 * 0.5F, p6 * 0.5F, p7 * 0.5F);
        else set_face_color(m, 0.5F, 0.5F, 0.5F);

        scale_face_colors(m, f9, f10, f11, f12);
        render_face_y_neg(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 0));
        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y + 1, z, 1))
    {
        if (m->max_y >= 1.0) ++y;

        m->b_xy_np = BL(x - 1, y, z);
        m->b_xy_pp = BL(x + 1, y, z);
        m->b_yz_pn = BL(x, y, z - 1);
        m->b_yz_pp = BL(x, y, z + 1);
        m->l_xy_np = AL(x - 1, y, z);
        m->l_xy_pp = AL(x + 1, y, z);
        m->l_yz_pn = AL(x, y, z - 1);
        m->l_yz_pp = AL(x, y, z + 1);
        int v16 = CG(x + 1, y + 1, z);
        int v17 = CG(x - 1, y + 1, z);
        int v18 = CG(x, y + 1, z + 1);
        int v19 = CG(x, y + 1, z - 1);

        if (!v19 && !v17) { m->l_xyz_npn = m->l_xy_np; m->b_xyz_npn = m->b_xy_np; }
        else { m->l_xyz_npn = AL(x - 1, y, z - 1); m->b_xyz_npn = BL(x - 1, y, z - 1); }

        if (!v19 && !v16) { m->l_xyz_ppn = m->l_xy_pp; m->b_xyz_ppn = m->b_xy_pp; }
        else { m->l_xyz_ppn = AL(x + 1, y, z - 1); m->b_xyz_ppn = BL(x + 1, y, z - 1); }

        if (!v18 && !v17) { m->l_xyz_npp = m->l_xy_np; m->b_xyz_npp = m->b_xy_np; }
        else { m->l_xyz_npp = AL(x - 1, y, z + 1); m->b_xyz_npp = BL(x - 1, y, z + 1); }

        if (!v18 && !v16) { m->l_xyz_ppp = m->l_xy_pp; m->b_xyz_ppp = m->b_xy_pp; }
        else { m->l_xyz_ppp = AL(x + 1, y, z + 1); m->b_xyz_ppp = BL(x + 1, y, z + 1); }

        if (m->max_y >= 1.0) --y;

        b20 = base;
        if (m->max_y >= 1.0 || !IOC(x, y + 1, z)) b20 = BL(x, y + 1, z);

        f21 = AL(x, y + 1, z);
        f12 = (m->l_xyz_npp + m->l_xy_np + m->l_yz_pp + f21) / 4.0F;
        f9 = (m->l_yz_pp + f21 + m->l_xyz_ppp + m->l_xy_pp) / 4.0F;
        f10 = (f21 + m->l_yz_pn + m->l_xy_pp + m->l_xyz_ppn) / 4.0F;
        f11 = (m->l_xy_np + m->l_xyz_npn + f21 + m->l_yz_pn) / 4.0F;
        m->bright_tr = get_ao_brightness(m->b_xyz_npp, m->b_xy_np, m->b_yz_pp, b20);
        m->bright_tl = get_ao_brightness(m->b_yz_pp, m->b_xyz_ppp, m->b_xy_pp, b20);
        m->bright_bl = get_ao_brightness(m->b_yz_pn, m->b_xy_pp, m->b_xyz_ppn, b20);
        m->bright_br = get_ao_brightness(m->b_xy_np, m->b_xyz_npn, m->b_yz_pn, b20);
        set_face_color(m, p5, p6, p7);
        scale_face_colors(m, f9, f10, f11, f12);
        render_face_y_pos(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 1));
        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y, z - 1, 2))
    {
        if (m->min_z <= 0.0) --z;

        m->l_xz_nn = AL(x - 1, y, z);
        m->l_yz_nn = AL(x, y - 1, z);
        m->l_yz_pn = AL(x, y + 1, z);
        m->l_xz_pn = AL(x + 1, y, z);
        m->b_xz_nn = BL(x - 1, y, z);
        m->b_yz_nn = BL(x, y - 1, z);
        m->b_yz_pn = BL(x, y + 1, z);
        m->b_xz_pn = BL(x + 1, y, z);
        int v16 = CG(x + 1, y, z - 1);
        int v17 = CG(x - 1, y, z - 1);
        int v18 = CG(x, y + 1, z - 1);
        int v19 = CG(x, y - 1, z - 1);

        if (!v17 && !v19) { m->l_xyz_nnn = m->l_xz_nn; m->b_xyz_nnn = m->b_xz_nn; }
        else { m->l_xyz_nnn = AL(x - 1, y - 1, z); m->b_xyz_nnn = BL(x - 1, y - 1, z); }

        if (!v17 && !v18) { m->l_xyz_npn = m->l_xz_nn; m->b_xyz_npn = m->b_xz_nn; }
        else { m->l_xyz_npn = AL(x - 1, y + 1, z); m->b_xyz_npn = BL(x - 1, y + 1, z); }

        if (!v16 && !v19) { m->l_xyz_pnn = m->l_xz_pn; m->b_xyz_pnn = m->b_xz_pn; }
        else { m->l_xyz_pnn = AL(x + 1, y - 1, z); m->b_xyz_pnn = BL(x + 1, y - 1, z); }

        if (!v16 && !v18) { m->l_xyz_ppn = m->l_xz_pn; m->b_xyz_ppn = m->b_xz_pn; }
        else { m->l_xyz_ppn = AL(x + 1, y + 1, z); m->b_xyz_ppn = BL(x + 1, y + 1, z); }

        if (m->min_z <= 0.0) ++z;

        b20 = base;
        if (m->min_z <= 0.0 || !IOC(x, y, z - 1)) b20 = BL(x, y, z - 1);

        f21 = AL(x, y, z - 1);
        f9 = (m->l_xz_nn + m->l_xyz_npn + f21 + m->l_yz_pn) / 4.0F;
        f10 = (f21 + m->l_yz_pn + m->l_xz_pn + m->l_xyz_ppn) / 4.0F;
        f11 = (m->l_yz_nn + f21 + m->l_xyz_pnn + m->l_xz_pn) / 4.0F;
        f12 = (m->l_xyz_nnn + m->l_xz_nn + m->l_yz_nn + f21) / 4.0F;
        m->bright_tl = get_ao_brightness(m->b_xz_nn, m->b_xyz_npn, m->b_yz_pn, b20);
        m->bright_bl = get_ao_brightness(m->b_yz_pn, m->b_xz_pn, m->b_xyz_ppn, b20);
        m->bright_br = get_ao_brightness(m->b_yz_nn, m->b_xyz_pnn, m->b_xz_pn, b20);
        m->bright_tr = get_ao_brightness(m->b_xyz_nnn, m->b_xz_nn, m->b_yz_nn, b20);

        if (gc) set_face_color(m, p5 * 0.8F, p6 * 0.8F, p7 * 0.8F);
        else set_face_color(m, 0.8F, 0.8F, 0.8F);

        scale_face_colors(m, f9, f10, f11, f12);
        int ico = block_icon_world_index(m, id, x, y, z, 2);
        render_face_z_neg(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            overlay_scale(m, p5, p6, p7);
            render_face_z_neg(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y, z + 1, 3))
    {
        if (m->max_z >= 1.0) ++z;

        m->l_xz_np = AL(x - 1, y, z);
        m->l_xz_pp = AL(x + 1, y, z);
        m->l_yz_np = AL(x, y - 1, z);
        m->l_yz_pp = AL(x, y + 1, z);
        m->b_xz_np = BL(x - 1, y, z);
        m->b_xz_pp = BL(x + 1, y, z);
        m->b_yz_np = BL(x, y - 1, z);
        m->b_yz_pp = BL(x, y + 1, z);
        int v16 = CG(x + 1, y, z + 1);
        int v17 = CG(x - 1, y, z + 1);
        int v18 = CG(x, y + 1, z + 1);
        int v19 = CG(x, y - 1, z + 1);

        if (!v17 && !v19) { m->l_xyz_nnp = m->l_xz_np; m->b_xyz_nnp = m->b_xz_np; }
        else { m->l_xyz_nnp = AL(x - 1, y - 1, z); m->b_xyz_nnp = BL(x - 1, y - 1, z); }

        if (!v17 && !v18) { m->l_xyz_npp = m->l_xz_np; m->b_xyz_npp = m->b_xz_np; }
        else { m->l_xyz_npp = AL(x - 1, y + 1, z); m->b_xyz_npp = BL(x - 1, y + 1, z); }

        if (!v16 && !v19) { m->l_xyz_pnp = m->l_xz_pp; m->b_xyz_pnp = m->b_xz_pp; }
        else { m->l_xyz_pnp = AL(x + 1, y - 1, z); m->b_xyz_pnp = BL(x + 1, y - 1, z); }

        if (!v16 && !v18) { m->l_xyz_ppp = m->l_xz_pp; m->b_xyz_ppp = m->b_xz_pp; }
        else { m->l_xyz_ppp = AL(x + 1, y + 1, z); m->b_xyz_ppp = BL(x + 1, y + 1, z); }

        if (m->max_z >= 1.0) --z;

        b20 = base;
        if (m->max_z >= 1.0 || !IOC(x, y, z + 1)) b20 = BL(x, y, z + 1);

        f21 = AL(x, y, z + 1);
        f9 = (m->l_xz_np + m->l_xyz_npp + f21 + m->l_yz_pp) / 4.0F;
        f12 = (f21 + m->l_yz_pp + m->l_xz_pp + m->l_xyz_ppp) / 4.0F;
        f11 = (m->l_yz_np + f21 + m->l_xyz_pnp + m->l_xz_pp) / 4.0F;
        f10 = (m->l_xyz_nnp + m->l_xz_np + m->l_yz_np + f21) / 4.0F;
        m->bright_tl = get_ao_brightness(m->b_xz_np, m->b_xyz_npp, m->b_yz_pp, b20);
        m->bright_tr = get_ao_brightness(m->b_yz_pp, m->b_xz_pp, m->b_xyz_ppp, b20);
        m->bright_br = get_ao_brightness(m->b_yz_np, m->b_xyz_pnp, m->b_xz_pp, b20);
        m->bright_bl = get_ao_brightness(m->b_xyz_nnp, m->b_xz_np, m->b_yz_np, b20);

        if (gc) set_face_color(m, p5 * 0.8F, p6 * 0.8F, p7 * 0.8F);
        else set_face_color(m, 0.8F, 0.8F, 0.8F);

        scale_face_colors(m, f9, f10, f11, f12);
        int ico = block_icon_world_index(m, id, x, y, z, 3);
        render_face_z_pos(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            overlay_scale(m, p5, p6, p7);
            render_face_z_pos(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x - 1, y, z, 4))
    {
        if (m->min_x <= 0.0) --x;

        m->l_xy_nn = AL(x, y - 1, z);
        m->l_xz_nn = AL(x, y, z - 1);
        m->l_xz_np = AL(x, y, z + 1);
        m->l_xy_np = AL(x, y + 1, z);
        m->b_xy_nn = BL(x, y - 1, z);
        m->b_xz_nn = BL(x, y, z - 1);
        m->b_xz_np = BL(x, y, z + 1);
        m->b_xy_np = BL(x, y + 1, z);
        int v16 = CG(x - 1, y + 1, z);
        int v17 = CG(x - 1, y - 1, z);
        int v18 = CG(x - 1, y, z - 1);
        int v19 = CG(x - 1, y, z + 1);

        if (!v18 && !v17) { m->l_xyz_nnn = m->l_xz_nn; m->b_xyz_nnn = m->b_xz_nn; }
        else { m->l_xyz_nnn = AL(x, y - 1, z - 1); m->b_xyz_nnn = BL(x, y - 1, z - 1); }

        if (!v19 && !v17) { m->l_xyz_nnp = m->l_xz_np; m->b_xyz_nnp = m->b_xz_np; }
        else { m->l_xyz_nnp = AL(x, y - 1, z + 1); m->b_xyz_nnp = BL(x, y - 1, z + 1); }

        if (!v18 && !v16) { m->l_xyz_npn = m->l_xz_nn; m->b_xyz_npn = m->b_xz_nn; }
        else { m->l_xyz_npn = AL(x, y + 1, z - 1); m->b_xyz_npn = BL(x, y + 1, z - 1); }

        if (!v19 && !v16) { m->l_xyz_npp = m->l_xz_np; m->b_xyz_npp = m->b_xz_np; }
        else { m->l_xyz_npp = AL(x, y + 1, z + 1); m->b_xyz_npp = BL(x, y + 1, z + 1); }

        if (m->min_x <= 0.0) ++x;

        b20 = base;
        if (m->min_x <= 0.0 || !IOC(x - 1, y, z)) b20 = BL(x - 1, y, z);

        f21 = AL(x - 1, y, z);
        f12 = (m->l_xy_nn + m->l_xyz_nnp + f21 + m->l_xz_np) / 4.0F;
        f9 = (f21 + m->l_xz_np + m->l_xy_np + m->l_xyz_npp) / 4.0F;
        f10 = (m->l_xz_nn + f21 + m->l_xyz_npn + m->l_xy_np) / 4.0F;
        f11 = (m->l_xyz_nnn + m->l_xy_nn + m->l_xz_nn + f21) / 4.0F;
        m->bright_tr = get_ao_brightness(m->b_xy_nn, m->b_xyz_nnp, m->b_xz_np, b20);
        m->bright_tl = get_ao_brightness(m->b_xz_np, m->b_xy_np, m->b_xyz_npp, b20);
        m->bright_bl = get_ao_brightness(m->b_xz_nn, m->b_xyz_npn, m->b_xy_np, b20);
        m->bright_br = get_ao_brightness(m->b_xyz_nnn, m->b_xy_nn, m->b_xz_nn, b20);

        if (gc) set_face_color(m, p5 * 0.6F, p6 * 0.6F, p7 * 0.6F);
        else set_face_color(m, 0.6F, 0.6F, 0.6F);

        scale_face_colors(m, f9, f10, f11, f12);
        int ico = block_icon_world_index(m, id, x, y, z, 4);
        render_face_x_neg(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            overlay_scale(m, p5, p6, p7);
            render_face_x_neg(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x + 1, y, z, 5))
    {
        if (m->max_x >= 1.0) ++x;

        m->l_xy_pn = AL(x, y - 1, z);
        m->l_xz_pn = AL(x, y, z - 1);
        m->l_xz_pp = AL(x, y, z + 1);
        m->l_xy_pp = AL(x, y + 1, z);
        m->b_xy_pn = BL(x, y - 1, z);
        m->b_xz_pn = BL(x, y, z - 1);
        m->b_xz_pp = BL(x, y, z + 1);
        m->b_xy_pp = BL(x, y + 1, z);
        int v16 = CG(x + 1, y + 1, z);
        int v17 = CG(x + 1, y - 1, z);
        int v18 = CG(x + 1, y, z + 1);
        int v19 = CG(x + 1, y, z - 1);

        if (!v17 && !v19) { m->l_xyz_pnn = m->l_xz_pn; m->b_xyz_pnn = m->b_xz_pn; }
        else { m->l_xyz_pnn = AL(x, y - 1, z - 1); m->b_xyz_pnn = BL(x, y - 1, z - 1); }

        if (!v17 && !v18) { m->l_xyz_pnp = m->l_xz_pp; m->b_xyz_pnp = m->b_xz_pp; }
        else { m->l_xyz_pnp = AL(x, y - 1, z + 1); m->b_xyz_pnp = BL(x, y - 1, z + 1); }

        if (!v16 && !v19) { m->l_xyz_ppn = m->l_xz_pn; m->b_xyz_ppn = m->b_xz_pn; }
        else { m->l_xyz_ppn = AL(x, y + 1, z - 1); m->b_xyz_ppn = BL(x, y + 1, z - 1); }

        if (!v16 && !v18) { m->l_xyz_ppp = m->l_xz_pp; m->b_xyz_ppp = m->b_xz_pp; }
        else { m->l_xyz_ppp = AL(x, y + 1, z + 1); m->b_xyz_ppp = BL(x, y + 1, z + 1); }

        if (m->max_x >= 1.0) --x;

        b20 = base;
        if (m->max_x >= 1.0 || !IOC(x + 1, y, z)) b20 = BL(x + 1, y, z);

        f21 = AL(x + 1, y, z);
        f9 = (m->l_xy_pn + m->l_xyz_pnp + f21 + m->l_xz_pp) / 4.0F;
        f10 = (m->l_xyz_pnn + m->l_xy_pn + m->l_xz_pn + f21) / 4.0F;
        f11 = (m->l_xz_pn + f21 + m->l_xyz_ppn + m->l_xy_pp) / 4.0F;
        f12 = (f21 + m->l_xz_pp + m->l_xy_pp + m->l_xyz_ppp) / 4.0F;
        m->bright_tl = get_ao_brightness(m->b_xy_pn, m->b_xyz_pnp, m->b_xz_pp, b20);
        m->bright_tr = get_ao_brightness(m->b_xz_pp, m->b_xy_pp, m->b_xyz_ppp, b20);
        m->bright_br = get_ao_brightness(m->b_xz_pn, m->b_xyz_ppn, m->b_xy_pp, b20);
        m->bright_bl = get_ao_brightness(m->b_xyz_pnn, m->b_xy_pn, m->b_xz_pn, b20);

        if (gc) set_face_color(m, p5 * 0.6F, p6 * 0.6F, p7 * 0.6F);
        else set_face_color(m, 0.6F, 0.6F, 0.6F);

        scale_face_colors(m, f9, f10, f11, f12);
        int ico = block_icon_world_index(m, id, x, y, z, 5);
        render_face_x_pos(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            overlay_scale(m, p5, p6, p7);
            render_face_x_pos(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    m->enable_ao = 0;
    return drawn;
}

/** RenderBlocks.renderStandardBlockWithAmbientOcclusionPartial. Faces 0 and 1
 * are the full method's; the four side faces interpolate the corner light
 * values across the partial bounds and blend the corner brightness the same
 * way, which is what mixAoBrightness is for. */
int render_standard_block_ao_partial(struct rb_mesher *m, int id, int x, int y, int z,
                                            float p5, float p6, float p7)
{
    struct rb_tess *t = m->t;
    m->enable_ao = 1;
    int drawn = 0;
    float f9 = 0.0F, f10 = 0.0F, f11 = 0.0F, f12 = 0.0F;
    float f22, f23, f24, f25;
    int32_t i26, i27, i28, i29;
    int gc = 1;
    int base = block_brightness(m, id, x, y, z);
    int32_t b20;
    float f21;
    double w22, w23, w24, w25;

    tess_set_brightness(t, 983055);

    if (icon_is(m, table_icon_index(m, id, 0, 1), "grass_top")) gc = 0;

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y - 1, z, 0))
    {
        if (m->min_y <= 0.0) --y;

        m->b_xy_nn = BL(x - 1, y, z);
        m->b_yz_nn = BL(x, y, z - 1);
        m->b_yz_np = BL(x, y, z + 1);
        m->b_xy_pn = BL(x + 1, y, z);
        m->l_xy_nn = AL(x - 1, y, z);
        m->l_yz_nn = AL(x, y, z - 1);
        m->l_yz_np = AL(x, y, z + 1);
        m->l_xy_pn = AL(x + 1, y, z);
        int v16 = CG(x + 1, y - 1, z);
        int v17 = CG(x - 1, y - 1, z);
        int v18 = CG(x, y - 1, z + 1);
        int v19 = CG(x, y - 1, z - 1);

        if (!v19 && !v17) { m->l_xyz_nnn = m->l_xy_nn; m->b_xyz_nnn = m->b_xy_nn; }
        else { m->l_xyz_nnn = AL(x - 1, y, z - 1); m->b_xyz_nnn = BL(x - 1, y, z - 1); }

        if (!v18 && !v17) { m->l_xyz_nnp = m->l_xy_nn; m->b_xyz_nnp = m->b_xy_nn; }
        else { m->l_xyz_nnp = AL(x - 1, y, z + 1); m->b_xyz_nnp = BL(x - 1, y, z + 1); }

        if (!v19 && !v16) { m->l_xyz_pnn = m->l_xy_pn; m->b_xyz_pnn = m->b_xy_pn; }
        else { m->l_xyz_pnn = AL(x + 1, y, z - 1); m->b_xyz_pnn = BL(x + 1, y, z - 1); }

        if (!v18 && !v16) { m->l_xyz_pnp = m->l_xy_pn; m->b_xyz_pnp = m->b_xy_pn; }
        else { m->l_xyz_pnp = AL(x + 1, y, z + 1); m->b_xyz_pnp = BL(x + 1, y, z + 1); }

        if (m->min_y <= 0.0) ++y;

        b20 = base;
        if (m->min_y <= 0.0 || !IOC(x, y - 1, z)) b20 = BL(x, y - 1, z);

        f21 = AL(x, y - 1, z);
        f9 = (m->l_xyz_nnp + m->l_xy_nn + m->l_yz_np + f21) / 4.0F;
        f12 = (m->l_yz_np + f21 + m->l_xyz_pnp + m->l_xy_pn) / 4.0F;
        f11 = (f21 + m->l_yz_nn + m->l_xy_pn + m->l_xyz_pnn) / 4.0F;
        f10 = (m->l_xy_nn + m->l_xyz_nnn + f21 + m->l_yz_nn) / 4.0F;
        m->bright_tl = get_ao_brightness(m->b_xyz_nnp, m->b_xy_nn, m->b_yz_np, b20);
        m->bright_tr = get_ao_brightness(m->b_yz_np, m->b_xyz_pnp, m->b_xy_pn, b20);
        m->bright_br = get_ao_brightness(m->b_yz_nn, m->b_xy_pn, m->b_xyz_pnn, b20);
        m->bright_bl = get_ao_brightness(m->b_xy_nn, m->b_xyz_nnn, m->b_yz_nn, b20);

        if (gc) set_face_color(m, p5 * 0.5F, p6 * 0.5F, p7 * 0.5F);
        else set_face_color(m, 0.5F, 0.5F, 0.5F);

        scale_face_colors(m, f9, f10, f11, f12);
        render_face_y_neg(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 0));
        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y + 1, z, 1))
    {
        if (m->max_y >= 1.0) ++y;

        m->b_xy_np = BL(x - 1, y, z);
        m->b_xy_pp = BL(x + 1, y, z);
        m->b_yz_pn = BL(x, y, z - 1);
        m->b_yz_pp = BL(x, y, z + 1);
        m->l_xy_np = AL(x - 1, y, z);
        m->l_xy_pp = AL(x + 1, y, z);
        m->l_yz_pn = AL(x, y, z - 1);
        m->l_yz_pp = AL(x, y, z + 1);
        int v16 = CG(x + 1, y + 1, z);
        int v17 = CG(x - 1, y + 1, z);
        int v18 = CG(x, y + 1, z + 1);
        int v19 = CG(x, y + 1, z - 1);

        if (!v19 && !v17) { m->l_xyz_npn = m->l_xy_np; m->b_xyz_npn = m->b_xy_np; }
        else { m->l_xyz_npn = AL(x - 1, y, z - 1); m->b_xyz_npn = BL(x - 1, y, z - 1); }

        if (!v19 && !v16) { m->l_xyz_ppn = m->l_xy_pp; m->b_xyz_ppn = m->b_xy_pp; }
        else { m->l_xyz_ppn = AL(x + 1, y, z - 1); m->b_xyz_ppn = BL(x + 1, y, z - 1); }

        if (!v18 && !v17) { m->l_xyz_npp = m->l_xy_np; m->b_xyz_npp = m->b_xy_np; }
        else { m->l_xyz_npp = AL(x - 1, y, z + 1); m->b_xyz_npp = BL(x - 1, y, z + 1); }

        if (!v18 && !v16) { m->l_xyz_ppp = m->l_xy_pp; m->b_xyz_ppp = m->b_xy_pp; }
        else { m->l_xyz_ppp = AL(x + 1, y, z + 1); m->b_xyz_ppp = BL(x + 1, y, z + 1); }

        if (m->max_y >= 1.0) --y;

        b20 = base;
        if (m->max_y >= 1.0 || !IOC(x, y + 1, z)) b20 = BL(x, y + 1, z);

        f21 = AL(x, y + 1, z);
        f12 = (m->l_xyz_npp + m->l_xy_np + m->l_yz_pp + f21) / 4.0F;
        f9 = (m->l_yz_pp + f21 + m->l_xyz_ppp + m->l_xy_pp) / 4.0F;
        f10 = (f21 + m->l_yz_pn + m->l_xy_pp + m->l_xyz_ppn) / 4.0F;
        f11 = (m->l_xy_np + m->l_xyz_npn + f21 + m->l_yz_pn) / 4.0F;
        m->bright_tr = get_ao_brightness(m->b_xyz_npp, m->b_xy_np, m->b_yz_pp, b20);
        m->bright_tl = get_ao_brightness(m->b_yz_pp, m->b_xyz_ppp, m->b_xy_pp, b20);
        m->bright_bl = get_ao_brightness(m->b_yz_pn, m->b_xy_pp, m->b_xyz_ppn, b20);
        m->bright_br = get_ao_brightness(m->b_xy_np, m->b_xyz_npn, m->b_yz_pn, b20);
        set_face_color(m, p5, p6, p7);
        scale_face_colors(m, f9, f10, f11, f12);
        render_face_y_pos(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 1));
        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y, z - 1, 2))
    {
        if (m->min_z <= 0.0) --z;

        m->l_xz_nn = AL(x - 1, y, z);
        m->l_yz_nn = AL(x, y - 1, z);
        m->l_yz_pn = AL(x, y + 1, z);
        m->l_xz_pn = AL(x + 1, y, z);
        m->b_xz_nn = BL(x - 1, y, z);
        m->b_yz_nn = BL(x, y - 1, z);
        m->b_yz_pn = BL(x, y + 1, z);
        m->b_xz_pn = BL(x + 1, y, z);
        int v16 = CG(x + 1, y, z - 1);
        int v17 = CG(x - 1, y, z - 1);
        int v18 = CG(x, y + 1, z - 1);
        int v19 = CG(x, y - 1, z - 1);

        if (!v17 && !v19) { m->l_xyz_nnn = m->l_xz_nn; m->b_xyz_nnn = m->b_xz_nn; }
        else { m->l_xyz_nnn = AL(x - 1, y - 1, z); m->b_xyz_nnn = BL(x - 1, y - 1, z); }

        if (!v17 && !v18) { m->l_xyz_npn = m->l_xz_nn; m->b_xyz_npn = m->b_xz_nn; }
        else { m->l_xyz_npn = AL(x - 1, y + 1, z); m->b_xyz_npn = BL(x - 1, y + 1, z); }

        if (!v16 && !v19) { m->l_xyz_pnn = m->l_xz_pn; m->b_xyz_pnn = m->b_xz_pn; }
        else { m->l_xyz_pnn = AL(x + 1, y - 1, z); m->b_xyz_pnn = BL(x + 1, y - 1, z); }

        if (!v16 && !v18) { m->l_xyz_ppn = m->l_xz_pn; m->b_xyz_ppn = m->b_xz_pn; }
        else { m->l_xyz_ppn = AL(x + 1, y + 1, z); m->b_xyz_ppn = BL(x + 1, y + 1, z); }

        if (m->min_z <= 0.0) ++z;

        b20 = base;
        if (m->min_z <= 0.0 || !IOC(x, y, z - 1)) b20 = BL(x, y, z - 1);

        f21 = AL(x, y, z - 1);
        f22 = (m->l_xz_nn + m->l_xyz_npn + f21 + m->l_yz_pn) / 4.0F;
        f23 = (f21 + m->l_yz_pn + m->l_xz_pn + m->l_xyz_ppn) / 4.0F;
        f24 = (m->l_yz_nn + f21 + m->l_xyz_pnn + m->l_xz_pn) / 4.0F;
        f25 = (m->l_xyz_nnn + m->l_xz_nn + m->l_yz_nn + f21) / 4.0F;
        w22 = m->max_y * (1.0 - m->min_x);
        w23 = m->max_y * m->min_x;
        w24 = (1.0 - m->max_y) * m->min_x;
        w25 = (1.0 - m->max_y) * (1.0 - m->min_x);
        f9 = (float)((double)f22 * w22 + (double)f23 * w23 + (double)f24 * w24 + (double)f25 * w25);
        {
            double a = m->max_y * (1.0 - m->max_x), b = m->max_y * m->max_x;
            double c = (1.0 - m->max_y) * m->max_x, d = (1.0 - m->max_y) * (1.0 - m->max_x);
            f10 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        {
            double a = m->min_y * (1.0 - m->max_x), b = m->min_y * m->max_x;
            double c = (1.0 - m->min_y) * m->max_x, d = (1.0 - m->min_y) * (1.0 - m->max_x);
            f11 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        {
            double a = m->min_y * (1.0 - m->min_x), b = m->min_y * m->min_x;
            double c = (1.0 - m->min_y) * m->min_x, d = (1.0 - m->min_y) * (1.0 - m->min_x);
            f12 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        i26 = get_ao_brightness(m->b_xz_nn, m->b_xyz_npn, m->b_yz_pn, b20);
        i27 = get_ao_brightness(m->b_yz_pn, m->b_xz_pn, m->b_xyz_ppn, b20);
        i28 = get_ao_brightness(m->b_yz_nn, m->b_xyz_pnn, m->b_xz_pn, b20);
        i29 = get_ao_brightness(m->b_xyz_nnn, m->b_xz_nn, m->b_yz_nn, b20);
        m->bright_tl = mix_ao_brightness(i26, i27, i28, i29, w22, w23, w24, w25);
        {
            double a = m->max_y * (1.0 - m->max_x), b = m->max_y * m->max_x;
            double c = (1.0 - m->max_y) * m->max_x, d = (1.0 - m->max_y) * (1.0 - m->max_x);
            m->bright_bl = mix_ao_brightness(i26, i27, i28, i29, a, b, c, d);
        }
        {
            double a = m->min_y * (1.0 - m->max_x), b = m->min_y * m->max_x;
            double c = (1.0 - m->min_y) * m->max_x, d = (1.0 - m->min_y) * (1.0 - m->max_x);
            m->bright_br = mix_ao_brightness(i26, i27, i28, i29, a, b, c, d);
        }
        {
            double a = m->min_y * (1.0 - m->min_x), b = m->min_y * m->min_x;
            double c = (1.0 - m->min_y) * m->min_x, d = (1.0 - m->min_y) * (1.0 - m->min_x);
            m->bright_tr = mix_ao_brightness(i26, i27, i28, i29, a, b, c, d);
        }

        if (gc) set_face_color(m, p5 * 0.8F, p6 * 0.8F, p7 * 0.8F);
        else set_face_color(m, 0.8F, 0.8F, 0.8F);

        scale_face_colors(m, f9, f10, f11, f12);
        int ico = block_icon_world_index(m, id, x, y, z, 2);
        render_face_z_neg(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            overlay_scale(m, p5, p6, p7);
            render_face_z_neg(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y, z + 1, 3))
    {
        if (m->max_z >= 1.0) ++z;

        m->l_xz_np = AL(x - 1, y, z);
        m->l_xz_pp = AL(x + 1, y, z);
        m->l_yz_np = AL(x, y - 1, z);
        m->l_yz_pp = AL(x, y + 1, z);
        m->b_xz_np = BL(x - 1, y, z);
        m->b_xz_pp = BL(x + 1, y, z);
        m->b_yz_np = BL(x, y - 1, z);
        m->b_yz_pp = BL(x, y + 1, z);
        int v16 = CG(x + 1, y, z + 1);
        int v17 = CG(x - 1, y, z + 1);
        int v18 = CG(x, y + 1, z + 1);
        int v19 = CG(x, y - 1, z + 1);

        if (!v17 && !v19) { m->l_xyz_nnp = m->l_xz_np; m->b_xyz_nnp = m->b_xz_np; }
        else { m->l_xyz_nnp = AL(x - 1, y - 1, z); m->b_xyz_nnp = BL(x - 1, y - 1, z); }

        if (!v17 && !v18) { m->l_xyz_npp = m->l_xz_np; m->b_xyz_npp = m->b_xz_np; }
        else { m->l_xyz_npp = AL(x - 1, y + 1, z); m->b_xyz_npp = BL(x - 1, y + 1, z); }

        if (!v16 && !v19) { m->l_xyz_pnp = m->l_xz_pp; m->b_xyz_pnp = m->b_xz_pp; }
        else { m->l_xyz_pnp = AL(x + 1, y - 1, z); m->b_xyz_pnp = BL(x + 1, y - 1, z); }

        if (!v16 && !v18) { m->l_xyz_ppp = m->l_xz_pp; m->b_xyz_ppp = m->b_xz_pp; }
        else { m->l_xyz_ppp = AL(x + 1, y + 1, z); m->b_xyz_ppp = BL(x + 1, y + 1, z); }

        if (m->max_z >= 1.0) --z;

        b20 = base;
        if (m->max_z >= 1.0 || !IOC(x, y, z + 1)) b20 = BL(x, y, z + 1);

        f21 = AL(x, y, z + 1);
        f22 = (m->l_xz_np + m->l_xyz_npp + f21 + m->l_yz_pp) / 4.0F;
        f23 = (f21 + m->l_yz_pp + m->l_xz_pp + m->l_xyz_ppp) / 4.0F;
        f24 = (m->l_yz_np + f21 + m->l_xyz_pnp + m->l_xz_pp) / 4.0F;
        f25 = (m->l_xyz_nnp + m->l_xz_np + m->l_yz_np + f21) / 4.0F;
        {
            double a = m->max_y * (1.0 - m->min_x), b = m->max_y * m->min_x;
            double c = (1.0 - m->max_y) * m->min_x, d = (1.0 - m->max_y) * (1.0 - m->min_x);
            f9 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        {
            double a = m->min_y * (1.0 - m->min_x), b = m->min_y * m->min_x;
            double c = (1.0 - m->min_y) * m->min_x, d = (1.0 - m->min_y) * (1.0 - m->min_x);
            f10 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        {
            double a = m->min_y * (1.0 - m->max_x), b = m->min_y * m->max_x;
            double c = (1.0 - m->min_y) * m->max_x, d = (1.0 - m->min_y) * (1.0 - m->max_x);
            f11 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        {
            double a = m->max_y * (1.0 - m->max_x), b = m->max_y * m->max_x;
            double c = (1.0 - m->max_y) * m->max_x, d = (1.0 - m->max_y) * (1.0 - m->max_x);
            f12 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        i26 = get_ao_brightness(m->b_xz_np, m->b_xyz_npp, m->b_yz_pp, b20);
        i27 = get_ao_brightness(m->b_yz_pp, m->b_xz_pp, m->b_xyz_ppp, b20);
        i28 = get_ao_brightness(m->b_yz_np, m->b_xyz_pnp, m->b_xz_pp, b20);
        i29 = get_ao_brightness(m->b_xyz_nnp, m->b_xz_np, m->b_yz_np, b20);
        {
            double a = m->max_y * (1.0 - m->min_x), b = (1.0 - m->max_y) * (1.0 - m->min_x);
            double c = (1.0 - m->max_y) * m->min_x, d = m->max_y * m->min_x;
            m->bright_tl = mix_ao_brightness(i26, i29, i28, i27, a, b, c, d);
        }
        {
            double a = m->min_y * (1.0 - m->min_x), b = (1.0 - m->min_y) * (1.0 - m->min_x);
            double c = (1.0 - m->min_y) * m->min_x, d = m->min_y * m->min_x;
            m->bright_bl = mix_ao_brightness(i26, i29, i28, i27, a, b, c, d);
        }
        {
            double a = m->min_y * (1.0 - m->max_x), b = (1.0 - m->min_y) * (1.0 - m->max_x);
            double c = (1.0 - m->min_y) * m->max_x, d = m->min_y * m->max_x;
            m->bright_br = mix_ao_brightness(i26, i29, i28, i27, a, b, c, d);
        }
        {
            double a = m->max_y * (1.0 - m->max_x), b = (1.0 - m->max_y) * (1.0 - m->max_x);
            double c = (1.0 - m->max_y) * m->max_x, d = m->max_y * m->max_x;
            m->bright_tr = mix_ao_brightness(i26, i29, i28, i27, a, b, c, d);
        }

        if (gc) set_face_color(m, p5 * 0.8F, p6 * 0.8F, p7 * 0.8F);
        else set_face_color(m, 0.8F, 0.8F, 0.8F);

        scale_face_colors(m, f9, f10, f11, f12);
        int ico = block_icon_world_index(m, id, x, y, z, 3);
        render_face_z_pos(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            overlay_scale(m, p5, p6, p7);
            render_face_z_pos(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x - 1, y, z, 4))
    {
        if (m->min_x <= 0.0) --x;

        m->l_xy_nn = AL(x, y - 1, z);
        m->l_xz_nn = AL(x, y, z - 1);
        m->l_xz_np = AL(x, y, z + 1);
        m->l_xy_np = AL(x, y + 1, z);
        m->b_xy_nn = BL(x, y - 1, z);
        m->b_xz_nn = BL(x, y, z - 1);
        m->b_xz_np = BL(x, y, z + 1);
        m->b_xy_np = BL(x, y + 1, z);
        int v16 = CG(x - 1, y + 1, z);
        int v17 = CG(x - 1, y - 1, z);
        int v18 = CG(x - 1, y, z - 1);
        int v19 = CG(x - 1, y, z + 1);

        if (!v18 && !v17) { m->l_xyz_nnn = m->l_xz_nn; m->b_xyz_nnn = m->b_xz_nn; }
        else { m->l_xyz_nnn = AL(x, y - 1, z - 1); m->b_xyz_nnn = BL(x, y - 1, z - 1); }

        if (!v19 && !v17) { m->l_xyz_nnp = m->l_xz_np; m->b_xyz_nnp = m->b_xz_np; }
        else { m->l_xyz_nnp = AL(x, y - 1, z + 1); m->b_xyz_nnp = BL(x, y - 1, z + 1); }

        if (!v18 && !v16) { m->l_xyz_npn = m->l_xz_nn; m->b_xyz_npn = m->b_xz_nn; }
        else { m->l_xyz_npn = AL(x, y + 1, z - 1); m->b_xyz_npn = BL(x, y + 1, z - 1); }

        if (!v19 && !v16) { m->l_xyz_npp = m->l_xz_np; m->b_xyz_npp = m->b_xz_np; }
        else { m->l_xyz_npp = AL(x, y + 1, z + 1); m->b_xyz_npp = BL(x, y + 1, z + 1); }

        if (m->min_x <= 0.0) ++x;

        b20 = base;
        if (m->min_x <= 0.0 || !IOC(x - 1, y, z)) b20 = BL(x - 1, y, z);

        f21 = AL(x - 1, y, z);
        f22 = (m->l_xy_nn + m->l_xyz_nnp + f21 + m->l_xz_np) / 4.0F;
        f23 = (f21 + m->l_xz_np + m->l_xy_np + m->l_xyz_npp) / 4.0F;
        f24 = (m->l_xz_nn + f21 + m->l_xyz_npn + m->l_xy_np) / 4.0F;
        f25 = (m->l_xyz_nnn + m->l_xy_nn + m->l_xz_nn + f21) / 4.0F;
        {
            double a = m->max_y * m->max_z, b = m->max_y * (1.0 - m->max_z);
            double c = (1.0 - m->max_y) * (1.0 - m->max_z), d = (1.0 - m->max_y) * m->max_z;
            f9 = (float)((double)f23 * a + (double)f24 * b + (double)f25 * c + (double)f22 * d);
        }
        {
            double a = m->max_y * m->min_z, b = m->max_y * (1.0 - m->min_z);
            double c = (1.0 - m->max_y) * (1.0 - m->min_z), d = (1.0 - m->max_y) * m->min_z;
            f10 = (float)((double)f23 * a + (double)f24 * b + (double)f25 * c + (double)f22 * d);
        }
        {
            double a = m->min_y * m->min_z, b = m->min_y * (1.0 - m->min_z);
            double c = (1.0 - m->min_y) * (1.0 - m->min_z), d = (1.0 - m->min_y) * m->min_z;
            f11 = (float)((double)f23 * a + (double)f24 * b + (double)f25 * c + (double)f22 * d);
        }
        {
            double a = m->min_y * m->max_z, b = m->min_y * (1.0 - m->max_z);
            double c = (1.0 - m->min_y) * (1.0 - m->max_z), d = (1.0 - m->min_y) * m->max_z;
            f12 = (float)((double)f23 * a + (double)f24 * b + (double)f25 * c + (double)f22 * d);
        }
        i26 = get_ao_brightness(m->b_xy_nn, m->b_xyz_nnp, m->b_xz_np, b20);
        i27 = get_ao_brightness(m->b_xz_np, m->b_xy_np, m->b_xyz_npp, b20);
        i28 = get_ao_brightness(m->b_xz_nn, m->b_xyz_npn, m->b_xy_np, b20);
        i29 = get_ao_brightness(m->b_xyz_nnn, m->b_xy_nn, m->b_xz_nn, b20);
        {
            double a = m->max_y * m->max_z, b = m->max_y * (1.0 - m->max_z);
            double c = (1.0 - m->max_y) * (1.0 - m->max_z), d = (1.0 - m->max_y) * m->max_z;
            m->bright_tl = mix_ao_brightness(i27, i28, i29, i26, a, b, c, d);
        }
        {
            double a = m->max_y * m->min_z, b = m->max_y * (1.0 - m->min_z);
            double c = (1.0 - m->max_y) * (1.0 - m->min_z), d = (1.0 - m->max_y) * m->min_z;
            m->bright_bl = mix_ao_brightness(i27, i28, i29, i26, a, b, c, d);
        }
        {
            double a = m->min_y * m->min_z, b = m->min_y * (1.0 - m->min_z);
            double c = (1.0 - m->min_y) * (1.0 - m->min_z), d = (1.0 - m->min_y) * m->min_z;
            m->bright_br = mix_ao_brightness(i27, i28, i29, i26, a, b, c, d);
        }
        {
            double a = m->min_y * m->max_z, b = m->min_y * (1.0 - m->max_z);
            double c = (1.0 - m->min_y) * (1.0 - m->max_z), d = (1.0 - m->min_y) * m->max_z;
            m->bright_tr = mix_ao_brightness(i27, i28, i29, i26, a, b, c, d);
        }

        if (gc) set_face_color(m, p5 * 0.6F, p6 * 0.6F, p7 * 0.6F);
        else set_face_color(m, 0.6F, 0.6F, 0.6F);

        scale_face_colors(m, f9, f10, f11, f12);
        int ico = block_icon_world_index(m, id, x, y, z, 4);
        render_face_x_neg(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            overlay_scale(m, p5, p6, p7);
            render_face_x_neg(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x + 1, y, z, 5))
    {
        if (m->max_x >= 1.0) ++x;

        m->l_xy_pn = AL(x, y - 1, z);
        m->l_xz_pn = AL(x, y, z - 1);
        m->l_xz_pp = AL(x, y, z + 1);
        m->l_xy_pp = AL(x, y + 1, z);
        m->b_xy_pn = BL(x, y - 1, z);
        m->b_xz_pn = BL(x, y, z - 1);
        m->b_xz_pp = BL(x, y, z + 1);
        m->b_xy_pp = BL(x, y + 1, z);
        int v16 = CG(x + 1, y + 1, z);
        int v17 = CG(x + 1, y - 1, z);
        int v18 = CG(x + 1, y, z + 1);
        int v19 = CG(x + 1, y, z - 1);

        if (!v17 && !v19) { m->l_xyz_pnn = m->l_xz_pn; m->b_xyz_pnn = m->b_xz_pn; }
        else { m->l_xyz_pnn = AL(x, y - 1, z - 1); m->b_xyz_pnn = BL(x, y - 1, z - 1); }

        if (!v17 && !v18) { m->l_xyz_pnp = m->l_xz_pp; m->b_xyz_pnp = m->b_xz_pp; }
        else { m->l_xyz_pnp = AL(x, y - 1, z + 1); m->b_xyz_pnp = BL(x, y - 1, z + 1); }

        if (!v16 && !v19) { m->l_xyz_ppn = m->l_xz_pn; m->b_xyz_ppn = m->b_xz_pn; }
        else { m->l_xyz_ppn = AL(x, y + 1, z - 1); m->b_xyz_ppn = BL(x, y + 1, z - 1); }

        if (!v16 && !v18) { m->l_xyz_ppp = m->l_xz_pp; m->b_xyz_ppp = m->b_xz_pp; }
        else { m->l_xyz_ppp = AL(x, y + 1, z + 1); m->b_xyz_ppp = BL(x, y + 1, z + 1); }

        if (m->max_x >= 1.0) --x;

        b20 = base;
        if (m->max_x >= 1.0 || !IOC(x + 1, y, z)) b20 = BL(x + 1, y, z);

        f21 = AL(x + 1, y, z);
        f22 = (m->l_xy_pn + m->l_xyz_pnp + f21 + m->l_xz_pp) / 4.0F;
        f23 = (m->l_xyz_pnn + m->l_xy_pn + m->l_xz_pn + f21) / 4.0F;
        f24 = (m->l_xz_pn + f21 + m->l_xyz_ppn + m->l_xy_pp) / 4.0F;
        f25 = (f21 + m->l_xz_pp + m->l_xy_pp + m->l_xyz_ppp) / 4.0F;
        {
            double a = (1.0 - m->min_y) * m->max_z, b = (1.0 - m->min_y) * (1.0 - m->max_z);
            double c = m->min_y * (1.0 - m->max_z), d = m->min_y * m->max_z;
            f9 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        {
            double a = (1.0 - m->min_y) * m->min_z, b = (1.0 - m->min_y) * (1.0 - m->min_z);
            double c = m->min_y * (1.0 - m->min_z), d = m->min_y * m->min_z;
            f10 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        {
            double a = (1.0 - m->max_y) * m->min_z, b = (1.0 - m->max_y) * (1.0 - m->min_z);
            double c = m->max_y * (1.0 - m->min_z), d = m->max_y * m->min_z;
            f11 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        {
            double a = (1.0 - m->max_y) * m->max_z, b = (1.0 - m->max_y) * (1.0 - m->max_z);
            double c = m->max_y * (1.0 - m->max_z), d = m->max_y * m->max_z;
            f12 = (float)((double)f22 * a + (double)f23 * b + (double)f24 * c + (double)f25 * d);
        }
        i26 = get_ao_brightness(m->b_xy_pn, m->b_xyz_pnp, m->b_xz_pp, b20);
        i27 = get_ao_brightness(m->b_xz_pp, m->b_xy_pp, m->b_xyz_ppp, b20);
        i28 = get_ao_brightness(m->b_xz_pn, m->b_xyz_ppn, m->b_xy_pp, b20);
        i29 = get_ao_brightness(m->b_xyz_pnn, m->b_xy_pn, m->b_xz_pn, b20);
        m->bright_tl = mix_ao_brightness(i26, i29, i28, i27, (1.0 - m->min_y) * m->max_z,
            (1.0 - m->min_y) * (1.0 - m->max_z), m->min_y * (1.0 - m->max_z), m->min_y * m->max_z);
        m->bright_bl = mix_ao_brightness(i26, i29, i28, i27, (1.0 - m->min_y) * m->min_z,
            (1.0 - m->min_y) * (1.0 - m->min_z), m->min_y * (1.0 - m->min_z), m->min_y * m->min_z);
        m->bright_br = mix_ao_brightness(i26, i29, i28, i27, (1.0 - m->max_y) * m->min_z,
            (1.0 - m->max_y) * (1.0 - m->min_z), m->max_y * (1.0 - m->min_z), m->max_y * m->min_z);
        m->bright_tr = mix_ao_brightness(i26, i29, i28, i27, (1.0 - m->max_y) * m->max_z,
            (1.0 - m->max_y) * (1.0 - m->max_z), m->max_y * (1.0 - m->max_z), m->max_y * m->max_z);

        if (gc) set_face_color(m, p5 * 0.6F, p6 * 0.6F, p7 * 0.6F);
        else set_face_color(m, 0.6F, 0.6F, 0.6F);

        scale_face_colors(m, f9, f10, f11, f12);
        int ico = block_icon_world_index(m, id, x, y, z, 5);
        render_face_x_pos(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            overlay_scale(m, p5, p6, p7);
            render_face_x_pos(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    m->enable_ao = 0;
    return drawn;
}

#undef BL
#undef AL
#undef CG
#undef IOC

/** RenderBlocks.renderStandardBlockWithColorMultiplier */
int render_standard_block_color(struct rb_mesher *m, int id, int x, int y, int z,
                                       float p5, float p6, float p7)
{
    struct rb_tess *t = m->t;
    m->enable_ao = 0;
    int drawn = 0;
    float v10 = 0.5F, v11 = 1.0F, v12 = 0.8F, v13 = 0.6F;
    float v14 = v11 * p5, v15 = v11 * p6, v16 = v11 * p7;
    float v17 = v10, v18 = v12, v19 = v13;
    float v20 = v10, v21 = v12, v22 = v13;
    float v23 = v10, v24 = v12, v25 = v13;

    int is_grass = id == 2 && strcmp(BLOCKS[2].class_name, "BlockGrass") == 0;

    if (!is_grass)
    {
        v17 = v10 * p5; v18 = v12 * p5; v19 = v13 * p5;
        v20 = v10 * p6; v21 = v12 * p6; v22 = v13 * p6;
        v23 = v10 * p7; v24 = v12 * p7; v25 = v13 * p7;
    }

    int base = block_brightness(m, id, x, y, z);

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y - 1, z, 0))
    {
        tess_set_brightness(t, m->min_y > 0.0 ? base : block_brightness(m, id, x, y - 1, z));
        tess_set_color_opaque_f(t, v17, v20, v23);
        render_face_y_neg(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 0));
        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y + 1, z, 1))
    {
        tess_set_brightness(t, m->max_y < 1.0 ? base : block_brightness(m, id, x, y + 1, z));
        tess_set_color_opaque_f(t, v14, v15, v16);
        render_face_y_pos(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 1));
        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y, z - 1, 2))
    {
        tess_set_brightness(t, m->min_z > 0.0 ? base : block_brightness(m, id, x, y, z - 1));
        tess_set_color_opaque_f(t, v18, v21, v24);
        int ico = block_icon_world_index(m, id, x, y, z, 2);
        render_face_z_neg(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            tess_set_color_opaque_f(t, v18 * p5, v21 * p6, v24 * p7);
            render_face_z_neg(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y, z + 1, 3))
    {
        tess_set_brightness(t, m->max_z < 1.0 ? base : block_brightness(m, id, x, y, z + 1));
        tess_set_color_opaque_f(t, v18, v21, v24);
        int ico = block_icon_world_index(m, id, x, y, z, 3);
        render_face_z_pos(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            tess_set_color_opaque_f(t, v18 * p5, v21 * p6, v24 * p7);
            render_face_z_pos(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x - 1, y, z, 4))
    {
        tess_set_brightness(t, m->min_x > 0.0 ? base : block_brightness(m, id, x - 1, y, z));
        tess_set_color_opaque_f(t, v19, v22, v25);
        int ico = block_icon_world_index(m, id, x, y, z, 4);
        render_face_x_neg(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            tess_set_color_opaque_f(t, v19 * p5, v22 * p6, v25 * p7);
            render_face_x_neg(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x + 1, y, z, 5))
    {
        tess_set_brightness(t, m->max_x < 1.0 ? base : block_brightness(m, id, x + 1, y, z));
        tess_set_color_opaque_f(t, v19, v22, v25);
        int ico = block_icon_world_index(m, id, x, y, z, 5);
        render_face_x_pos(m, (double)x, (double)y, (double)z, uv_of(m, ico));

        if (icon_code_is(m, ico, "grass_side"))
        {
            tess_set_color_opaque_f(t, v19 * p5, v22 * p6, v25 * p7);
            render_face_x_pos(m, (double)x, (double)y, (double)z, &m->special[RB_ICON_GRASS_OVERLAY]);
        }

        drawn = 1;
    }

    return drawn;
}

/** RenderBlocks.renderStandardBlock: the AO and colour paths the oracle picks. */
int render_standard_block(struct rb_mesher *m, int id, int x, int y, int z)
{
    int32_t c = color_multiplier(m, id, x, y, z);
    float r = (float)((c >> 16) & 255) / 255.0F;
    float g = (float)((c >> 8) & 255) / 255.0F;
    float b = (float)(c & 255) / 255.0F;

    if (m->tab->ao != 0 && m->tab->props[id].light == 0)
    {
        return m->partial ? render_standard_block_ao_partial(m, id, x, y, z, r, g, b)
                          : render_standard_block_ao(m, id, x, y, z, r, g, b);
    }

    return render_standard_block_color(m, id, x, y, z, r, g, b);
}
/** RenderBlocks.drawCrossedSquares */
void draw_crossed_squares(struct rb_mesher *m, const struct rb_uv *ic,
                                 double x, double y, double z, float f)
{
    struct rb_tess *t = m->t;
    double u0 = (double)ic->min_u, v0 = (double)ic->min_v;
    double u1 = (double)ic->max_u, v1 = (double)ic->max_v;
    double d = 0.45 * (double)f;
    double x0 = x + 0.5 - d, x1 = x + 0.5 + d;
    double z0 = z + 0.5 - d, z1 = z + 0.5 + d;

    tess_add_vertex_with_uv(t, x0, y + (double)f, z0, u0, v0);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z0, u0, v1);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z1, u1, v1);
    tess_add_vertex_with_uv(t, x1, y + (double)f, z1, u1, v0);
    tess_add_vertex_with_uv(t, x1, y + (double)f, z1, u0, v0);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z1, u0, v1);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z0, u1, v1);
    tess_add_vertex_with_uv(t, x0, y + (double)f, z0, u1, v0);
    tess_add_vertex_with_uv(t, x0, y + (double)f, z1, u0, v0);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z1, u0, v1);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z0, u1, v1);
    tess_add_vertex_with_uv(t, x1, y + (double)f, z0, u1, v0);
    tess_add_vertex_with_uv(t, x1, y + (double)f, z0, u0, v0);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z0, u0, v1);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z1, u1, v1);
    tess_add_vertex_with_uv(t, x0, y + (double)f, z1, u1, v0);
}

/** RenderBlocks.renderCrossedSquares */
static int render_crossed_squares(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int32_t c = color_multiplier(m, id, x, y, z);
    float r = (float)((c >> 16) & 255) / 255.0F;
    float g = (float)((c >> 8) & 255) / 255.0F;
    float b = (float)(c & 255) / 255.0F;
    tess_set_color_opaque_f(t, r, g, b);

    double dx = (double)x, dy = (double)y, dz = (double)z;
    int meta = rb_world_meta(m->w, x, y, z);
    int rt = BLOCKS[id].render_type;

    if (rt == 1 && strcmp(BLOCKS[id].class_name, "BlockTallGrass") == 0)
    {
        uint64_t h = (uint64_t)(int64_t)(int32_t)(x * 3129871) ^ ((uint64_t)(int64_t)z * 116129781ULL) ^ (uint64_t)(int64_t)y;
        h = h * h * 42317861ULL + h * 11ULL;
        dx += ((double)((float)(h >> 16 & 15ULL) / 15.0F) - 0.5) * 0.5;
        dy += ((double)((float)(h >> 20 & 15ULL) / 15.0F) - 1.0) * 0.2;
        dz += ((double)((float)(h >> 24 & 15ULL) / 15.0F) - 0.5) * 0.5;
    }
    else if (strcmp(BLOCKS[id].class_name, "BlockFlower") == 0)
    {
        uint64_t h = (uint64_t)(int64_t)(int32_t)(x * 3129871) ^ ((uint64_t)(int64_t)z * 116129781ULL) ^ (uint64_t)(int64_t)y;
        h = h * h * 42317861ULL + h * 11ULL;
        dx += ((double)((float)(h >> 16 & 15ULL) / 15.0F) - 0.5) * 0.3;
        dz += ((double)((float)(h >> 24 & 15ULL) / 15.0F) - 0.5) * 0.3;
    }

    draw_crossed_squares(m, icon_from_side_meta(m, id, 0, meta), dx, dy, dz, 1.0F);
    (void)rt;
    return 1;
}

/** BlockLiquid.func_149798_e: this material's meta here, or -1. */
static int liquid_meta(struct rb_mesher *m, int x, int y, int z, int mat)
{
    if (BLOCKS[rb_world_block(m->w, x, y, z)].material != mat) return -1;
    int meta = rb_world_meta(m->w, x, y, z);
    return meta >= 8 ? 0 : meta;
}

/** BlockLiquid.isBlockSolid */
static int liquid_is_block_solid(struct rb_mesher *m, int x, int y, int z, int side, int mat)
{
    int nmat = BLOCKS[rb_world_block(m->w, x, y, z)].material;
    if (nmat == mat) return 0;
    if (side == 1) return 1;
    if (strcmp(MATERIALS[nmat].name, "ice") == 0) return 0;
    return MATERIALS[nmat].is_solid;
}

/** MathHelper.sin: a 65536 entry table lookup, not libm. */
float mh_sin(const struct rb_mesher *m, float f)
{
    return m->tab->sin_table[(int)(f * 10430.378F) & 65535];
}

/** MathHelper.cos: the same table, offset half a turn. */
float mh_cos(const struct rb_mesher *m, float f)
{
    return m->tab->sin_table[(int)(f * 10430.378F + 16384.0F) & 65535];
}

#if !defined(__NVPTX__)
/* libm as the mesher calls it (render_blocks_int.h): glibc's own */
double rb_cos(const struct rb_mesher *m, double x) { (void)m; return cos(x); }
double rb_sin(const struct rb_mesher *m, double x) { (void)m; return sin(x); }
double rb_atan2(const struct rb_mesher *m, double y, double x) { (void)m; return atan2(y, x); }
#endif

/** Vec3.normalize */
static void vec_normalize(double *x, double *y, double *z)
{
    double len = sqrt(*x * *x + *y * *y + *z * *z);

    if (len < 1.0E-4)
    {
        *x = *y = *z = 0.0;
        return;
    }

    *x /= len;
    *y /= len;
    *z /= len;
}

/* the flow vector's tail of BlockLiquid.func_149800_f (its y is 0 until
 * the solid neighbours push it down): 0 when it is (0, 0) in x and z */
static int liquid_flow_norm(double *vx, double *vz, int solid)
{
    double vy = 0.0;

    if (solid)
    {
        vec_normalize(vx, &vy, vz);
        vy -= 6.0;
    }

    vec_normalize(vx, &vy, vz);
    return !(*vx == 0.0 && *vz == 0.0);
}

/** BlockLiquid.func_149800_f / func_149802_a: the flow angle. */
float liquid_angle(struct rb_mesher *m, int x, int y, int z, int mat)
{
    int meta = rb_world_meta(m->w, x, y, z);
    int me = liquid_meta(m, x, y, z, mat);
    double vx = 0.0, vz = 0.0;

    for (int i = 0; i < 4; ++i)
    {
        int ax = x, az = z;
        if (i == 0) ax = x - 1;
        if (i == 1) az = z - 1;
        if (i == 2) ++ax;
        if (i == 3) ++az;
        int v = liquid_meta(m, ax, y, az, mat);

        if (v < 0)
        {
            if (!MATERIALS[BLOCKS[rb_world_block(m->w, ax, y, az)].material].blocks_movement)
            {
                v = liquid_meta(m, ax, y - 1, az, mat);

                if (v >= 0)
                {
                    double d = v - (me - 8);
                    vx += (double)(ax - x) * d;
                    vz += (double)(az - z) * d;
                }
            }
        }
        else
        {
            double d = v - me;
            vx += (double)(ax - x) * d;
            vz += (double)(az - z) * d;
        }
    }

    int solid = 0;

    if (meta >= 8)
    {
        if (liquid_is_block_solid(m, x, y, z - 1, 2, mat)) solid = 1;
        if (!solid && liquid_is_block_solid(m, x, y, z + 1, 3, mat)) solid = 1;
        if (!solid && liquid_is_block_solid(m, x - 1, y, z, 4, mat)) solid = 1;
        if (!solid && liquid_is_block_solid(m, x + 1, y, z, 5, mat)) solid = 1;
        if (!solid && liquid_is_block_solid(m, x, y + 1, z - 1, 2, mat)) solid = 1;
        if (!solid && liquid_is_block_solid(m, x, y + 1, z + 1, 3, mat)) solid = 1;
        if (!solid && liquid_is_block_solid(m, x - 1, y + 1, z, 4, mat)) solid = 1;
        if (!solid && liquid_is_block_solid(m, x + 1, y + 1, z, 5, mat)) solid = 1;
    }

    if (!liquid_flow_norm(&vx, &vz, solid)) return -1000.0F;
    return (float)(rb_atan2(m, vz, vx) - (3.14159265358979323846 / 2.0));
}

/* rb_trig_liquid: atan2's arguments for the flow sums (vx, vz), each a sum
 * of two neighbours' level differences (-15..15) */
int rb_trig_liquid(int vx, int vz, int solid, struct rb_trig *out)
{
    double x = (double)vx, z = (double)vz;
    if (!liquid_flow_norm(&x, &z, solid)) return 0;
    out->fn = RB_TRIG_ATAN2;
    out->a = z;
    out->b = x;
    out->v = atan2(z, x);
    return 1;
}

/** BlockLiquid.func_149801_b */
static float liquid_height_percent(int meta)
{
    if (meta >= 8) meta = 0;
    return (float)(meta + 1) / 9.0F;
}

/** RenderBlocks.getFluidHeight */
float fluid_height(struct rb_mesher *m, int x, int y, int z, int mat)
{
    int count = 0;
    float h = 0.0F;

    for (int i = 0; i < 4; ++i)
    {
        int ax = x - (i & 1);
        int az = z - ((i >> 1) & 1);

        if (BLOCKS[rb_world_block(m->w, ax, y + 1, az)].material == mat) return 1.0F;
        int nmat = BLOCKS[rb_world_block(m->w, ax, y, az)].material;

        if (nmat == mat)
        {
            int meta = rb_world_meta(m->w, ax, y, az);

            if (meta >= 8 || meta == 0)
            {
                h += liquid_height_percent(meta) * 10.0F;
                count += 10;
            }

            h += liquid_height_percent(meta);
            ++count;
        }
        else if (!MATERIALS[nmat].is_solid)
        {
            h += 1.0F;
            ++count;
        }
    }

    return 1.0F - h / (float)count;
}

/** RenderBlocks.renderBlockFluids */
static int render_block_fluids(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int32_t c = color_multiplier(m, id, x, y, z);
    float r = (float)((c >> 16) & 255) / 255.0F;
    float g = (float)((c >> 8) & 255) / 255.0F;
    float b = (float)(c & 255) / 255.0F;
    int mat = BLOCKS[id].material;
    int top = should_side_be_rendered(m, id, x, y + 1, z, 1);
    int bottom = should_side_be_rendered(m, id, x, y - 1, z, 0);
    int side[4];
    side[0] = should_side_be_rendered(m, id, x, y, z - 1, 2);
    side[1] = should_side_be_rendered(m, id, x, y, z + 1, 3);
    side[2] = should_side_be_rendered(m, id, x - 1, y, z, 4);
    side[3] = should_side_be_rendered(m, id, x + 1, y, z, 5);

    if (!top && !bottom && !side[0] && !side[1] && !side[2] && !side[3]) return 0;

    int drawn = 0;
    float f14 = 0.5F, f15 = 1.0F, f16 = 0.8F, f17 = 0.6F;
    double d18 = 0.0, d20 = 1.0;
    int meta = rb_world_meta(m->w, x, y, z);
    double h24 = (double)fluid_height(m, x, y, z, mat);
    double h26 = (double)fluid_height(m, x, y, z + 1, mat);
    double h28 = (double)fluid_height(m, x + 1, y, z + 1, mat);
    double h30 = (double)fluid_height(m, x + 1, y, z, mat);
    double d32 = 0.0010000000474974513;

    if (m->render_all_faces || top)
    {
        drawn = 1;
        const struct rb_uv *ic = icon_from_side_meta(m, id, 1, meta);
        float angle = liquid_angle(m, x, y, z, mat);

        if ((double)angle > -999.0) ic = icon_from_side_meta(m, id, 2, meta);

        h24 -= d32;
        h26 -= d32;
        h28 -= d32;
        h30 -= d32;

        double u0, v0, u1, v1, u2, v2, u3, v3;

        if ((double)angle < -999.0)
        {
            u0 = (double)interp_u(ic, 0.0);
            v0 = (double)interp_v(ic, 0.0);
            u1 = u0;
            v1 = (double)interp_v(ic, 16.0);
            u2 = (double)interp_u(ic, 16.0);
            v2 = v1;
            u3 = u2;
            v3 = v0;
        }
        else
        {
            float s = mh_sin(m, angle) * 0.25F;
            float co = mh_cos(m, angle) * 0.25F;
            u0 = (double)interp_u(ic, (double)(8.0F + (-co - s) * 16.0F));
            v0 = (double)interp_v(ic, (double)(8.0F + (-co + s) * 16.0F));
            u1 = (double)interp_u(ic, (double)(8.0F + (-co + s) * 16.0F));
            v1 = (double)interp_v(ic, (double)(8.0F + (co + s) * 16.0F));
            u2 = (double)interp_u(ic, (double)(8.0F + (co + s) * 16.0F));
            v2 = (double)interp_v(ic, (double)(8.0F + (co - s) * 16.0F));
            u3 = (double)interp_u(ic, (double)(8.0F + (co - s) * 16.0F));
            v3 = (double)interp_v(ic, (double)(8.0F + (-co - s) * 16.0F));
        }

        tess_set_brightness(t, block_brightness(m, id, x, y, z));
        tess_set_color_opaque_f(t, f15 * r, f15 * g, f15 * b);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)y + h24, (double)(z + 0), u0, v0);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)y + h26, (double)(z + 1), u1, v1);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + h28, (double)(z + 1), u2, v2);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + h30, (double)(z + 0), u3, v3);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)y + h24, (double)(z + 0), u0, v0);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + h30, (double)(z + 0), u3, v3);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + h28, (double)(z + 1), u2, v2);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)y + h26, (double)(z + 1), u1, v1);
    }

    if (m->render_all_faces || bottom)
    {
        tess_set_brightness(t, block_brightness(m, id, x, y - 1, z));
        tess_set_color_opaque_f(t, f14, f14, f14);
        render_face_y_neg(m, (double)x, (double)y + d32, (double)z, icon_from_side(m, id, 0));
        drawn = 1;
    }

    for (int i = 0; i < 4; ++i)
    {
        int ax = x, az = z;

        if (i == 0) az = z - 1;
        if (i == 1) ++az;
        if (i == 2) ax = x - 1;
        if (i == 3) ++ax;

        const struct rb_uv *ic = icon_from_side_meta(m, id, i + 2, meta);

        if (m->render_all_faces || side[i])
        {
            double e, f, g2, h2, i2, j2;

            if (i == 0) { e = h24; f = h30; g2 = (double)x; i2 = (double)(x + 1); h2 = (double)z + d32; j2 = (double)z + d32; }
            else if (i == 1) { e = h28; f = h26; g2 = (double)(x + 1); i2 = (double)x; h2 = (double)(z + 1) - d32; j2 = (double)(z + 1) - d32; }
            else if (i == 2) { e = h26; f = h24; g2 = (double)x + d32; i2 = (double)x + d32; h2 = (double)(z + 1); j2 = (double)z; }
            else { e = h30; f = h28; g2 = (double)(x + 1) - d32; i2 = (double)(x + 1) - d32; h2 = (double)z; j2 = (double)(z + 1); }

            drawn = 1;
            float u0 = interp_u(ic, 0.0);
            float u1 = interp_u(ic, 8.0);
            float v0 = interp_v(ic, (1.0 - e) * 16.0 * 0.5);
            float v1 = interp_v(ic, (1.0 - f) * 16.0 * 0.5);
            float v2 = interp_v(ic, 8.0);
            tess_set_brightness(t, block_brightness(m, id, ax, y, az));
            float f56 = 1.0F;
            f56 *= i < 2 ? f16 : f17;
            tess_set_color_opaque_f(t, f15 * f56 * r, f15 * f56 * g, f15 * f56 * b);
            tess_add_vertex_with_uv(t, g2, (double)y + e, h2, (double)u0, (double)v0);
            tess_add_vertex_with_uv(t, i2, (double)y + f, j2, (double)u1, (double)v1);
            tess_add_vertex_with_uv(t, i2, (double)(y + 0), j2, (double)u1, (double)v2);
            tess_add_vertex_with_uv(t, g2, (double)(y + 0), h2, (double)u0, (double)v2);
            tess_add_vertex_with_uv(t, g2, (double)(y + 0), h2, (double)u0, (double)v2);
            tess_add_vertex_with_uv(t, i2, (double)(y + 0), j2, (double)u1, (double)v2);
            tess_add_vertex_with_uv(t, i2, (double)y + f, j2, (double)u1, (double)v1);
            tess_add_vertex_with_uv(t, g2, (double)y + e, h2, (double)u0, (double)v0);
        }
    }

    m->min_y = d18;
    m->max_y = d20;
    return drawn;
}

/** RenderBlocks.renderBlockLog */
static int render_block_log(struct rb_mesher *m, int id, int x, int y, int z)
{
    int meta = rb_world_meta(m->w, x, y, z);
    int axis = meta & 12;

    if (axis == 4)
    {
        m->uv_east = m->uv_west = m->uv_top = m->uv_bottom = 1;
    }
    else if (axis == 8)
    {
        m->uv_south = m->uv_north = 1;
    }

    int r = render_standard_block(m, id, x, y, z);
    m->uv_south = m->uv_east = m->uv_west = m->uv_north = m->uv_top = m->uv_bottom = 0;
    return r;
}

/* renderBlockDoublePlant's position hash */
static uint64_t double_plant_hash(int x, int z)
{
    uint64_t h = (uint64_t)(int64_t)(int32_t)(x * 3129871) ^ ((uint64_t)(int64_t)z * 116129781ULL);
    return h * h * 42317861ULL + h * 11ULL;
}

/* rb_trig_double_plant: the sunflower head's three libm calls at column (x, z) */
int rb_trig_double_plant(int x, int z, struct rb_trig out[3])
{
    double a0 = (double)double_plant_hash(x, z) * 0.8;
    double c0 = cos(a0);
    double a = c0 * 3.14159265358979323846 * 0.1;
    out[0] = (struct rb_trig){.fn = RB_TRIG_COS, .a = a0, .v = c0};
    out[1] = (struct rb_trig){.fn = RB_TRIG_COS, .a = a, .v = cos(a)};
    out[2] = (struct rb_trig){.fn = RB_TRIG_SIN, .a = a, .v = sin(a)};
    return 3;
}

/** RenderBlocks.renderBlockDoublePlant */
static int render_block_double_plant(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int32_t c = color_multiplier(m, id, x, y, z);
    float r = (float)((c >> 16) & 255) / 255.0F;
    float g = (float)((c >> 8) & 255) / 255.0F;
    float b = (float)(c & 255) / 255.0F;
    tess_set_color_opaque_f(t, r, g, b);

    uint64_t h = double_plant_hash(x, z);
    double dx = (double)x, dy = (double)y, dz = (double)z;
    dx += ((double)((float)(h >> 16 & 15ULL) / 15.0F) - 0.5) * 0.3;
    dz += ((double)((float)(h >> 24 & 15ULL) / 15.0F) - 0.5) * 0.3;
    int meta = rb_world_meta(m->w, x, y, z);
    int upper = (meta & 8) != 0;
    int variant;

    if (upper)
    {
        if (rb_world_block(m->w, x, y - 1, z) != id) return 0;
        variant = rb_world_meta(m->w, x, y - 1, z) & 7;
    }
    else
    {
        variant = meta & 7;
    }

    const struct rb_uv *ic = upper ? &m->special[RB_ICON_DP_HIGH + variant] : &m->special[RB_ICON_DP_LOW + variant];
    draw_crossed_squares(m, ic, dx, dy, dz, 1.0F);

    if (upper && variant == 0)
    {
        const struct rb_uv *f0 = &m->special[RB_ICON_DP_FLOWER];
        double a = rb_cos(m, (double)h * 0.8) * 3.14159265358979323846 * 0.1;
        double ca = rb_cos(m, a);
        double sa = rb_sin(m, a);
        double u0 = (double)f0->min_u, v0 = (double)f0->min_v;
        double u1 = (double)f0->max_u, v1 = (double)f0->max_v;
        double p41 = 0.5 + 0.3 * ca - 0.5 * sa;
        double p43 = 0.5 + 0.5 * ca + 0.3 * sa;
        double p45 = 0.5 + 0.3 * ca + 0.5 * sa;
        double p47 = 0.5 + -0.5 * ca + 0.3 * sa;
        double p49 = 0.5 + -0.05 * ca + 0.5 * sa;
        double p51 = 0.5 + -0.5 * ca + -0.05 * sa;
        double p53 = 0.5 + -0.05 * ca - 0.5 * sa;
        double p55 = 0.5 + 0.5 * ca + -0.05 * sa;
        tess_add_vertex_with_uv(t, dx + p49, dy + 1.0, dz + p51, u0, v1);
        tess_add_vertex_with_uv(t, dx + p53, dy + 1.0, dz + p55, u1, v1);
        tess_add_vertex_with_uv(t, dx + p41, dy + 0.0, dz + p43, u1, v0);
        tess_add_vertex_with_uv(t, dx + p45, dy + 0.0, dz + p47, u0, v0);
        const struct rb_uv *f1 = &m->special[RB_ICON_DP_FLOWER + 1];
        u0 = (double)f1->min_u;
        v0 = (double)f1->min_v;
        u1 = (double)f1->max_u;
        v1 = (double)f1->max_v;
        tess_add_vertex_with_uv(t, dx + p53, dy + 1.0, dz + p55, u0, v1);
        tess_add_vertex_with_uv(t, dx + p49, dy + 1.0, dz + p51, u1, v1);
        tess_add_vertex_with_uv(t, dx + p45, dy + 0.0, dz + p47, u1, v0);
        tess_add_vertex_with_uv(t, dx + p41, dy + 0.0, dz + p43, u0, v0);
    }

    return 1;
}

/* ------------------------------------------------------------ the dispatch */

/* what render_block sets before its draw: the Tessellator's current block
 * and the block's bounds */
static void render_block_begin(struct rb_mesher *m, int id, int meta, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    t->cur_id = id;
    t->cur_meta = meta;
    t->cur_rt = BLOCKS[id].render_type;
    t->cur_x = x;
    t->cur_y = y;
    t->cur_z = z;
    set_bounds(m, id, meta);
}

static void render_block(struct rb_mesher *m, int id, int meta, int x, int y, int z)
{
    render_block_begin(m, id, meta, x, y, z);

    /* the piston base's dispatch bounds come from its stored bounds, which
     * the renders move (render_blocks3.c) */
    if (id == 33 || id == 29) rb3_piston_dispatch_bounds(m, id, meta);

    switch (BLOCKS[id].render_type)
    {
        case 0: render_standard_block(m, id, x, y, z); break;
        case 1: render_crossed_squares(m, id, x, y, z); break;
        case 2: rb2_render_torch(m, id, x, y, z); break;
        case 3: rb2_render_fire(m, id, x, y, z); break;
        case 4: render_block_fluids(m, id, x, y, z); break;
        case 5: rb2_render_redstone_wire(m, id, x, y, z); break;
        case 6: rb2_render_crops(m, id, x, y, z); break;
        case 7: rb2_render_door(m, id, x, y, z); break;
        case 8: rb2_render_ladder(m, id, x, y, z); break;
        case 9: rb2_render_rail(m, id, x, y, z); break;
        case 10: rb2_render_stairs(m, id, x, y, z); break;
        case 11: rb2_render_fence(m, id, x, y, z); break;
        case 12: rb2_render_lever(m, id, x, y, z); break;
        case 13: rb2_render_cactus(m, id, x, y, z); break;
        case 14: rb3_render_bed(m, id, x, y, z); break;
        case 15: rb3_render_repeater(m, id, x, y, z); break;
        case 16: rb3_render_piston_base(m, id, x, y, z); break;
        case 17: rb3_render_piston_extension(m, id, x, y, z); break;
        case 18: rb3_render_pane(m, id, x, y, z); break;
        case 19: rb3_render_stem(m, id, x, y, z); break;
        case 20: rb3_render_vine(m, id, x, y, z); break;
        case 21: rb3_render_fence_gate(m, id, x, y, z); break;
        case 23: rb3_render_lily_pad(m, id, x, y, z); break;
        case 24: rb3_render_cauldron(m, id, x, y, z); break;
        case 25: rb3_render_brewing_stand(m, id, x, y, z); break;
        case 26: rb3_render_end_portal_frame(m, id, x, y, z); break;
        case 27: rb3_render_dragon_egg(m, id, x, y, z); break;
        case 28: rb3_render_cocoa(m, id, x, y, z); break;
        case 29: rb3_render_tripwire_source(m, id, x, y, z); break;
        case 30: rb3_render_tripwire(m, id, x, y, z); break;
        case 32: rb3_render_wall(m, id, x, y, z); break;
        case 33: rb3_render_flowerpot(m, id, x, y, z); break;
        case 34: rb3_render_beacon(m, id, x, y, z); break;
        case 35: rb3_render_anvil(m, id, x, y, z); break;
        case 37: rb3_render_comparator(m, id, x, y, z); break;
        case 38: rb3_render_hopper(m, id, x, y, z); break;
        case 39: rb3_render_quartz(m, id, x, y, z); break;
        case 41: rb3_render_stained_glass_pane(m, id, x, y, z); break;
        case 31: render_block_log(m, id, x, y, z); break;
        case 40: render_block_double_plant(m, id, x, y, z); break;
        case 22: break;   /* BlockChest: a tile entity renderer draws it, RenderBlocks draws nothing */
        case 255: break;  /* getRenderType() -1: RenderBlocks draws nothing (skulls) */
        default:
            fprintf(stderr, "render_blocks: render type %d (block %d %s) is not ported\n",
                BLOCKS[id].render_type, id, BLOCKS[id].name);
            break;
    }
}

/* A block no face of which can be drawn: a standard opaque cube (render
 * type 0, full bounds) all six of whose neighbours are opaque cubes, so
 * every shouldSideBeRendered is false. render_block would draw nothing;
 * what it leaves behind is its prologue, and the AO path's brightness
 * (renderStandardBlockWithAmbientOcclusion's setBrightness(983055)) and
 * enableAO false. rb_mesh_hidden_fast 0 turns the shortcut off (the mesh
 * equivalence check). */
int rb_mesh_hidden_fast = 1;

static int hidden_cube(struct rb_mesher *m, int id, int meta, int x, int y, int z)
{
    if (BLOCKS[id].render_type != 0 || !m->tab->props[id].opaque_cube || m->render_all_faces) return 0;
    const double *b = m->tab->bounds + ((size_t)id * RB_METAS + (meta & 15)) * 6;
    if (b[0] != 0.0 || b[1] != 0.0 || b[2] != 0.0 || b[3] != 1.0 || b[4] != 1.0 || b[5] != 1.0) return 0;
    return opaque_cube(m, x, y - 1, z) && opaque_cube(m, x, y + 1, z) && opaque_cube(m, x, y, z - 1) &&
           opaque_cube(m, x, y, z + 1) && opaque_cube(m, x - 1, y, z) && opaque_cube(m, x + 1, y, z);
}

static void hidden_cube_state(struct rb_mesher *m, int id, int meta, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    t->cur_id = id;
    t->cur_meta = meta;
    t->cur_rt = 0;
    t->cur_x = x;
    t->cur_y = y;
    t->cur_z = z;
    set_bounds(m, id, meta);
    if (m->tab->ao != 0 && m->tab->props[id].light == 0) tess_set_brightness(t, 983055);
    m->enable_ao = 0;
}

#if defined(__NVPTX__)
/* The device mesher's guess of the state a cell's draw leaves (cuda/
 * meshing kernels.c guess_as), one call whatever the cell: KIND 0 none, 1
 * a hidden cube's (hidden_cube_state), 2 a drawn cell only begun
 * (render_block_begin: for most render types the words the device holds
 * against the lane before's end are the ones this sets), stairs, doors and
 * fences with their last part's bounds besides (render_blocks2.c). A cell
 * whose draw leaves more is caught there and drawn again. */
void rb_mesh_guess_cell(struct rb_mesher *m, int kind, int id, int meta, int x, int y, int z)
{
    if (kind == 1) hidden_cube_state(m, id, meta, x, y, z);
    if (kind != 2) return;
    render_block_begin(m, id, meta, x, y, z);
    int rt = BLOCKS[id].render_type;
    if (rt == 10) rb2_stairs_state(m, id, x, y, z);
    else if (rt == 11) rb2_fence_state(m, id, x, y, z);
    else if (rt == 7) rb2_door_state(m, id, x, y, z);
}
#endif

void rb_mesh_section_begin(struct rb_mesher *m, int cx, int cz, int section)
{
    struct rb_tess *t = m->t;
    rb_tess_start_quads(t);
    rb_tess_set_translation(t, (double)(-(cx << 4)), (double)(-(section << 4)), (double)(-(cz << 4)));
}

int rb_mesh_cell_kind(struct rb_mesher *m, int x, int y, int z, int pass, int *id, int *meta)
{
    int b = rb_world_block(m->w, x, y, z);
    if (block_class_of(b) & SC_AIR) return 0;
    if (m->tab->props[b].render_pass != pass) return 0;

    int mt = rb_world_meta(m->w, x, y, z);
    *id = b;
    *meta = mt;
    if (rb_mesh_hidden_fast && !(x == m->px && y == m->py && z == m->pz) && hidden_cube(m, b, mt, x, y, z))
        return 1;
    return 2;
}

int rb_block_pass(const struct rb_table *tab, int id)
{
    return block_class_of(id) & SC_AIR ? -1 : tab->props[id].render_pass;
}

/* rb_mesh_cell of a hidden cube or a standard block (render type 0, never a
 * piston base): the block the camera is in drawn a second time from inside */
void rb_mesh_cell_standard(struct rb_mesher *m, int kind, int id, int meta, int x, int y, int z)
{
    if (kind == 1)
    {
        hidden_cube_state(m, id, meta, x, y, z);
        return;
    }
    render_block_begin(m, id, meta, x, y, z);
    render_standard_block(m, id, x, y, z);

    if (x == m->px && y == m->py && z == m->pz)
    {
        m->render_from_inside = 1;
        m->render_all_faces = 1;
        render_block_begin(m, id, rb_world_meta(m->w, x, y, z), x, y, z);
        render_standard_block(m, id, x, y, z);
        m->render_from_inside = 0;
        m->render_all_faces = 0;
    }
}

void rb_mesh_cell(struct rb_mesher *m, int kind, int id, int meta, int x, int y, int z)
{
    if (kind == 1 || BLOCKS[id].render_type == 0) rb_mesh_cell_standard(m, kind, id, meta, x, y, z);
    else render_block(m, id, meta, x, y, z);
}

void rb_mesh_section(struct rb_mesher *m, int cx, int cz, int section, int pass)
{
    int base_x = cx << 4, base_z = cz << 4, y0 = section << 4;

    rb_mesh_section_begin(m, cx, cz, section);

    for (int y = y0; y < y0 + 16; ++y)
    {
        for (int z = base_z; z < base_z + 16; ++z)
        {
            for (int x = base_x; x < base_x + 16; ++x)
            {
                int id = 0, meta = 0;
                int kind = rb_mesh_cell_kind(m, x, y, z, pass, &id, &meta);
                if (kind) rb_mesh_cell(m, kind, id, meta, x, y, z);
            }
        }
    }
}

void rb_mesh_override_block(struct rb_mesher *m, int x, int y, int z,
                            const struct rb_uv *icon)
{
    struct rb_tess *t = m->t;
    rb_tess_start_quads(t);
    rb_tess_set_translation(t, (double)(-(x >> 4 << 4)),
                            (double)(-(y >> 4 << 4)), (double)(-(z >> 4 << 4)));
    int id = rb_world_block(m->w, x, y, z);
    if (id <= 0 || id >= RB_IDS) return;
    m->override = icon;
    render_block(m, id, rb_world_meta(m->w, x, y, z), x, y, z);
    m->override = NULL;
}

/* RenderFallingBlock.doRender's anvil and dragon egg: the RenderBlocks paths
 * over the entity's world at its cell, the Tessellator translated by the
 * cell's negated corner minus a half, into t. */
void rb_mesh_falling_block(struct rb_mesher *m, struct rb_tess *t, int id, int meta, int x, int y, int z)
{
    struct rb_tess *saved = m->t;
    m->t = t;
    rb_tess_start_quads(t);
    rb_tess_set_translation(t, (double)((float)(-x) - 0.5F), (double)((float)(-y) - 0.5F),
                            (double)((float)(-z) - 0.5F));
    if (id == 145) rb3_render_anvil_meta(m, id, x, y, z, meta);
    else if (id == 122) rb3_render_dragon_egg(m, id, x, y, z);
    rb_tess_set_translation(t, 0.0, 0.0, 0.0);
    m->t = saved;
}

/* --------------------------------------------------------------- the setup */

static int atlas_uv_for(const struct rb_atlas *a, const char *name, struct rb_uv *out)
{
    for (int i = 0; i < a->n; ++i)
    {
        if (strcmp(a->name[i], name) == 0)
        {
            *out = a->uv[i];
            return 0;
        }
    }
    return -1;
}

int rb_mesher_init(struct rb_mesher *m, const struct rb_table *tab, const struct rb_atlas *a,
                   const struct rb_world *w, struct rb_tess *t, int px, int py, int pz)
{
    memset(m, 0, sizeof *m);
    m->tab = tab;
    m->atlas = a;
    m->w = w;
    m->t = t;
    m->px = px;
    m->py = py;
    m->pz = pz;
    m->icon = calloc((size_t)RB_IDS * RB_METAS * RB_SIDES, sizeof *m->icon);
    if (!m->icon) return -1;
    m->special = calloc(RB_SPECIALS + RB2_SPECIALS + RB3_SPECIALS, sizeof *m->special);
    if (!m->special) return -1;
    m->special2 = m->special + RB_SPECIALS;
    m->special3 = m->special2 + RB2_SPECIALS;
    m->mat_snow = material_by_name("field_151597_y");
    m->mat_crafted_snow = material_by_name("craftedSnow");

    for (int i = 0; i < tab->n_icons; ++i)
    {
        if (atlas_uv_for(a, tab->icon_name[i], &m->icon[i]) != 0)
        {
            fprintf(stderr, "render_blocks: icon %s is not in atlas.json\n", tab->icon_name[i]);
            return -1;
        }
    }

    m->has_missingno = atlas_uv_for(a, "missingno", &m->missingno) == 0;

    for (int i = 0; i < tab->n_special; ++i)
    {
        if (atlas_uv_for(a, tab->special[i], &m->special[i]) != 0)
        {
            fprintf(stderr, "render_blocks: special icon %s is not in atlas.json\n", tab->special[i]);
            return -1;
        }
    }

    struct perlin *p = calloc(2, sizeof *p);
    if (!p) return -1;
    jrand r;
    jr_seed(&r, 1234);
    perlin_init(&p[0], &r, 1);
    jr_seed(&r, 2345);
    perlin_init(&p[1], &r, 1);
    m->perlin_a = &p[0];
    m->perlin_b = &p[1];
    if (rb2_init(m) != 0) return -1;
    return rb3_init(m);
}

void rb_mesher_free(struct rb_mesher *m)
{
    free(m->icon);
    m->icon = NULL;
    free(m->special);   /* special2 and special3 are the same block's */
    m->special = m->special2 = m->special3 = NULL;
    free((void *)m->perlin_a);   /* perlin_b is the same block's second */
    m->perlin_a = m->perlin_b = NULL;
}
