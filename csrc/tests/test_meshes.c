/* The native chunk mesher against the oracle's mesh probes: every record of
 * out/java/meshes/<name>/sections.bin is rebuilt from that directory's
 * chunks.bin and compared int for int, in the order the probe wrote them.
 *
 * Two modes. The default walks the radius box of MeshProbe's spawn scenes in
 * the probe's order and complains when the mesher draws anything the probe did
 * not record. The scene mode (MeshScene manifests, which carry "scene":true)
 * walks the records in file order instead, and sections whose blocks hold a
 * render type the native mesher does not port yet are reported as skipped by
 * type, not failed. Both print the vertices matched per render type.
 *
 * A directory recorded before the world dump existed (the original mp-s1 and
 * mp-s42) has no chunks.bin and nothing to check; it is reported and skipped.
 */
#include "../engine/render_blocks.h"
#include "../engine/render_blocks_int.h"
#include "../engine/blocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FIELD[8] = {"x", "y", "z", "u", "v", "color", "normal", "brightness"};

/* render types this build ports; getRenderType() -1 reads as 255 (draws
 * nothing, like 22 chests) */
static int ported_rt[256];
static long long rt_matched[256];      /* vertices matched, by render type */

static void ported_rt_init(void)
{
    static const int RT[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
        20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37,
        38, 39, 40, 41, 255
    };

    for (int i = 0; i < (int)(sizeof RT / sizeof RT[0]); ++i) ported_rt[RT[i]] = 1;
}

struct record {
    int32_t cx, cz, section, pass, count, flags;
    const int32_t *v;
};

static int32_t *slurp(const char *dir, const char *name, long *len)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    int32_t *b = malloc((size_t)*len + 8);
    if (!b || fread(b, 1, (size_t)*len, f) != (size_t)*len) { fclose(f); free(b); return NULL; }
    fclose(f);
    return b;
}

static void show_vertex(int idx, const int32_t *v)
{
    printf("      vertex %d:", idx);

    for (int i = 0; i < 8; ++i)
    {
        if (i == 0 || i == 1 || i == 2 || i == 3 || i == 4)
        {
            float f;
            memcpy(&f, &v[i], 4);
            printf(" %s=%.9g(%08x)", FIELD[i], (double)f, (uint32_t)v[i]);
        }
        else
        {
            printf(" %s=%d(%08x)", FIELD[i], v[i], (uint32_t)v[i]);
        }
    }

    printf("\n");
}

/** The render types in this section that no ported renderer draws; 1 if any.
 * Each type present counts one skipped section, not one per block. */
static int section_unported_types(const struct rb_mesher *m, int cx, int cz, int section, long long *skipped)
{
    int found[256];
    memset(found, 0, sizeof found);

    for (int x = 0; x < 16; ++x)
    {
        for (int z = 0; z < 16; ++z)
        {
            for (int y = section << 4; y < (section << 4) + 16; ++y)
            {
                int id = rb_world_block(m->w, (cx << 4) + x, y, (cz << 4) + z);

                if (strcmp(MATERIALS[BLOCKS[id].material].name, "air") == 0) continue;

                int rt = BLOCKS[id].render_type;

                if (!ported_rt[rt]) found[rt] = 1;
            }
        }
    }

    int any = 0;

    for (int rt = 0; rt < 256; ++rt)
    {
        if (found[rt])
        {
            ++skipped[rt];
            any = 1;
        }
    }

    return any;
}

/** Rebuild one section and pass and compare it against the oracle's record. */
static int compare_record(const char *dir, struct rb_mesher *m, const struct record *r,
                          int cx, int cz, int section, int pass, int *bad, char *first_bad)
{
    struct rb_tess *tess = m->t;
    rb_mesh_section(m, cx, cz, section, pass);

    if (tess->dropped)
    {
        fprintf(stderr, "%s: the vertex buffer overflowed at %d,%d section %d pass %d\n",
            dir, cx, cz, section, pass);
        return 2;
    }

    int has = (tess->has_texture ? 1 : 0) | (tess->has_color ? 2 : 0)
        | (tess->has_brightness ? 4 : 0) | (tess->has_normals ? 8 : 0);

    if (tess->vertex_count != r->count || has != r->flags)
    {
        printf("%s: %d,%d section %d pass %d: %d vertices flags %d, oracle %d flags %d\n",
            dir, cx, cz, section, pass, tess->vertex_count, has, r->count, r->flags);

        int n = tess->vertex_count < r->count ? tess->vertex_count : r->count;

        for (int i = 0; i < n; ++i)
        {
            const int32_t *a = tess->raw + (size_t)i * 8;
            const int32_t *b = r->v + (size_t)i * 8;

            if (memcmp(a, b, 32) == 0) continue;

            printf("    first differing vertex %d of %d/%d: block %d %s meta %d render type %d at %d,%d,%d\n",
                i, tess->vertex_count, r->count,
                tess->dbg[((size_t)i) * 4], BLOCKS[tess->dbg[((size_t)i) * 4]].name,
                tess->dbg[((size_t)i) * 4 + 1], tess->dbg[((size_t)i) * 4 + 2],
                (tess->dbg[((size_t)i) * 4 + 3] >> 20) - 2048, (tess->dbg[((size_t)i) * 4 + 3] >> 12) & 255,
                (tess->dbg[((size_t)i) * 4 + 3] & 4095) - 2048);
            show_vertex(i, a);
            show_vertex(i, b);
            break;
        }

        if (!first_bad[0])
            snprintf(first_bad, 512, "%d,%d section %d pass %d count %d/%d",
                cx, cz, section, pass, tess->vertex_count, r->count);

        return 1;
    }

    for (int i = 0; i < tess->vertex_count; ++i)
    {
        const int32_t *a = tess->raw + (size_t)i * 8;
        const int32_t *b = r->v + (size_t)i * 8;

        for (int k = 0; k < 8; ++k)
        {
            /* Tessellator leaves disabled attributes in its reused raw buffer.
             * Their old bytes are not submitted to GL and depend on earlier
             * unrelated draws. Compare the attributes this record enables. */
            if ((k == 3 || k == 4) && !(r->flags & 1)) continue;
            if (k == 5 && !(r->flags & 2)) continue;
            if (k == 6 && !(r->flags & 8)) continue;
            if (k == 7 && !(r->flags & 4)) continue;
            if (a[k] == b[k]) continue;

            int32_t dbg = tess->dbg[((size_t)i) * 4 + 3];
            printf("%s: %d,%d section %d pass %d vertex %d field %s: native %d(%08x) oracle %d(%08x); block %d %s meta %d render type %d at %d,%d,%d\n",
                dir, cx, cz, section, pass, i, FIELD[k], (uint32_t)a[k], (uint32_t)a[k],
                (uint32_t)b[k], (uint32_t)b[k],
                tess->dbg[((size_t)i) * 4], BLOCKS[tess->dbg[((size_t)i) * 4]].name,
                tess->dbg[((size_t)i) * 4 + 1], tess->dbg[((size_t)i) * 4 + 2],
                (dbg >> 20) - 2048, (dbg >> 12) & 255, (dbg & 4095) - 2048);
            show_vertex(i, a);
            show_vertex(i, b);

            if (!first_bad[0])
                snprintf(first_bad, 512, "%d,%d section %d pass %d vertex %d %s",
                    cx, cz, section, pass, i, FIELD[k]);

            return 1;
        }
    }

    /* every vertex matched; tally by render type */
    for (int i = 0; i < tess->vertex_count; ++i)
    {
        ++rt_matched[tess->dbg[((size_t)i) * 4 + 2] & 255];
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: test_meshes <probe dir>\n");
        return 2;
    }

    const char *dir = argv[1];
    char probe[1200];
    snprintf(probe, sizeof probe, "%s/chunks.bin", dir);
    FILE *f = fopen(probe, "rb");
    if (!f)
    {
        printf("%s: no chunks.bin (recorded before the world dump); nothing to check\n", dir);
        return 0;
    }
    fclose(f);

    /* scene mode: MeshScene manifests, records walked in file order */
    char mpath[1200];
    snprintf(mpath, sizeof mpath, "%s/manifest.json", dir);
    static char man[1 << 18];
    int scene = 0;
    FILE *mf = fopen(mpath, "rb");

    if (mf)
    {
        size_t n = fread(man, 1, sizeof man - 1, mf);
        man[n] = 0;
        fclose(mf);
        scene = strstr(man, "\"scene\":true") != NULL;
    }

    int radius, pcx, pcz, px, py, pz;
    if (rb_manifest_load(dir, &radius, &pcx, &pcz, &px, &py, &pz) != 0) return 2;

    struct rb_table tab;
    struct rb_atlas atlas;
    struct rb_world world;
    if (rb_table_load(&tab, dir) != 0) return 2;
    if (rb_atlas_load(&atlas, dir) != 0) return 2;
    if (rb_world_load(&world, dir) != 0) return 2;
    world.origin_cx = pcx;
    world.origin_cz = pcz;

    if (world.margin != radius + 1)
    {
        fprintf(stderr, "%s: chunks.bin holds margin %d, the manifest says radius %d\n", dir, world.margin, radius);
        return 2;
    }

    struct rb_tess tess;
    if (rb_tess_init(&tess, RB_TESS_CAP) != 0) return 2;

    struct rb_mesher m;
    if (rb_mesher_init(&m, &tab, &atlas, &world, &tess, px, py, pz) != 0) return 2;

    long len = 0;
    int32_t *sections = slurp(dir, "sections.bin", &len);

    if (!sections)
    {
        fprintf(stderr, "%s: cannot read sections.bin\n", dir);
        return 2;
    }

    int nrec = 0;
    int rec_cap = 4096;
    struct record *recs = malloc((size_t)rec_cap * sizeof *recs);
    int32_t *p = sections;

    while ((char *)p < (char *)sections + len)
    {
        if ((char *)p + 24 > (char *)sections + len)
        {
            fprintf(stderr, "%s: sections.bin ends mid record\n", dir);
            return 2;
        }

        if (nrec == rec_cap)
        {
            rec_cap *= 2;
            recs = realloc(recs, (size_t)rec_cap * sizeof *recs);
        }

        struct record *r = &recs[nrec++];
        r->cx = p[0]; r->cz = p[1]; r->section = p[2]; r->pass = p[3];
        r->count = p[4]; r->flags = p[5];
        r->v = p + 6;
        p += 6 + (size_t)r->count * 8;

        if (r->count < 0 || r->count % 4 != 0)
        {
            fprintf(stderr, "%s: record %d has a bad vertex count %d\n", dir, nrec - 1, r->count);
            return 2;
        }
    }

    ported_rt_init();

    int bad = 0, compared = 0;
    long long vertices = 0, file_vertices = 0;
    char first_bad[512] = "";
    long long rt_skipped[256];
    memset(rt_skipped, 0, sizeof rt_skipped);

    if (scene)
    {
        /* the records in file order; a section holding an unported render type
         * is reported as skipped, not failed */
        for (int rec = 0; rec < nrec && bad < 20; ++rec)
        {
            struct record *r = &recs[rec];

            if (section_unported_types(&m, r->cx, r->cz, r->section, rt_skipped))
            {
                file_vertices += r->count;
                continue;
            }

            int rc = compare_record(dir, &m, r, r->cx, r->cz, r->section, r->pass, &bad, first_bad);

            if (rc == 2) return 2;

            if (rc == 0)
            {
                ++compared;
                vertices += r->count;
                file_vertices += r->count;
            }
            else
            {
                ++bad;   /* compare_record already printed and named the difference */
            }
        }

        if (bad == 0)
        {
            printf("%s: scene: %d records compared, %lld vertices; skipped sections by unported type:",
                dir, compared, vertices);

            for (int rt = 0; rt < 256; ++rt)
            {
                if (rt_skipped[rt]) printf(" %d:%lld", rt == 255 ? -1 : rt, rt_skipped[rt]);
            }

            printf("\n");
            printf("%s: scene: vertices matched by render type:", dir);

            for (int rt = 0; rt < 256; ++rt)
            {
                if (rt_matched[rt]) printf(" %d:%lld", rt == 255 ? -1 : rt, rt_matched[rt]);
            }

            printf("\n");
        }
    }
    else
    {
        int rec = 0;

        for (int dx = -radius; dx <= radius && bad < 20; ++dx)
        {
            for (int dz = -radius; dz <= radius && bad < 20; ++dz)
            {
                for (int s = 0; s < 16 && bad < 20; ++s)
                {
                    for (int pass = 0; pass < 2 && bad < 20; ++pass)
                    {
                        int cx = pcx + dx, cz = pcz + dz;
                        rb_mesh_section(&m, cx, cz, s, pass);
                        vertices += tess.vertex_count;

                        if (tess.dropped)
                        {
                            fprintf(stderr, "%s: the vertex buffer overflowed at %d,%d section %d pass %d\n", dir, cx, cz, s, pass);
                            return 2;
                        }

                        int in_file = rec < nrec && recs[rec].cx == cx && recs[rec].cz == cz
                            && recs[rec].section == s && recs[rec].pass == pass;

                        if (!in_file)
                        {
                            if (tess.vertex_count != 0)
                            {
                                printf("%s: EXTRA %d,%d section %d pass %d: %d vertices the oracle did not record\n",
                                    dir, cx, cz, s, pass, tess.vertex_count);
                                ++bad;
                            }

                            continue;
                        }

                        struct record *r = &recs[rec];
                        file_vertices += r->count;
                        int rc = compare_record(dir, &m, r, cx, cz, s, pass, &bad, first_bad);

                        if (rc == 2) return 2;

                        if (rc == 0)
                        {
                            ++compared;
                        }
                        else
                        {
                            ++bad;   /* compare_record already printed and named the difference */
                        }

                        ++rec;
                    }
                }
            }
        }

        if (rec != nrec)
        {
            printf("%s: sections.bin holds %d records, the mesher walked %d\n", dir, nrec, rec);
            ++bad;
        }
    }

    printf("%s: radius %d camera %d,%d,%d: %d records, %lld vertices (oracle %lld) compared, %d bad\n",
        dir, radius, px, py, pz, compared, vertices, file_vertices, bad);

    if (bad) printf("%s: first difference %s\n", dir, first_bad);

    free(sections);
    free(recs);
    rb_mesher_free(&m);
    rb_tess_free(&tess);
    rb_table_free(&tab);
    rb_atlas_free(&atlas);
    rb_world_free(&world);
    return bad ? 1 : 0;
}
