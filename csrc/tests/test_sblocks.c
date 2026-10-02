/* Gate: the native structure block half against the oracle's
 * StructureBlocksProbe recordings (out/java/sblocks/<name>/).
 *
 * A probe file holds a square of raw chunks (population off) with the mine
 * generation's structure data beside it, and a list of mineshaft starts. For
 * each start the probe walked its chunk set cx-major with cz inner and, per
 * chunk step, seeded the population Random as ChunkProviderGenerate.populate
 * seeds it, ran start.generateStructure against the step chunk's 16x16 box at
 * +8, and recorded: every write in order, the tile entities the region diff
 * after the step, the entities the step constructed, an FNV-1a 64 hash of the
 * 3x3 chunks around the step chunk, and the whole region after the last step.
 *
 * This replays the same walk through the native structure layer: the starts
 * come from structure_walk (test_structures.c already checks them piece for
 * piece), each step runs sc_generate over the structure type's block dispatch
 * (mineshaft_blocks.c, temple_blocks.c), and
 * every write, tile entity, entity and hash is checked in order against the
 * record. The first difference stops the run and names the step. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/fortress.h"
#include "../engine/mineshaft.h"
#include "../engine/nbtjson.h"
#include "../engine/stronghold.h"
#include "../engine/temple.h"
#include "../engine/temple_blocks.h"
#include "../engine/tileentity.h"
#include "../engine/village.h"
#include "probe.h"

/* One step of the record: the start it belongs to, the step chunk, the write
 * count and the 3x3 hash. */
struct step_rec
{
    int start_cx, start_cz;
    int cx, cz;
    uint32_t writes;
    uint64_t hash;
};

/* The oracle's entity record for one step: what recordEntities wrote. */
struct ent_rec
{
    int kind;
    int entity_id;
    int64_t uuid_msb, uuid_lsb;
    double x, y, z;
    float yaw, hover;
    double motion_x, motion_z;
    int item, damage, count;
    int profession;         /* a villager's profession */
    char *nbt;              /* a cart's Items list, canonical text */
};

/* The write listener's state for the step in progress. */
static struct
{
    unsigned char *base;    /* the step's bytes in writes.bin */
    uint32_t want;
    uint32_t got;
    int overflow;
    int bad;
} cur;

static char diff_what[256];

static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    (void)ctx;
    int iid = id & 0xffff;

    if (cur.got >= cur.want)
    {
        if (!cur.overflow)
        {
            /* the first difference in order is the bad write, when there was
             * one; the overflow note only fills diff_what when there was not */
            cur.overflow = 1;

            if (!cur.bad)
                snprintf(diff_what, sizeof diff_what, "write %u (%d,%d,%d) id %d meta %d: the oracle has only %u writes",
                         cur.got + 1, x, y, z, iid, meta, cur.want);
        }

        ++cur.got;
        return;
    }

    const unsigned char *o = cur.base + 16 * (size_t)cur.got;
    int wx = (int)le32(o), wy = (int)le32(o + 4), wz = (int)le32(o + 8);
    int wid = o[12] | o[13] << 8, wmeta = o[14];


    if (!cur.bad && (x != wx || y != wy || z != wz || iid != wid || meta != wmeta))
    {
        cur.bad = (int)cur.got + 1;
        snprintf(diff_what, sizeof diff_what, "write %u want (%d,%d,%d) id %d meta %d, got (%d,%d,%d) id %d meta %d",
                 cur.got + 1, wx, wy, wz, wid, wmeta, x, y, z, iid, meta);
    }

    ++cur.got;
}

/* ------------------------------------------------------------ record readers */

static void ent_free(struct ent_rec *v, int n)
{
    for (int i = 0; i < n; ++i) free(v[i].nbt);

    free(v);
}

/* One recorded step's entities, in construction order. */
static int read_ents(const unsigned char *buf, size_t len, size_t *off, struct ent_rec **out, uint32_t *nout)
{
    if (len - *off < 4) return 0;

    uint32_t n = le32(buf + *off);
    *off += 4;

    struct ent_rec *v = malloc((size_t)(n ? n : 1) * sizeof *v);

    for (uint32_t i = 0; i < n; ++i)
    {
        memset(&v[i], 0, sizeof v[i]);

        if (len - *off < 25) { free(v); return 0; }

        v[i].kind = buf[*off + 4];
        v[i].entity_id = (int)le32(buf + *off + 5);
        v[i].uuid_msb = (int64_t)le64(buf + *off + 9);
        v[i].uuid_lsb = (int64_t)le64(buf + *off + 17);
        *off += 25;

        if (len - *off < 24) { free(v); return 0; }

        memcpy(&v[i].x, buf + *off, 8);
        memcpy(&v[i].y, buf + *off + 8, 8);
        memcpy(&v[i].z, buf + *off + 16, 8);
        *off += 24;

        if (v[i].kind == 4)
        {
            /* a witch: the position setLocationAndAngles gave it, nothing else */
        }
        else if (v[i].kind == 1)
        {
            uint32_t tlen = le32(buf + *off);
            *off += 4;

            if (len - *off < tlen) { free(v); return 0; }

            v[i].nbt = malloc((size_t)tlen + 1);
            memcpy(v[i].nbt, buf + *off, tlen);
            v[i].nbt[tlen] = 0;
            *off += tlen;
        }
        else if (v[i].kind == 3)
        {
            if (len - *off < 4) { free(v); return 0; }

            v[i].profession = (int)le32(buf + *off);
            *off += 4;
        }
        else
        {
            if (len - *off < 36) { free(v); return 0; }

            memcpy(&v[i].yaw, buf + *off, 4);
            memcpy(&v[i].hover, buf + *off + 4, 4);
            memcpy(&v[i].motion_x, buf + *off + 8, 8);
            memcpy(&v[i].motion_z, buf + *off + 16, 8);
            v[i].item = (int)le32(buf + *off + 24);
            v[i].damage = (int)le32(buf + *off + 28);
            v[i].count = (int)le32(buf + *off + 32);
            *off += 36;
        }
    }

    *out = v;
    *nout = n;
    return 1;
}

/* ------------------------------------------------------------ the check */

static void ent_diff(const struct sc_ctx *c, const struct ent_rec *w, uint32_t wi, char *why, size_t whyn)
{
    /* the first mismatched field, in the record's layout order */
    if (wi >= (uint32_t)c->nents)
    {
        snprintf(why, whyn, "the oracle constructed a kind %d entity, the native run none", w->kind);
        return;
    }

    const struct sc_ent *g = &c->ents[wi];

    if (g->kind != w->kind)
    {
        snprintf(why, whyn, "entity kind want %d got %d", w->kind, g->kind);
        return;
    }

    if (g->entity_id != w->entity_id)
    {
        snprintf(why, whyn, "entity id want %d got %d", w->entity_id, g->entity_id);
        return;
    }

    if (g->uuid_msb != w->uuid_msb || g->uuid_lsb != w->uuid_lsb)
    {
        snprintf(why, whyn, "entity uuid want %016llx%016llx got %016llx%016llx",
                 (unsigned long long)w->uuid_msb, (unsigned long long)w->uuid_lsb,
                 (unsigned long long)g->uuid_msb, (unsigned long long)g->uuid_lsb);
        return;
    }

    if (w->kind == SC_CART)
    {
        /* the position and the loot, as canonical NBT over the Items list */
        nbt *want = nbt_parse(w->nbt);
        nbt *have = nbt_new_compound();
        nbt *items = nbt_new_list();

        nbt_put(have, "Items", items);

        for (int i = 0; i < CART_SLOTS; ++i)
        {
            if (g->slots[i].item <= 0) continue;

            nbt *e = nbt_new_compound();

            nbt_put(e, "Slot", nbt_new_byte(i));
            nbt_put(e, "id", nbt_new_short(g->slots[i].item));
            nbt_put(e, "Count", nbt_new_byte(g->slots[i].count));
            nbt_put(e, "Damage", nbt_new_short(g->slots[i].damage));

            itag_put(e, g->slots[i].tag);

            nbt_list_add(items, e);
        }

        char *got = nbt_render(have);
        nbt *got_tree = nbt_parse(got);

        if (want == NULL || got_tree == NULL || nbt_diff(want, got_tree, why, whyn))
        {
            snprintf(why, whyn, "cart items differ: want %.100s got %.100s", w->nbt, got);
        }
        else if (g->x != w->x || g->y != w->y || g->z != w->z)
        {
            snprintf(why, whyn, "cart position want (%f,%f,%f) got (%f,%f,%f)", w->x, w->y, w->z, g->x, g->y, g->z);
        }

        free(got);
        nbt_free(got_tree);
        nbt_free(have);
        nbt_free(want);
    }
    else if (w->kind == SC_WITCH)
    {
        if (g->x != w->x || g->y != w->y || g->z != w->z)
        {
            snprintf(why, whyn, "witch position want (%f,%f,%f) got (%f,%f,%f)", w->x, w->y, w->z, g->x, g->y, g->z);
        }
    }
    else if (w->kind == SC_VILLAGER)
    {
        if (g->x != w->x || g->y != w->y || g->z != w->z)
        {
            snprintf(why, whyn, "villager position want (%f,%f,%f) got (%f,%f,%f)", w->x, w->y, w->z, g->x, g->y, g->z);
        }
        else if (g->profession != w->profession)
        {
            snprintf(why, whyn, "villager profession want %d got %d", w->profession, g->profession);
        }
    }
    else
    {
        if (g->x != w->x || g->y != w->y || g->z != w->z)
        {
            snprintf(why, whyn, "item drop position want (%f,%f,%f) got (%f,%f,%f)", w->x, w->y, w->z, g->x, g->y, g->z);
        }
        else if (g->yaw != w->yaw || g->hover != w->hover)
        {
            snprintf(why, whyn, "item drop yaw/hover differ");
        }
        else if (g->motion_x != w->motion_x || g->motion_z != w->motion_z)
        {
            snprintf(why, whyn, "item drop motion differ");
        }
        else if (g->item != w->item || g->damage != w->damage || g->count != w->count)
        {
            snprintf(why, whyn, "item drop stack want (%d,%d,%d) got (%d,%d,%d)",
                     w->item, w->damage, w->count, g->item, g->damage, g->count);
        }
    }
}

static void ents_diff(const struct sc_ctx *c, const struct ent_rec *w, uint32_t wn, char *why, size_t whyn)
{
    if ((uint32_t)c->nents != wn)
    {
        if (c->nents > 0)
            snprintf(why, whyn, "%u entities recorded, the native step made %d (first native kind %d at (%f,%f,%f) item %d)",
                     wn, c->nents, c->ents[0].kind, c->ents[0].x, c->ents[0].y, c->ents[0].z, c->ents[0].item);
        else
            snprintf(why, whyn, "%u entities recorded, the native step made 0", wn);
        return;
    }

    for (uint32_t i = 0; i < wn; ++i)
    {
        size_t n0 = strlen(why);

        if (n0 > 0 && n0 < whyn - 2) { why[n0++] = ' '; why[n0] = 0; }

        ent_diff(c, &w[i], i, why + n0, whyn - n0);
    }
}

/* ------------------------------------------------------------ tile entities */

struct te
{
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

static int te_order_cmp(const void *a, const void *b)
{
    return te_order(a, b);
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

            struct tile_entity *te = c->tes.v[j];

            v[count].x = te->x;
            v[count].y = te->y;
            v[count].z = te->z;
            v[count].kind = te->kind;
            v[count].text = te_render(te);
            ++count;
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
        char *wt = malloc((size_t)wlen + 1);
        char why[256];

        memcpy(wt, wtext, wlen);
        wt[wlen] = 0;

        nbt *a = nbt_parse(wt), *b = nbt_parse(text);

        if (a == NULL || b == NULL || nbt_diff(a, b, why, sizeof why))
            snprintf(what, whyn, "tile entity (%d,%d,%d) kind %d: %.150s", x, y, z, kind, why);
        else
            snprintf(what, whyn, "tile entity (%d,%d,%d) kind %d: want %.90s got %.90s", x, y, z, kind, wt, text);

        if (a) nbt_free(a);
        if (b) nbt_free(b);
        free(wt);
        return 1;
    }

    return 0;
}

/* The step's record: the entries the step left different from the step before,
 * the same merge the probe wrote them with. */
static int check_tiles(const unsigned char *rec, size_t rlen, size_t *off, struct world *w, struct te **prev,
                       int *nprev, char *what, size_t whyn)
{
    if (rlen - *off < 4)
    {
        snprintf(what, whyn, "the record ends early");
        return 1;
    }

    uint32_t want = le32(rec + *off);
    *off += 4;

    int ncur = 0;
    struct te *curte = te_snapshot(w, &ncur);
    int i = 0, j = 0;
    uint32_t got = 0;
    int bad = 0;

    while ((i < *nprev || j < ncur) && !bad)
    {
        int cmp = i == *nprev ? 1 : j == ncur ? -1 : te_order(&(*prev)[i], &curte[j]);

        if (cmp < 0)
        {
            bad = entry_matches(rec, rlen, off, (*prev)[i].x, (*prev)[i].y, (*prev)[i].z, 0, "", what, whyn);
            ++i;
        }
        else if (cmp > 0)
        {
            bad = entry_matches(rec, rlen, off, curte[j].x, curte[j].y, curte[j].z, curte[j].kind, curte[j].text,
                                what, whyn);
            ++j;
        }
        else
        {
            if (strcmp((*prev)[i].text, curte[j].text) != 0)
            {
                bad = entry_matches(rec, rlen, off, curte[j].x, curte[j].y, curte[j].z, curte[j].kind,
                                    curte[j].text, what, whyn);
                if (!bad) ++got;
            }

            ++i;
            ++j;
        }

        if (!bad && cmp != 0) ++got;
    }

    if (!bad && got != want)
    {
        /* name the first entry whose presence differs, for the counts case */
        i = 0, j = 0;

        while (i < *nprev || j < ncur)
        {
            int cmp = i == *nprev ? 1 : j == ncur ? -1 : te_order(&(*prev)[i], &curte[j]);

            if (cmp < 0) ++i;
            else if (cmp > 0)
            {
                snprintf(what, whyn, "the record has %u entries, the world changed %u (new (%d,%d,%d) kind %d %.90s)",
                         want, got, curte[j].x, curte[j].y, curte[j].z, curte[j].kind, curte[j].text);
                bad = 1;
                break;
            }
            else ++i, ++j;
        }

        if (!bad)
        {
            for (i = 0; i < *nprev; ++i)
                if (bsearch(&(*prev)[i], curte, (size_t)ncur, sizeof *curte, te_order_cmp) == NULL) break;

            if (i < *nprev)
                snprintf(what, whyn, "the record has %u entries, the world changed %u (removed (%d,%d,%d) kind %d %.90s)",
                         want, got, (*prev)[i].x, (*prev)[i].y, (*prev)[i].z, (*prev)[i].kind, (*prev)[i].text);
            else
                snprintf(what, whyn, "the record has %u entries, the world changed %u (removed an entry)", want, got);

            bad = 1;
        }
    }

    for (int k = 0; k < *nprev; ++k) free((*prev)[k].text);
    free(*prev);
    *prev = curte;
    *nprev = ncur;
    return bad;
}

/* ------------------------------------------------------------ main */

static int zlib_read(const char *path, unsigned char **out, size_t *outn)
{
    FILE *f = fopen(path, "rb");

    if (!f) return 0;

    size_t cap = 1 << 22, n = 0;
    unsigned char *v = malloc(cap);
    unsigned char inb[65536];
    int r, done = 0;

    z_stream zs;

    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) { fclose(f); free(v); return 0; }

    while (!done && (r = (int)fread(inb, 1, sizeof inb, f)) > 0)
    {
        zs.next_in = inb;
        zs.avail_in = (uInt)r;

        while (zs.avail_in != 0)
        {
            if (n + 65536 > cap)
            {
                cap *= 2;
                v = realloc(v, cap);
            }

            zs.next_out = v + n;
            zs.avail_out = 65536;
            int zr = inflate(&zs, Z_NO_FLUSH);

            n += 65536 - zs.avail_out;

            if (zr == Z_STREAM_END) { done = 1; break; }

            if (zr != Z_OK && zr != Z_BUF_ERROR) break;
        }
    }

    inflateEnd(&zs);
    fclose(f);
    *out = v;
    *outn = n;
    return 1;
}

/* The block half of every structure type, by the name the probe records
 * (the native type's name in structure.c). A structure lane adds its line. */
struct block_half
{
    const char *name;
    const struct structure_type *type;
    int (*blocks)(struct sc_ctx *c, struct piece *p);
};

static const struct block_half block_halves[] = {
    {"Mineshaft", &structure_mineshaft, mineshaft_blocks},
    {"Temple", &structure_temple, temple_blocks},
    {"Village", &structure_village, village_blocks},
    {"Stronghold", &structure_stronghold, stronghold_blocks},
    {"Fortress", &structure_fortress, fortress_blocks},
    {NULL, NULL, NULL}
};

int main(int argc, char **argv)
{
    probe_args(&argc, argv);
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_sblocks PROBE_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1024];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    char kind[64];

    manifest_str(manifest, "kind", kind, sizeof kind);
    if (strcmp(kind, "sblocks") != 0)
    {
        printf("SKIP %s: kind %s is not an sblocks probe\n", dir, kind);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int steps = (int)manifest_int(manifest, "steps");

    /* the probe's structure type and world; older probes recorded
     * mineshafts in the overworld and carry neither */
    char sname[64] = "Mineshaft";
    int dim = 0;

    if (strstr(manifest, "\"structure\":\"")) manifest_str(manifest, "structure", sname, sizeof sname);
    if (strstr(manifest, "\"dim\":")) dim = (int)manifest_int(manifest, "dim");

    const struct block_half *half = NULL;

    for (int i = 0; block_halves[i].name; ++i)
        if (!strcmp(block_halves[i].name, sname)) half = &block_halves[i];

    if (half == NULL)
    {
        printf("FAIL %s: no native block half for structure %s\n", dir, sname);
        return 1;
    }
    int m_x0 = (int)manifest_int(manifest, "x0");
    int m_z0 = (int)manifest_int(manifest, "z0");
    int m_x1 = (int)manifest_int(manifest, "x1");
    int m_z1 = (int)manifest_int(manifest, "z1");
    int nstarts = 0;
    int *scx = NULL, *scz = NULL;

    /* the manifest's "starts": [ [cx,cz], ... ] in run order */
    {
        const char *p = strstr(manifest, "\"starts\":[");

        if (p == NULL)
        {
            fprintf(stderr, "%s: no starts list\n", dir);
            return 2;
        }

        p += strlen("\"starts\":[");
        int cap = 8;
        scx = malloc((size_t)cap * sizeof *scx);
        scz = malloc((size_t)cap * sizeof *scz);

        while (*p && *p != ']')
        {
            if (*p == '[')
            {
                if (nstarts == cap)
                {
                    cap *= 2;
                    scx = realloc(scx, (size_t)cap * sizeof *scx);
                    scz = realloc(scz, (size_t)cap * sizeof *scz);
                }

                if (sscanf(p, "[%d,%d]", &scx[nstarts], &scz[nstarts]) != 2)
                {
                    fprintf(stderr, "%s: bad start pair\n", dir);
                    return 2;
                }

                ++nstarts;
                p = strchr(p, ']');     /* skip past the pair just parsed */

                if (p == NULL) break;
            }

            ++p;
        }
    }


    int nchunks = 0, *lcz = NULL;
    int *lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    /* the steps, in the order the probe wrote them */
    snprintf(path, sizeof path, "%s/steps.bin", dir);
    size_t slen;
    unsigned char *sbuf = probe_read_file(path, &slen);

    if (slen != (size_t)steps * 32)
    {
        fprintf(stderr, "%s: steps.bin is %zu bytes, the manifest says %d steps\n", dir, slen, steps);
        return 2;
    }

    struct step_rec *recs = malloc((size_t)steps * sizeof *recs);

    for (int i = 0; i < steps; ++i)
    {
        const unsigned char *s = sbuf + (size_t)i * 32;

        recs[i].start_cx = (int)le32(s + 4);
        recs[i].start_cz = (int)le32(s + 8);
        recs[i].cx = (int)le32(s + 12);
        recs[i].cz = (int)le32(s + 16);
        recs[i].writes = le32(s + 20);
        recs[i].hash = le64(s + 24);
    }

    snprintf(path, sizeof path, "%s/writes.bin", dir);
    size_t wlen;
    unsigned char *wbuf = probe_read_file(path, &wlen);

    /* tile entities and entities, when the probe recorded any */
    int have_tes = strstr(manifest, "\"tile_entities\":") != NULL;
    unsigned char *tebuf = NULL;
    size_t telen = 0, teoff = 0;

    if (have_tes)
    {
        char tefile[256];

        manifest_str(manifest, "tile_entities", tefile, sizeof tefile);
        snprintf(path, sizeof path, "%s/%s", dir, tefile);
        tebuf = probe_read_file(path, &telen);
    }

    char efile[256];

    manifest_str(manifest, "entity_layout", efile, sizeof efile);   /* presence only */
    (void)efile;
    snprintf(path, sizeof path, "%s/entities.bin", dir);
    size_t elen;
    unsigned char *ebuf = probe_read_file(path, &elen);

    /* the whole region after the last step: compared chunk by chunk at the
     * end (cmp_final streams it), and read whole only to explain a failed
     * hash, since a region can hold thousands of chunks */
    char fpath[1100];
    snprintf(fpath, sizeof fpath, "%s/final.bin.gz", dir);
    unsigned char *fbuf = NULL;
    size_t flen = 0;

    /* the world: every chunk of the region, in the probe's load order */
    struct world w;

    world_init(&w, seed);
    w.dim = dim;

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    w.on_block = on_write;
    w.on_block_ctx = NULL;

    /* Det's OTHER role streams and World.rand, snapshotted before the first
     * step: the generation moves them only through the entities it builds.
     * det_load sets the streams' 48-bit states directly, the way a Java
     * snapshot hands them over; the entity-id counter goes through det_init's
     * reset, so it is set after. */
    det_state det;

    det_init(&det);

    {
        uint64_t seeder[DET_ROLES] = {0}, mathv[DET_ROLES] = {0};
        int32_t next_id[DET_ROLES] = {0};

        seeder[DET_OTHER] = (uint64_t)manifest_int(manifest, "seeder_other");
        mathv[DET_OTHER] = (uint64_t)manifest_int(manifest, "math_other");
        next_id[DET_OTHER] = (int32_t)manifest_int(manifest, "next_id_other");
        det_load(&det, seed, seeder, mathv, next_id);
    }

    /* the snapshot's value IS the 48-bit state; setSeed would scramble it */
    jrand world_rand;

    world_rand.seed = (uint64_t)manifest_int(manifest, "worldrand") & JR_MASK;

    unsigned char *hashbuf = malloc(CHUNK_BYTES);
    size_t woff = 0, eoff = 0;
    int fail = 0;

    struct start_list starts;

    memset(&starts, 0, sizeof starts);
    structure_walk(half->type, seed, m_x0, m_z0, m_x1, m_z1, &starts);

    /* the probe kept only the starts wholly inside its region; the walk
     * covers every candidate chunk, so the recorded starts are matched by
     * their chunk pair, in the record's order */
    int *slot = malloc((size_t)(nstarts ? nstarts : 1) * sizeof *slot);

    for (int i = 0; i < nstarts; ++i) slot[i] = -1;

    for (int i = 0; i < nstarts; ++i)
        for (int j = 0; j < starts.n; ++j)
            if (starts.v[j]->chunk_x == scx[i] && starts.v[j]->chunk_z == scz[i]) { slot[i] = j; break; }

    for (int i = 0; i < nstarts; ++i)
        if (slot[i] < 0)
        {
            printf("FAIL %s: the walk has no start at (%d,%d)\n", dir, scx[i], scz[i]);
            return 1;
        }

    /* per start: the walk's chunk set, cx-major with cz inner, one sc_ctx
     * carried across the start's steps */
    int si = 0;
    int cur_cx = 1 << 30, cur_cz = 1 << 30;
    struct start *run = NULL;
    struct sc_ctx c;

    memset(&c, 0, sizeof c);
    c.w = &w;
    c.det = &det;
    c.world_rand = &world_rand;
    c.piece_fn = half->blocks;
    c.box.minY = 0;
    c.box.maxY = 255;

    struct te *prevte = NULL;
    int nprevte = 0;


    for (int st = 0; st < steps && !fail; ++st)
    {
        struct step_rec *r = &recs[st];

        if (r->start_cx != cur_cx || r->start_cz != cur_cz)
        {
            if (si >= starts.n)
            {
                printf("FAIL %s step %d: the record names start (%d,%d), the walk has %d\n", dir, st,
                       r->start_cx, r->start_cz, starts.n);
                ++fail;
                break;
            }

            /* the record's start chunks are in run order */
            while (si < nstarts && (scx[si] != r->start_cx || scz[si] != r->start_cz)) ++si;

            if (si >= nstarts)
            {
                printf("FAIL %s step %d: the record's start (%d,%d) is not in the manifest\n", dir, st,
                       r->start_cx, r->start_cz);
                ++fail;
                break;
            }

            run = starts.v[slot[si]];
            cur_cx = r->start_cx;
            cur_cz = r->start_cz;
            sc_free_ents(&c);
        }

        cur.base = wbuf + woff;
        cur.want = r->writes;
        cur.got = 0;
        cur.overflow = 0;
        cur.bad = 0;

        /* the population Random, seeded as populate seeds it */
        jrand pr;

        {
            jrand t;

            jr_seed(&t, seed);
            int64_t var7 = jr_long(&t) / 2L * 2L + 1L;
            int64_t var9 = jr_long(&t) / 2L * 2L + 1L;
            jr_seed(&pr, (int64_t)((uint64_t)(int64_t)r->cx * (uint64_t)var7 + (uint64_t)(int64_t)r->cz * (uint64_t)var9)
                         ^ (int64_t)seed);
        }

        c.rand = &pr;
        c.box.minX = (r->cx << 4) + 8;
        c.box.minZ = (r->cz << 4) + 8;
        c.box.maxX = (r->cx << 4) + 23;
        c.box.maxZ = (r->cz << 4) + 23;

        /* the entity record is per step */
        sc_free_ents(&c);
        sc_generate(&c, run);

        if (cur.bad)
        {
            printf("FAIL %s step %d (start %d,%d chunk %d,%d): %s\n", dir, st, r->start_cx, r->start_cz, r->cx,
                   r->cz, diff_what);
            ++fail;
            break;
        }

        if (cur.got != r->writes)
        {
            printf("FAIL %s step %d (start %d,%d chunk %d,%d): %u writes, the oracle recorded %u%s%s\n", dir, st,
                   r->start_cx, r->start_cz, r->cx, r->cz, cur.got, r->writes,
                   cur.overflow ? ", " : "", cur.overflow ? diff_what : "");
            ++fail;
            break;
        }

        uint64_t got_hash = probe_hash_due(st, steps) ? hash_around(&w, r->cx, r->cz, hashbuf) : r->hash;

        if (got_hash != r->hash)
        {
            printf("FAIL %s step %d (start %d,%d chunk %d,%d): hash want %016llx got %016llx\n", dir, st,
                   r->start_cx, r->start_cz, r->cx, r->cz, (unsigned long long)r->hash,
                   (unsigned long long)got_hash);

            /* the region after the last step, from final.bin.gz, for the 3x3
             * chunks the hash covers: steps after this one wrote nothing near
             * them, so the final bytes show where the two sides differ */
            if (fbuf == NULL) zlib_read(fpath, &fbuf, &flen);

            if (fbuf != NULL && flen > 0)
            {
                size_t fo = 0;

                printf("  final.bin.gz is %zu bytes (%d chunks)\n", flen, (int)(flen / (8 + CHUNK_BYTES + 257)));

                while (fo + 8 + CHUNK_BYTES + 257 <= flen)
                {
                    int fcx = (int)le32(fbuf + fo), fcz = (int)le32(fbuf + fo + 4);

                    if (fcx >= r->cx - 1 && fcx <= r->cx + 1 && fcz >= r->cz - 1 && fcz <= r->cz + 1)
                    {
                        struct chunk *c = world_chunk(&w, fcx, fcz);

                        if (c != NULL)
                        {
                            unsigned char *wantb = malloc(CHUNK_BYTES);

                            memcpy(wantb, fbuf + fo + 8, CHUNK_BYTES);

                            char what[128];
                            int d = cmp_chunk(wantb, c, what, sizeof what);

                            printf("  chunk (%d,%d): %s\n", fcx, fcz, d ? what : "identical to the final bytes");
                            free(wantb);
                        }
                    }

                    fo += 8 + CHUNK_BYTES + 256 + 1;
                }
            }

            ++fail;
            break;
        }

        /* the entities the step built, in order */
        {
            uint32_t wn = 0;
            struct ent_rec *wents = NULL;

            if (read_ents(ebuf, elen, &eoff, &wents, &wn))
            {
                char why[256];

                memset(why, 0, sizeof why);
                ents_diff(&c, wents, wn, why, sizeof why);
                if (why[0])
                {
                    printf("FAIL %s step %d (start %d,%d chunk %d,%d): entities: %s\n", dir, st, r->start_cx,
                           r->start_cz, r->cx, r->cz, why);
                    for (char *q = why; *q; ++q) if (*q < 32) printf("  byte %ld: %d\n", (long)(q - why), *q);
                    ++fail;
                }
                ent_free(wents, wn);
            }
        }

        if (fail) break;

        woff += (size_t)r->writes * 16;

        if (tebuf != NULL
            && check_tiles(tebuf, telen, &teoff, &w, &prevte, &nprevte, diff_what, sizeof diff_what))
        {
            printf("FAIL %s step %d (start %d,%d chunk %d,%d): tile entities: %s\n", dir, st, r->start_cx,
                   r->start_cz, r->cx, r->cz, diff_what);
            ++fail;
            break;
        }
    }

    sc_free_ents(&c);
    start_list_free(&starts);

    if (!fail) fail = cmp_final(dir, fpath, &w, lcx, lcz, nchunks);

    if (!fail)
    {
        printf("PASS %s: %d/%d steps over %d starts, seed %lld\n", dir, steps, steps, nstarts, (long long)seed);
        if (tebuf != NULL)
            printf("     tile entities: %d in the region after the last step\n", nprevte);
    }

    free(hashbuf);
    free(wbuf);
    free(ebuf);
    free(tebuf);
    free(fbuf);
    free(recs);
    free(lcx);
    free(lcz);
    free(scx);
    free(scz);
    free(slot);
    free(manifest);
    world_free(&w);

    if (fail) probe_localize(argv[0], dir);

    return fail;
}
