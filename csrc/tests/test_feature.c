/* Gate: the native population features against the oracle's feature probe
 * (oracle/harness/netherite/oracle/FeatureProbe.java; recorded with
 * `make -C oracle probe KIND=feature`).
 *
 * The probe loaded a square of raw chunks, ran one feature many times (each
 * case: a config from the feature table, a position and a fresh Random seed),
 * and recorded per case the number of block writes the feature made and an
 * FNV-1a 64 hash of the 3x3 chunks around the case position, then every write
 * in order, then the whole region after the last case.
 *
 * This replays cases.bin through the native feature table and checks, case by
 * case, the write count and every write and the hash; then every field of every
 * loaded chunk against final.bin.gz. A feature whose probe records tile
 * entities (the manifest's "tile_entities") also has every entity the case left
 * checked, against the record the probe wrote for that case. The first
 * difference stops the run and names the case, the write index and both values. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/features.h"
#include "../engine/env.h"
#include "../engine/features_nether.h"
#include "../engine/nbtjson.h"
#include "probe.h"

/* The oracle's case record: config uint8, x/y/z int32, seed int64, writes
 * uint32, hash uint64. */
#define CASE_BYTES 33

/* The write listener's state for the case in progress: where the case's writes
 * start in the oracle's writes.bin and how many have been recorded. A write
 * past the recorded count is a difference in itself. */
struct writes
{
    unsigned char *base;   /* the oracle's bytes for this case */
    uint32_t want;         /* how many the oracle recorded */
    uint32_t got;
    int overflow;          /* the native side wrote more than the oracle */
    int bad;               /* first difference: 1-based index, 0 for none */
};

static struct writes cur;
static char diff_what[256];

/* One tile entity of the native world, for the per-case diff against the
 * oracle's record (the manifest's "tile_entities" file): its position, its kind
 * and the canonical NBT text te_render gives it. */
struct te
{
    int x, y, z, kind;
    char *text;
};

/* The record's order: x, then z, then y. */
static int te_order(const void *a, const void *b)
{
    const struct te *p = a, *q = b;

    if (p->x != q->x) return p->x < q->x ? -1 : 1;
    if (p->z != q->z) return p->z < q->z ? -1 : 1;
    if (p->y != q->y) return p->y < q->y ? -1 : 1;
    return 0;
}

/* Every tile entity the world holds, in the record's order. */
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

            struct tile_entity *te = c->tes.v[j];

            v[count].x = te->x;
            v[count].y = te->y;
            v[count].z = te->z;
            /* the record's kind: 1 chest, 2 spawner */
            v[count].kind = te->kind == TE_CHEST ? FE_REC_CHEST : FE_REC_SPAWNER;
            v[count].text = te_render(te);
            ++count;
        }
    }

    qsort(v, (size_t)count, sizeof *v, te_order);
    *n = count;
    return v;
}

static void te_list_free(struct te *v, int n)
{
    for (int i = 0; i < n; ++i) free(v[i].text);

    free(v);
}

/* One entry of the record: x, y, z, kind uint8, len uint32 LE, text. */
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

/* 1 when two canonical NBT texts differ, naming the first key path. */
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

/* 1 when the record's next entry is this position, kind and text; the text
 * difference is named by key through nbt_diff when it is canonical. */
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

/* The case's record: the entries the case left different from the case before,
 * the same merge the probe wrote them with. */
static int check_tiles(const unsigned char *rec, size_t rlen, size_t *off, struct world *w, struct te **prev,
                       int *nprev, long long *total, char *what, size_t whyn)
{
    if (rlen - *off < 4)
    {
        snprintf(what, whyn, "the record ends early");
        return 1;
    }

    uint32_t want = le32(rec + *off);
    *off += 4;

    int ncur = 0;
    struct te *cur = te_snapshot(w, &ncur);
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
            emit = strcmp((*prev)[i].text, cur[j].text) != 0;

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

    te_list_free(*prev, *nprev);
    *prev = cur;
    *nprev = ncur;
    return bad;
}

/* world's on_block hook: compare one write against the oracle's, in order. */

/* world's on_block hook: compare one write against the oracle's, in order. The
 * listener passes id -1 for a metadata-only write (World.setBlockMetadataWithNotify
 * is patched the same way), which the probe records as 0xffff, so ids compare
 * with 16 bits. */
static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    struct writes *s = ctx;
    int iid = id & 0xffff;

    if (s->got >= s->want)
    {
        if (!s->overflow)
        {
            s->overflow = 1;
            snprintf(diff_what, sizeof diff_what, "write %u (%d,%d,%d) id %d meta %d: the oracle has only %u writes",
                     s->got + 1, x, y, z, iid, meta, s->want);
        }

        ++s->got;
        return;
    }

    const unsigned char *o = s->base + 16 * (size_t)s->got;
    int wx = (int)le32(o), wy = (int)le32(o + 4), wz = (int)le32(o + 8);
    int wid = o[12] | o[13] << 8, wmeta = o[14];

    /* a metadata change reports id -1, which the record stores as 0xffff */
    if (!s->bad && (x != wx || y != wy || z != wz || (id & 0xffff) != wid || meta != wmeta))
    {
        s->bad = (int)s->got + 1;
        snprintf(diff_what, sizeof diff_what, "write %u want (%d,%d,%d) id %d meta %d, got (%d,%d,%d) id %d meta %d",
                 s->got + 1, wx, wy, wz, wid, wmeta, x, y, z, iid, meta);
    }

    ++s->got;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_feature PROBE_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1024];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    char kind[64], feature[64];

    manifest_str(manifest, "kind", kind, sizeof kind);
    if (strcmp(kind, "feature") != 0)
    {
        printf("FAIL %s: kind %s is not a feature probe\n", dir, kind);
        free(manifest);
        return 2;
    }
    manifest_str(manifest, "feature", feature, sizeof feature);

    int64_t seed = manifest_int(manifest, "seed");
    int cases = (int)manifest_int(manifest, "cases");
    /* the dimension the probe ran in: 0 the overworld (every probe recorded
     * before the field), -1 the Nether, 1 the End */
    int dim = strstr(manifest, "\"dim\":") != NULL ? (int)manifest_int(manifest, "dim") : 0;

    int ncfg = 0;
    const struct feature *table = features_for(feature, &ncfg);

    if (table == NULL)
    {
        fprintf(stderr, "%s: the native feature table has no %s\n", dir, feature);
        return 2;
    }

    int nchunks = 0, *lcz = NULL;
    int *lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    struct world w;

    world_init(&w, seed);
    w.dim = dim;

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    w.on_block = on_write;
    w.on_block_ctx = &cur;

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

    /* A feature that leaves tile entities records them beside its writes
     * (FeatureProbeDungeons); one that does not has no such file and nothing
     * here runs. */
    char tefile[256];
    int have_tes = strstr(manifest, "\"tile_entities\":\"") != NULL;
    unsigned char *tebuf = NULL;
    size_t telen = 0, teoff = 0;
    struct te *prev = NULL;
    int nprev = 0;
    long long te_entries = 0;

    if (have_tes)
    {
        manifest_str(manifest, "tile_entities", tefile, sizeof tefile);
        snprintf(path, sizeof path, "%s/%s", dir, tefile);
        tebuf = probe_read_file(path, &telen);
    }

    /* A feature that spawns an entity (WorldGenSpikes' crystal) records it
     * beside its writes; the native generator reports the one it would spawn
     * through spike_crystal_last, and the records compare in order. */
    char efile[256];
    int have_ents = strstr(manifest, "\"entities\":\"") != NULL;
    unsigned char *ebuf = NULL;
    size_t elen = 0, eoff = 0;
    long long ents = 0;

    if (have_ents)
    {
        manifest_str(manifest, "entities", efile, sizeof efile);
        snprintf(path, sizeof path, "%s/%s", dir, efile);
        ebuf = probe_read_file(path, &elen);
    }

    unsigned char *hashbuf = malloc(CHUNK_BYTES);
    int fail = 0;
    uint64_t total = 0;
    int per_config[64];
    long long cfg_write[64];

    memset(per_config, 0, sizeof per_config);
    memset(cfg_write, 0, sizeof cfg_write);

    for (int i = 0; i < cases && !fail; ++i)
    {
        const unsigned char *c = cbuf + (size_t)CASE_BYTES * i;
        int ci = c[0];
        int x = (int)le32(c + 1), y = (int)le32(c + 5), z = (int)le32(c + 9);
        uint64_t fs = le64(c + 13);
        uint32_t nw = le32(c + 21);
        uint64_t want_hash = le64(c + 25);

        if (ci >= ncfg)
        {
            printf("FAIL %s case %d: config %d, the native table has %d rows\n", dir, i, ci, ncfg);
            ++fail;
            break;
        }

        if (woff + (size_t)nw * 16 > wlen)
        {
            fprintf(stderr, "%s: writes.bin is short at case %d (wants %u more)\n", dir, i, nw);
            return 2;
        }

        ++per_config[ci];

        cur.base = wbuf + woff;
        cur.want = nw;
        cur.got = 0;
        cur.overflow = 0;
        cur.bad = 0;

        jrand r;
        jr_seed(&r, (int64_t)fs);
        table[ci].generate(&w, &r, x, y, z, table[ci].block, table[ci].count);

        if (cur.got != nw)
        {
            printf("FAIL %s case %d config %d pos (%d,%d,%d): %u writes, the oracle recorded %u%s%s\n", dir, i, ci, x, y, z,
                   cur.got, nw, cur.overflow ? ", " : "", cur.overflow ? diff_what : "");
            ++fail;
            break;
        }

        if (cur.bad)
        {
            printf("FAIL %s case %d config %d pos (%d,%d,%d): %s\n", dir, i, ci, x, y, z, diff_what);
            ++fail;
            break;
        }

        uint64_t got_hash = probe_hash_due(i, cases) ? hash_around(&w, x >> 4, z >> 4, hashbuf) : want_hash;

        if (got_hash != want_hash)
        {
            printf("FAIL %s case %d config %d pos (%d,%d,%d): hash of the 3x3 chunks want %016llx got %016llx\n", dir,
                   i, ci, x, y, z, (unsigned long long)want_hash, (unsigned long long)got_hash);
            ++fail;
            break;
        }

        cfg_write[ci] += nw;
        total += nw;
        woff += (size_t)nw * 16;

        if (tebuf != NULL)
        {
            if (check_tiles(tebuf, telen, &teoff, &w, &prev, &nprev, &te_entries, diff_what, sizeof diff_what))
            {
                printf("FAIL %s case %d config %d pos (%d,%d,%d): tile entities: %s\n", dir, i, ci, x, y, z,
                       diff_what);
                ++fail;
                break;
            }
        }

        if (ebuf != NULL && spike_crystal_last.spawned)
        {
            if (eoff + 1 > elen)
            {
                printf("FAIL %s case %d config %d pos (%d,%d,%d): the native generator spawned an entity, "
                       "the record has none left\n", dir, i, ci, x, y, z);
                ++fail;
                break;
            }

            int nl = ebuf[eoff];

            if (eoff + 1 + (size_t)nl + 28 > elen)
            {
                printf("FAIL %s case %d: the entity record is short at %zu\n", dir, i, eoff);
                ++fail;
                break;
            }

            const unsigned char *p = ebuf + eoff + 1 + nl;
            uint64_t bx, by, bz, bits;

            bx = le64(p);
            by = le64(p + 8);
            bz = le64(p + 16);
            bits = (uint64_t)p[24] | (uint64_t)p[25] << 8 | (uint64_t)p[26] << 16 | (uint64_t)p[27] << 24;

            double ex, ey, ez;
            float eyaw;
            unsigned b4 = (unsigned)bits;

            memcpy(&ex, &bx, sizeof ex);
            memcpy(&ey, &by, sizeof ey);
            memcpy(&ez, &bz, sizeof ez);
            memcpy(&eyaw, &b4, sizeof eyaw);

            int bad = nl != 18 || memcmp(ebuf + eoff + 1, "EntityEnderCrystal", 18) != 0 ||
                      ex != spike_crystal_last.x || ey != spike_crystal_last.y ||
                      ez != spike_crystal_last.z || eyaw != spike_crystal_last.yaw;

            if (bad)
            {
                printf("FAIL %s case %d config %d pos (%d,%d,%d): entity want (%.1f,%.1f,%.1f) yaw %g, "
                       "got (%.1f,%.1f,%.1f) yaw %g\n", dir, i, ci, x, y, z, ex, ey, ez, eyaw,
                       spike_crystal_last.x, spike_crystal_last.y, spike_crystal_last.z,
                       spike_crystal_last.yaw);
                ++fail;
                break;
            }

            ++ents;
            eoff += 1 + (size_t)nl + 28;
        }
    }

    if (!fail && ebuf != NULL && eoff != elen)
    {
        printf("FAIL %s: the entity record has %zu bytes left after %d cases\n", dir, elen - eoff, cases);
        ++fail;
    }

    if (!fail && tebuf != NULL && teoff != telen)
    {
        printf("FAIL %s: the tile entity record has %zu bytes left after %d cases\n", dir, telen - teoff, cases);
        ++fail;
    }

    if (!fail)
    {
        snprintf(path, sizeof path, "%s/final.bin.gz", dir);
        fail = cmp_final(dir, path, &w, lcx, lcz, nchunks);
    }

    if (fail) probe_localize(argv[0], dir);

    if (!fail)
    {
        printf("PASS %s: %d/%d cases, %llu writes, %d chunks, seed %lld, %s\n", dir, cases, cases,
               (unsigned long long)total, nchunks, (long long)seed, feature);
        printf("     writes per config:");

        for (int i = 0; i < ncfg; ++i) printf(" %d:%lld/%d", i, cfg_write[i], per_config[i]);

        printf("\n");

        if (tebuf != NULL)
            printf("     tile entities: %lld entries compared, %d in the region after the last case\n", te_entries,
                   nprev);

        if (ebuf != NULL) printf("     entities: %lld records compared\n", ents);
    }

    free(hashbuf);
    free(wbuf);
    free(cbuf);
    free(tebuf);
    free(ebuf);
    te_list_free(prev, nprev);
    free(lcx);
    free(lcz);
    free(manifest);
    world_free(&w);
    return fail;
}
