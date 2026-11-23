/* Gate: the native placement port against the oracle's PlaceProbe dump
 * (oracle/harness/netherite/oracle/PlaceProbe.java). The probe loaded a square of
 * raw chunks, scattered supports, and ran one tryPlaceItemIntoWorld per case:
 * the placer's pose, the stack, the target cell, the side and the hit vector,
 * all drawn from Random(opseed) and written into the case record. Per case it
 * recorded the return value, the stack after (size, damage), the number of
 * block writes, the dropped item entities, an FNV-1a 64 hash of the 3x3 chunks
 * around the case position, the tile entities the case left different and the
 * Det digest line.
 *
 * The replay places the shapes, then runs each case's inputs through
 * place_try with the same streams and checks, case by case, the return value,
 * the stack, every write in order, the drops, the tile entities, the Det
 * states and the hash; then every field of every loaded chunk against
 * final.bin.gz. The first difference stops the run and names the case, the
 * write index and both values. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/items.h"
#include "../engine/place.h"
#include "probe.h"

/* A whole gzip file in memory; the caller frees. */
static unsigned char *probe_read_gz(const char *path, size_t *len)
{
    gzFile g = gzopen(path, "rb");
    size_t cap = 1 << 16, n = 0;
    unsigned char *buf = malloc(cap);
    int r;

    if (g == NULL)
    {
        perror(path);
        exit(2);
    }

    while ((r = gzread(g, buf + n, (unsigned)(cap - n))) > 0)
    {
        n += (size_t)r;

        if (n == cap)
        {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }

    gzclose(g);
    *len = n;
    return buf;
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

static int le16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}

#define CASE_BYTES 88
#define WRITE_BYTES 16
#define DROP_BYTES 63

/* ------------------------------------------------- the Det snapshot reader */

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

/* --------------------------------------------------- the write comparison
 * world's on_block hook: compare one write against the oracle's, in order. A
 * metadata-only write reports id -1, which the record stores as 0xffff. */
struct writes {
    unsigned char *base;
    uint32_t want, got;
    int overflow, bad;
    char what[256];
};

static struct writes cur;

static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    struct writes *s = ctx;

    if (s->got >= s->want)
    {
        if (!s->overflow)
        {
            s->overflow = 1;
            snprintf(s->what, sizeof s->what, "write %u (%d,%d,%d) id %d meta %d: the oracle has only %u writes",
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
        snprintf(s->what, sizeof s->what, "write %u want (%d,%d,%d) id %d meta %d, got (%d,%d,%d) id %d meta %d",
                 s->got + 1, wx, wy, wz, wid, wmeta, x, y, z, id & 0xffff, meta);
    }

    ++s->got;
}

/* ---------------------------------------------------- the drop comparison
 * the oracle's drops.bin, one 63-byte record per dropped entity, in the order
 * the probe recorded them (the world list, newest first). The native reports
 * drops as it makes them, so a case's records are buffered here and compared
 * against the oracle's slice newest first. */
static struct {
    const unsigned char *base;
    size_t len;
    size_t off;
    int bad;
    char what[256];
} drops;

static struct drop_ent case_drops[256];
static int case_ndrops;

static void on_drop(void *ctx, const struct drop_ent *e)
{
    (void)ctx;

    if (case_ndrops == (int)(sizeof case_drops / sizeof case_drops[0]))
    {
        if (!drops.bad)
        {
            drops.bad = 1;
            snprintf(drops.what, sizeof drops.what, "drop item %d at (%a,%a,%a): the case made too many drops",
                     e->item, e->x, e->y, e->z);
        }

        return;
    }

    case_drops[case_ndrops++] = *e;
}

/* the case is over: its drops, newest first, against the oracle's slice */
static int drop_case_check(int want, char *what, size_t whyn)
{
    if (case_ndrops != want)
    {
        snprintf(what, whyn, "the oracle recorded %d drops, the native replay made %d", want, case_ndrops);
        return 1;
    }

    for (int k = 0; k < case_ndrops; ++k)
    {
        const unsigned char *o = drops.base + drops.off + (size_t)k * DROP_BYTES;
        const struct drop_ent *e = &case_drops[case_ndrops - 1 - k];

        int want_kind = o[0];
        int want_item = (int)le16(o + 1);
        int want_damage = (int)le16(o + 3);
        int want_count = o[5];
        long long want_xp = (long long)(int32_t)le32(o + 6);
        double want_x = le_double(o + 10), want_y = le_double(o + 18), want_z = le_double(o + 26);
        double want_mx = le_double(o + 34), want_my = le_double(o + 42), want_mz = le_double(o + 50);
        float want_yaw = le_float(o + 58);

        if (want_kind != e->kind || want_item != e->item || want_damage != e->damage || want_count != e->count ||
            want_xp != e->xp || want_x != e->x || want_y != e->y || want_z != e->z ||
            want_mx != e->mx || want_my != e->my || want_mz != e->mz || want_yaw != e->yaw)
        {
            snprintf(what, whyn,
                     "drop want kind %d item %d dmg %d cnt %d xp %lld at (%a,%a,%a) mx %a yaw %a,"
                     " got kind %d item %d dmg %d cnt %d xp %lld at (%a,%a,%a) mx %a yaw %a",
                     want_kind, want_item, want_damage, want_count, want_xp, want_x, want_y, want_z,
                     want_mx, want_yaw, e->kind, e->item, e->damage, e->count, (long long)e->xp,
                     e->x, e->y, e->z, e->mx, e->yaw);
            return 1;
        }
    }

    drops.off += (size_t)case_ndrops * DROP_BYTES;
    return 0;
}

/* ---------------------------------------------------- the tile entities
 * the per-case diff, the same shape test_feature.c checks with. */
struct te {
    int x, y, z, kind;
    char *text;
};

static int te_order(const void *a, const void *b)
{
    const struct te *p = a, *q = b;

    if (p->x != q->x) return p->x < q->x ? -1 : 1;
    if (p->z != q->z) return p->z < q->z ? -1 : 1;
    if (p->y != q->y) return p->y < q->y ? -1 : 1;
    return 0;
}

static void te_fill(struct te *e, const struct tile_entity *te)
{
    /* the store's kind numbering is the tick probes'; the placement probe's
     * manifest records its own table, so translate */
    static const signed char probe_kind[20] = {
        1, 2, 3, 4, 10, 13, 17, 18, 6, 15, 7, 12, 20, 8, 9, 11, 14, 16, 19, 0
    };

    e->x = te->x;
    e->y = te->y;
    e->z = te->z;
    e->kind = te->kind >= 1 && te->kind <= 19 ? probe_kind[te->kind - 1] : te->kind;
    e->text = te_render(te);
}

/* In the 3x3 chunks around chunk (cx,cz): the area a case is checked in. */
static int te_in(int x, int z, int cx, int cz)
{
    return (x >> 4) >= cx - 1 && (x >> 4) <= cx + 1 && (z >> 4) >= cz - 1 && (z >> 4) <= cz + 1;
}

/* The tile entities of that area, rendered, in te_order. */
static struct te *te_local(struct world *w, int cx, int cz, int *n)
{
    int cap = 32, count = 0;
    struct te *v = malloc((size_t)cap * sizeof *v);

    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
        {
            struct chunk *c = world_chunk(w, cx + dx, cz + dz);

            if (c == NULL) continue;

            for (int j = 0; j < c->tes.n; ++j)
            {
                if (count == cap)
                {
                    cap *= 2;
                    v = realloc(v, (size_t)cap * sizeof *v);
                }

                te_fill(&v[count++], c->tes.v[j]);
            }
        }

    qsort(v, (size_t)count, sizeof *v, te_order);
    *n = count;
    return v;
}

static struct te *te_snapshot(struct world *w, int *n)
{
    int cap = 32, count = 0;
    struct te *v = malloc((size_t)cap * sizeof *v);

    for (size_t i = 0; i < w->cap; ++i)
    {
        struct chunk *c = chunk_ptr(w->slot[i]);

        if (c == NULL) continue;

        for (int j = 0; j < c->tes.n; ++j)
        {
            if (count == cap)
            {
                cap *= 2;
                v = realloc(v, (size_t)cap * sizeof *v);
            }

            te_fill(&v[count++], c->tes.v[j]);
        }
    }

    qsort(v, (size_t)count, sizeof *v, te_order);
    *n = count;
    return v;
}

static int read_entry(const unsigned char *rec, size_t rlen, size_t *off, int *x, int *y, int *z, int *kind,
                      const unsigned char **text, uint32_t *len)
{
    if (rlen - *off < 17) return 0;

    *x = (int)le32(rec + *off);
    *y = (int)le32(rec + *off + 4);
    *z = (int)le32(rec + *off + 8);
    *kind = rec[*off + 12];
    *len = le32(rec + *off + 13);
    *off += 17;

    if (rlen - *off < *len) return 0;

    *text = rec + *off;
    *off += *len;
    return 1;
}

static int want_nbt_diff(const char *want, const char *got, char *why, size_t whyn)
{
    nbt *a = nbt_parse(want);
    nbt *b = nbt_parse(got);
    int d;

    if (a == NULL || b == NULL) d = 1;
    else d = nbt_diff(a, b, why, whyn);

    if (a != NULL) nbt_free(a);
    if (b != NULL) nbt_free(b);
    return d;
}

static int entry_matches(const unsigned char *rec, size_t rlen, size_t *off, int x, int y, int z, int kind,
                         const char *text, char *what, size_t whyn)
{
    int wx, wy, wz, wkind;
    const unsigned char *wtext;
    uint32_t wlen;

    if (!read_entry(rec, rlen, off, &wx, &wy, &wz, &wkind, &wtext, &wlen))
    {
        snprintf(what, whyn, "the record has no entry for (%d,%d,%d) kind %d", x, y, z, kind);
        return 1;
    }

    if (wx != x || wy != y || wz != z || wkind != kind)
    {
        snprintf(what, whyn, "want (%d,%d,%d) kind %d, got (%d,%d,%d) kind %d", wx, wy, wz, wkind, x, y, z, kind);
        return 1;
    }

    size_t n = strlen(text);

    if (wlen != n || memcmp(wtext, text, n))
    {
        char *w = malloc((size_t)wlen + 1);
        char why[256];

        memcpy(w, wtext, wlen);
        w[wlen] = 0;

        if (want_nbt_diff(w, text, why, sizeof why))
            snprintf(what, whyn, "tile entity (%d,%d,%d) kind %d: %.150s", x, y, z, kind, why);
        else
            snprintf(what, whyn, "tile entity (%d,%d,%d) kind %d: want %.90s got %.90s", x, y, z, kind, w, text);

        free(w);
        return 1;
    }

    return 0;
}

/* A case whose recorded entries all lie in the 3x3 chunks around it (cx,cz)
 * renders only that area's tile entities and carries every other text from
 * the case before; one with an entry outside, and every full case (every
 * 64th, the last, and all of them under --hash-all), renders the whole
 * world, which catches a native change far from the case that no record
 * lists. */
static int check_tiles(const unsigned char *rec, size_t rlen, size_t *off, struct world *w, struct te **prev,
                       int *nprev, long long *total, char *what, size_t whyn, int full, int cx, int cz)
{
    if (rlen - *off < 4)
    {
        snprintf(what, whyn, "the record ends early");
        return 1;
    }

    uint32_t want = le32(rec + *off);
    *off += 4;

    size_t o = *off;

    for (uint32_t k = 0; k < want && !full; ++k)
    {
        int ex, ey, ez, ek;
        const unsigned char *et;
        uint32_t el;

        /* a short record goes to the full path, which reports it */
        if (!read_entry(rec, rlen, &o, &ex, &ey, &ez, &ek, &et, &el) || !te_in(ex, ez, cx, cz)) full = 1;
    }

    int ncur = 0;
    struct te *cur;

    if (full)
        cur = te_snapshot(w, &ncur);
    else
    {
        int nloc;
        struct te *loc = te_local(w, cx, cz, &nloc);
        int a = 0, b = 0;

        cur = malloc((size_t)(*nprev + nloc + 1) * sizeof *cur);

        while (a < *nprev || b < nloc)
        {
            if (a < *nprev && te_in((*prev)[a].x, (*prev)[a].z, cx, cz)) ++a;
            else if (b == nloc || (a < *nprev && te_order(&(*prev)[a], &loc[b]) < 0)) cur[ncur++] = (*prev)[a++];
            else cur[ncur++] = loc[b++];
        }

        free(loc);
    }

    int i = 0, j = 0;
    uint32_t got = 0;
    int bad = 0;

    while ((i < *nprev || j < ncur) && !bad)
    {
        int c = i == *nprev ? 1 : j == ncur ? -1 : te_order(&(*prev)[i], &cur[j]);
        int emit;

        if (c < 0)
        {
            bad = entry_matches(rec, rlen, off, (*prev)[i].x, (*prev)[i].y, (*prev)[i].z, 0, "", what, whyn);
            ++i;
            emit = 1;
        }
        else if (c > 0)
        {
            bad = entry_matches(rec, rlen, off, cur[j].x, cur[j].y, cur[j].z, cur[j].kind, cur[j].text, what, whyn);
            ++j;
            emit = 1;
        }
        else
        {
            emit = (*prev)[i].text != cur[j].text && strcmp((*prev)[i].text, cur[j].text) != 0;

            if (emit)
                bad = entry_matches(rec, rlen, off, cur[j].x, cur[j].y, cur[j].z, cur[j].kind, cur[j].text, what, whyn);

            ++i;
            ++j;
        }

        if (emit && !bad) ++got;
    }

    if (!bad && got != want)
    {
        snprintf(what, whyn, "the record has %u entries, the world changed %u", want, got);
        bad = 1;
    }

    if (!bad) *total += got;

    /* the area's old texts were rendered afresh; the rest carry into cur */
    for (int k = 0; k < *nprev; ++k)
        if (full || te_in((*prev)[k].x, (*prev)[k].z, cx, cz)) free((*prev)[k].text);

    free(*prev);
    *prev = cur;
    *nprev = ncur;
    return bad;
}

/* --------------------------------------------------------- the digest line */

static int check_digest(gzFile df, const char *dir, int i, det_state *det)
{
    char line[1024];

    if (!gzgets(df, line, sizeof line))
    {
        printf("FAIL %s: digest line missing at case %d\n", dir, i);
        return 1;
    }

    char *tok[32];
    int n = 0;
    char *p = strtok(line, " ");

    while (p && n < 32)
    {
        tok[n++] = p;
        p = strtok(NULL, " ");
    }

    if (n != 24 || strcmp(tok[0], "t") != 0 || atoi(tok[1]) != i)
    {
        printf("FAIL %s case %d: digest line has %d tokens, expected 24 for case %d\n", dir, i, n, i);
        return 1;
    }

    int bad = 0;

    for (int role_i = 0; role_i < DET_ROLES; ++role_i)
    {
        uint64_t a = strtoull(tok[4 + role_i * 5], NULL, 16);
        uint64_t b = strtoull(tok[5 + role_i * 5], NULL, 16);
        uint64_t c = strtoull(tok[6 + role_i * 5], NULL, 16);

        if (det_seeder_state(det, role_i) != a)
        {
            printf("FAIL %s case %d: role %d seeder state: the oracle recorded %016llx, the native replay has"
                   " %016llx\n", dir, i, role_i, (unsigned long long)a, (unsigned long long)det_seeder_state(det, role_i));
            bad = 1;
        }

        if (det_math_state(det, role_i) != b)
        {
            printf("FAIL %s case %d: role %d math state: the oracle recorded %016llx, the native replay has"
                   " %016llx\n", dir, i, role_i, (unsigned long long)b, (unsigned long long)det_math_state(det, role_i));
            bad = 1;
        }

        if (det_split_state(det, role_i) != c)
        {
            printf("FAIL %s case %d: role %d split state: the oracle recorded %016llx, the native replay has"
                   " %016llx\n", dir, i, role_i, (unsigned long long)c, (unsigned long long)det_split_state(det, role_i));
            bad = 1;
        }
    }

    return bad;
}

/* --------------------------------------------------------------- the sweep */

int main(int argc, char **argv)
{
    probe_args(&argc, argv);
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_place PROBE_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1200];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    if (strstr(manifest, "\"kind\":\"place\"") == NULL)
    {
        printf("skip %s: not a placement dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int cases = (int)manifest_int(manifest, "cases");
    int nshapes = (int)manifest_int(manifest, "shapes");
    double placer_y_off = 0.0;
    {
        const char *yp = strstr(manifest, "\"placer_y_offset\":");
        if (yp) placer_y_off = strtod(yp + strlen("\"placer_y_offset\":"), NULL);
    }

    int nchunks = 0, *lcz = NULL;
    int *lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    struct world w;

    world_init(&w, seed);

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    w.on_drop_ctx = NULL;

    /* the shapes, placed as the probe placed them */
    snprintf(path, sizeof path, "%s/shapes.bin", dir);
    size_t slen;
    unsigned char *shapes = probe_read_file(path, &slen);

    if ((int)(slen / 16) != nshapes)
    {
        fprintf(stderr, "%s: shapes.bin holds %d rows, the manifest says %d\n", dir, (int)(slen / 16), nshapes);
        return 2;
    }

    for (int i = 0; i < nshapes; ++i)
    {
        const unsigned char *s = shapes + i * 16;

        world_set_block(&w, (int)le32(s), (int)le32(s + 4), (int)le32(s + 8), le16(s + 12), s[14], 2);
    }

    /* the write and drop listeners go on after the shapes, as the probe's
     * does: the drops the shapes themselves make are the oracle's own
     * entities, spawned outside any case and recorded by nothing */
    w.on_block = on_write;
    w.on_block_ctx = &cur;
    w.on_drop = on_drop;
    w.on_drop_ctx = &drops;

    /* the region after the shapes, before the first case */
    snprintf(path, sizeof path, "%s/startchunks.bin.gz", dir);

    if (access(path, F_OK) == 0 && cmp_final(dir, path, &w, lcx, lcz, nchunks))
        return 1;

    /* the Det state the sweep starts from */
    struct ref start;

    read_ref(&start, dir, "start.txt");

    det_state det;
    det_init(&det);
    det_load(&det, start.world_seed, start.seeder, start.math, start.next_id);

    for (int i = 0; i < start.nsplits; ++i)
        det_split_add(&det, start.splits[i].name, start.splits[i].state, start.splits[i].used);

    det_split *item_rand = det_split_find(&det, "./net/minecraft/item/Item.java:itemRand");

    if (item_rand == NULL)
    {
        fprintf(stderr, "%s: the Det snapshot has no item split\n", dir);
        return 2;
    }

    snprintf(path, sizeof path, "%s/cases.bin", dir);
    size_t clen;
    unsigned char *cbuf = probe_read_file(path, &clen);

    if (clen % CASE_BYTES != 0 || (int)(clen / CASE_BYTES) != cases)
    {
        fprintf(stderr, "%s: cases.bin is %zu bytes, the manifest says %d cases\n", dir, clen, cases);
        return 2;
    }

    snprintf(path, sizeof path, "%s/writes.bin", dir);
    size_t wlen;
    unsigned char *wbuf = probe_read_file(path, &wlen);
    size_t woff = 0;

    snprintf(path, sizeof path, "%s/drops.bin", dir);
    size_t dlen;
    unsigned char *dbuf = probe_read_file(path, &dlen);

    drops.base = dbuf;
    drops.len = dlen;

    snprintf(path, sizeof path, "%s/tileentities.bin", dir);
    size_t telen;
    unsigned char *tebuf = probe_read_file(path, &telen);
    size_t teoff = 0;
    struct te *prev = NULL;
    int nprev = 0;
    long long te_entries = 0;

    snprintf(path, sizeof path, "%s/digest.txt.gz", dir);
    gzFile df = gzopen(path, "rb");

    if (!df)
    {
        perror(path);
        return 2;
    }

    unsigned char *hashbuf = malloc(CHUNK_BYTES);
    int fail = 0;
    long long placed = 0, refused = 0;
    uint64_t total_writes = 0, total_drops = 0;

    /* the watch channel: the oracle's per-case chunk hashes (debugging) */
    int watch_n = 0;
    static int watch_cx[128], watch_cz[128];
    unsigned char *watchbuf = NULL;
    size_t watch_pos = 0;
    int dump_at = -1;
    static int dump_cx[16], dump_cz[16];
    int dump_n = 0;
    {
        const char *dp = strstr(manifest, "\"dump_at\":");

        if (dp)
        {
            dump_at = (int)strtol(dp + strlen("\"dump_at\":"), NULL, 10);
            const char *dc = strstr(manifest, "\"dump_chunks\":\"");

            if (dc)
            {
                dc += strlen("\"dump_chunks\":\"");
                char *end;

                while (*dc && *dc != '"' && dump_n < 16)
                {
                    dump_cx[dump_n] = (int)strtol(dc, &end, 10);
                    dump_cz[dump_n] = (int)strtol(strchr(end, ',') + 1, &end, 10);
                    ++dump_n;
                    dc = end;

                    while (*dc == ' ') ++dc;
                }
            }
        }
    }
    const char *wp = strstr(manifest, "\"watch_chunks\":[");

    if (wp)
    {
        wp += strlen("\"watch_chunks\":[");
        char *end;

        while (*wp && *wp != ']' && watch_n < 128)
        {
            watch_cx[watch_n] = (int)strtoll(wp + 1, &end, 10);
            watch_cz[watch_n] = (int)strtoll(strchr(end, ',') + 1, &end, 10);
            ++watch_n;
            wp = strchr(end, ']') + 1;

            if (*wp == ',') ++wp;
            else break;
        }

        snprintf(path, sizeof path, "%s/watch.bin.gz", dir);
        watchbuf = probe_read_gz(path, &watch_pos);
        watch_pos = 0;
    }

    for (int i = 0; i < cases && !fail; ++i)
    {
        const unsigned char *c = cbuf + (size_t)CASE_BYTES * i;
        int count = c[4];
        int side = c[5];
        int ret_want = c[7];
        int x = (int)le32(c + 8), y = (int)le32(c + 12), z = (int)le32(c + 16);
        float hx = le_float(c + 20), hy = le_float(c + 24), hz = le_float(c + 28);
        double px = le_double(c + 32), py = le_double(c + 40), pz = le_double(c + 48);
        float yaw = le_float(c + 56), pitch = le_float(c + 60);
        long long case_seed = (long long)le64(c + 64);
        uint32_t nw = le32(c + 72);
        int stack_size_want = c[76];
        int stack_damage_want = (int)le16(c + 77);
        int drops_want = c[79];
        uint64_t want_hash = le64(c + 80);

        (void)pitch;
        (void)py;

        if (woff + (size_t)nw * WRITE_BYTES > wlen)
        {
            fprintf(stderr, "%s: writes.bin is short at case %d (wants %u more)\n", dir, i, nw);
            return 2;
        }

        cur.base = wbuf + woff;
        cur.want = nw;
        cur.got = 0;
        cur.overflow = 0;
        cur.bad = 0;
        cur.what[0] = 0;

        drops.bad = 0;
        case_ndrops = 0;
        drops.what[0] = 0;

        struct placer pl = {px, py, pz, yaw, pitch, placer_y_off};
        struct place_stack stack = {(int)le16(c), count, (int)le16(c + 2)};

        place_set_case(case_seed, &det, &det.math[DET_OTHER], item_rand);

        int ret = place_try(&w, &pl, &stack, x, y, z, side, hx, hy, hz);

        if (ret != ret_want)
        {
            printf("FAIL %s case %d: return: the oracle recorded %d, the native replay has %d\n",
                   dir, i, ret_want, ret);
            fail = 1;
            break;
        }

        if (stack.count != stack_size_want || stack.damage != stack_damage_want)
        {
            printf("FAIL %s case %d: stack: the oracle recorded size %d damage %d, the native replay has"
                   " size %d damage %d\n", dir, i, stack_size_want, stack_damage_want, stack.count, stack.damage);
            fail = 1;
            break;
        }

        if (cur.got != nw)
        {
            printf("FAIL %s case %d: %u writes, the oracle recorded %u%s%s\n", dir, i, cur.got, nw,
                   cur.overflow ? ", " : "", cur.overflow ? cur.what : "");
            fail = 1;
            break;
        }

        if (cur.bad)
        {
            printf("FAIL %s case %d: %s\n", dir, i, cur.what);
            fail = 1;
            break;
        }

        uint64_t got_hash = probe_hash_due(i, cases) ? hash_around(&w, x >> 4, z >> 4, hashbuf) : want_hash;

        /* debugging: on a hash mismatch, dump the native's chunks the
         * manifest's dump_chunks lists, in the oracle's atN.bin.gz format */
        if (got_hash != want_hash && dump_at >= 0)
        {
            gzFile dz = gzopen("/tmp/native-at.bin.gz", "wb");
            unsigned char abuf[CHUNK_BYTES];

            for (int k = 0; k < dump_n; ++k)
            {
                unsigned char head2[8], cols2[256];
                struct chunk *dc = world_chunk(&w, dump_cx[k], dump_cz[k]);

                if (dc == NULL) continue;

                chunk_bytes(dc, abuf);
                put_le32(head2, dump_cx[k]);
                put_le32(head2 + 4, dump_cz[k]);
                gzwrite(dz, head2, 8);
                gzwrite(dz, abuf, CHUNK_BYTES);

                for (int j = 0; j < 256; ++j) cols2[j] = (unsigned char)(dc->update_skylight_columns[j] ? 1 : 0);

                gzwrite(dz, cols2, 256);
                gzputc(dz, dc->gap_lighting_updated ? 1 : 0);
            }

            gzclose(dz);
        }

        if (got_hash != want_hash)
        {
            printf("FAIL %s case %d: hash of the 3x3 chunks want %016llx got %016llx\n", dir, i,
                   (unsigned long long)want_hash, (unsigned long long)got_hash);
            fail = 1;
            break;
        }

        /* the drops: buffered per case, newest first, against the oracle's
         * slice; the end-of-run check is drops.off reaching dlen */
        if (drops.bad || drop_case_check(drops_want, cur.what, sizeof cur.what))
        {
            printf("FAIL %s case %d: drops: %s%s%s\n", dir, i, drops.bad ? drops.what : "",
                   drops.bad && cur.what[0] ? "; " : "", drops.bad ? "" : cur.what);
            fail = 1;
            break;
        }

        /* the tile entities */
        if (check_tiles(tebuf, telen, &teoff, &w, &prev, &nprev, &te_entries, cur.what, sizeof cur.what,
                        probe_hash_due(i, cases), x >> 4, z >> 4))
        {
            printf("FAIL %s case %d: tile entities: %s\n", dir, i, cur.what);
            fail = 1;
            break;
        }

        /* the Det states */
        if (check_digest(df, dir, i, &det))
        {
            fail = 1;
            break;
        }

        /* the watch channel: the oracle's per-case chunk hashes */
        if (watch_n > 0)
        {
            for (int k = 0; k < watch_n; ++k)
            {
                unsigned char wb[CHUNK_BYTES];
                struct chunk *wc = world_chunk(&w, watch_cx[k], watch_cz[k]);
                uint64_t got = FNV_OFFSET, want = 0;

                if (wc != NULL)
                {
                    chunk_bytes(wc, wb);

                    for (int b = 0; b < CHUNK_BYTES; ++b) got = (got ^ wb[b]) * FNV_PRIME;
                }
                else got *= FNV_PRIME;

                for (int b = 0; b < 8; ++b) want |= (uint64_t)watchbuf[watch_pos] << (8 * b), watch_pos++;

                if (got != want)
                    printf("WATCH %s: chunk (%d,%d) diverged after case %d: want %016llx got %016llx\n",
                           dir, watch_cx[k], watch_cz[k], i, (unsigned long long)want, (unsigned long long)got);
            }
        }

        woff += (size_t)nw * WRITE_BYTES;
        total_writes += nw;
        total_drops += (uint64_t)drops_want;
        if (ret_want) ++placed;
        else ++refused;

    }

    if (!fail && teoff != telen)
    {
        printf("FAIL %s: the tile entity record has %zu bytes left after %d cases\n", dir, telen - teoff, cases);
        fail = 1;
    }

    if (!fail && drops.off != dlen)
    {
        printf("FAIL %s: the drop record has %zu bytes left after %d cases\n", dir, dlen - drops.off, cases);
        fail = 1;
    }

    if (!fail)
    {
        snprintf(path, sizeof path, "%s/final.bin.gz", dir);
        fail = cmp_final(dir, path, &w, lcx, lcz, nchunks);
    }

    if (!fail)
        printf("PASS %s: %d/%d cases (%lld placed, %lld refused), %llu writes, %llu drops, %d chunks, seed %lld\n",
               dir, cases, cases, placed, refused, (unsigned long long)total_writes,
               (unsigned long long)total_drops, nchunks, (long long)seed);

    gzclose(df);
    free(hashbuf);
    free(shapes);
    free(wbuf);
    free(cbuf);
    free(dbuf);
    free(tebuf);
    for (int i = 0; i < nprev; ++i) free(prev[i].text);
    free(prev);
    free(lcx);
    free(lcz);
    free(manifest);
    world_free(&w);
    if (fail) probe_localize(argv[0], dir);

    return fail;
}