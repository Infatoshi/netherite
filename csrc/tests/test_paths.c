/* Gate: the native pathfinder against the oracle's path probe
 * (oracle/harness/netherite/oracle/PathProbe.java; recorded with
 *   make -C oracle run SEED=N CLASS=PathProbe NAME=NAME \
 *        CMD='{"out":".../out/java/paths/NAME","cx":3000,"cz":3000,"cases":N}')
 * and run with   make -C csrc test   on every directory under out/java/paths.
 *
 * The test rebuilds the region from final.bin.gz's own chunk order (the
 * placements in shapes.bin are already inside those chunks), replays the case
 * draw stream from the manifest's case_draws rule, and calls the native
 * pathfinder per case with the recorded entity size, position, target and
 * flags. The recorded path (or null) must match point for point.
 *
 * The per-case 3x3 hash other probes record is not part of this one
 * (pathfinding writes nothing); a placement that landed differently changes
 * the path or the final region compare instead. --hash-all is accepted and
 * ignored, so probe_localize keeps working. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "probe.h"
#include "../engine/jrand.h"
#include "../engine/pathfind.h"
#include "../engine/world.h"

/* The oracle's entity sizes (width, height), in PathProbe.SIZES order. */
static const float SIZES[][2] = {
    {0.6F, 1.8F}, {0.9F, 1.3F}, {0.9F, 0.9F}, {1.4F, 0.9F}, {0.4F, 0.7F},
    {0.6F, 2.9F}, {1.4F, 2.9F}, {0.6F, 0.6F}, {2.4F, 2.4F},
};

#define N_SIZES (int)(sizeof SIZES / sizeof SIZES[0])

/* The search distances, in PathProbe.DISTS order. */
static const float DISTS[3] = {16.0F, 32.0F, 40.0F};

#define MAX_POINTS 4096

static int le_i32(const unsigned char *p)
{
    return (int)le32(p);
}

static double le_double(const unsigned char *p)
{
    uint64_t v = 0;

    for (int i = 7; i >= 0; --i) v = v << 8 | p[i];

    double d;
    memcpy(&d, &v, sizeof d);
    return d;
}

static inline void *probe_read_file2(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL)
    {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n + 1);

    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
    {
        fprintf(stderr, "short read on %s\n", path);
        exit(2);
    }

    fclose(f);
    buf[n] = 0;
    *len = (size_t)n;
    return buf;
}

/* The case replay. Returns 1 on failure, stopping at the first mismatch. */
static int run_cases(const char *dir, const unsigned char *cases, size_t clen, int ncases, struct world *w,
                     int bx0, int bz0, int width, jrand *r)
{
    struct pf f;
    pf_init(&f, w);

    static int out[3 * 4096];
    size_t off = 0;

    for (int i = 0; i < ncases; ++i)
    {
        if (off + 73 > clen)
        {
            printf("FAIL %s: cases.bin ends early at case %d\n", dir, i);
            pf_free(&f);
            return 1;
        }

        const unsigned char *c = cases + off;
        int size_idx = c[0];
        int method = c[1];
        int flags = c[2];
        int dist_idx = c[3];

        if (size_idx >= N_SIZES || dist_idx >= 3)
        {
            printf("FAIL %s case %d: size %d dist %d out of range\n", dir, i, size_idx, dist_idx);
            pf_free(&f);
            return 1;
        }

        float width_f = SIZES[size_idx][0], height_f = SIZES[size_idx][1];
        float max_dist = DISTS[dist_idx];
        int di = (int)max_dist;

        /* the recorded header fields the replay checks */
        int want_sx = le_i32(c + 4), want_sy = le_i32(c + 8), want_sz = le_i32(c + 12);
        int want_result = c[68];
        int want_npts = le_i32(c + 69);

        /* the replay's draws, in case_draws order */
        int size_draw = jr_int_n(r, N_SIZES);

        if (size_draw != size_idx)
        {
            printf("FAIL %s case %d: the draw stream gives size %d, cases.bin has %d\n", dir, i, size_draw, size_idx);
            pf_free(&f);
            return 1;
        }

        int sx = bx0 + (int)jr_int_n(r, width);
        int sz = bz0 + (int)jr_int_n(r, width);
        (void)jr_int_n(r, 3); /* sy = getHeightValue(sx, sz) - 1 + this */
        int method_draw = (int)jr_int_n(r, 2);
        int dist_draw = (int)jr_int_n(r, 3);

        if (method_draw != method || dist_draw != dist_idx)
        {
            printf("FAIL %s case %d: the draw stream gives method %d dist %d, cases.bin has %d %d\n", dir, i, method_draw,
                   dist_draw, method, dist_idx);
            pf_free(&f);
            return 1;
        }

        int di_draw = (int)DISTS[dist_draw];

        if (di_draw != di)
        {
            printf("FAIL %s case %d: dist draw %d, cases.bin has %d\n", dir, i, di_draw, di);
            pf_free(&f);
            return 1;
        }

        int txx = sx + (int)jr_int_n(r, 2 * di + 1) - di;

        if (txx < bx0) txx += width;
        else if (txx >= bx0 + width) txx -= width;

        int tzz = sz + (int)jr_int_n(r, 2 * di + 1) - di;

        if (tzz < bz0) tzz += width;
        else if (tzz >= bz0 + width) tzz -= width;

        int f1 = jr_next(r, 1);
        int f2 = jr_next(r, 1);
        int f3 = jr_next(r, 1);
        int f4 = jr_next(r, 1);

        if (((f1 ? 1 : 0) | (f2 ? 2 : 0) | (f3 ? 4 : 0) | (f4 ? 8 : 0)) != flags)
        {
            printf("FAIL %s case %d: the draw stream gives flags %d, cases.bin has %d\n", dir, i,
                   (f1 ? 1 : 0) | (f2 ? 2 : 0) | (f3 ? 4 : 0) | (f4 ? 8 : 0), flags);
            pf_free(&f);
            return 1;
        }

        double tx, ty, tz;
        int ty_draw;

        if (method == 0)
        {
            ty_draw = (int)world_get_height_value(w, txx, tzz) - 1 + (int)jr_int_n(r, 3);
            tx = (double)((float)txx + 0.5F);
            ty = (double)((float)ty_draw + 0.5F);
            tz = (double)((float)tzz + 0.5F);
        }
        else
        {
            ty_draw = (int)world_get_height_value(w, txx, tzz) + (int)jr_int_n(r, 2);
            tx = (double)txx + 0.5;
            ty = (double)ty_draw;
            tz = (double)tzz + 0.5;
        }

        /* the recorded start position is the entity the oracle searched with */
        struct pf_entity e;
        memset(&e, 0, sizeof e);
        e.width = width_f;
        e.height = height_f;
        e.max_safe_point_tries = 3;
        pf_entity_set_position(&e, le_double(c + 16), le_double(c + 24), le_double(c + 32));

        /* the replay's own floor(minX)/floor(minZ) check against the header */
        int fx = (int)e.box.min_x - (e.box.min_x < (double)(int)e.box.min_x ? 1 : 0);
        int fz = (int)e.box.min_z - (e.box.min_z < (double)(int)e.box.min_z ? 1 : 0);

        if (fx != want_sx || fz != want_sz)
        {
            printf("FAIL %s case %d: the recorded start (%d,%d) does not match the recorded position (floor %d,%d)\n", dir,
                   i, want_sx, want_sz, fx, fz);
            pf_free(&f);
            return 1;
        }

        int got = method == 0
                      ? pf_get_entity_path_to_xyz(&f, &e, txx, ty_draw, tzz, max_dist, f1, f2, f3, f4, out, 4096)
                      : pf_get_path_entity_to_entity(&f, &e, tx, ty, tz, max_dist, f1, f2, f3, f4, out, 4096);

        if (got < 0)
        {
            if (want_result != 0 || want_npts != 0)
            {
                printf("FAIL %s case %d: size %.1fx%.1f method %d dist %d flags %d start (%d,%d,%d) target (%g,%g,%g):"
                       " the oracle recorded a path of %d points, the native port found none\n", dir, i, width_f, height_f,
                       method, (int)max_dist, flags, want_sx, want_sy, want_sz, tx, ty, tz, want_npts);
                pf_free(&f);
                return 1;
            }
        }
        else
        {
            if (want_result == 0 || got != want_npts)
            {
                printf("FAIL %s case %d: size %.1fx%.1f method %d dist %d flags %d start (%d,%d,%d) target (%g,%g,%g):"
                       " the oracle recorded %s with %d points, the native port has %d\n", dir, i, width_f, height_f,
                       method, (int)max_dist, flags, want_sx, want_sy, want_sz, tx, ty, tz,
                       want_result == 0 ? "null" : "a path", want_npts, got);
                pf_free(&f);
                return 1;
            }

            const unsigned char *rp = c + 73;

            for (int k = 0; k < got; ++k)
            {
                int wx = le_i32(rp + k * 12), wy = le_i32(rp + k * 12 + 4), wz = le_i32(rp + k * 12 + 8);

                if (out[k * 3] != wx || out[k * 3 + 1] != wy || out[k * 3 + 2] != wz)
                {
                    printf("FAIL %s case %d: point %d of %d is (%d,%d,%d), the oracle recorded (%d,%d,%d); size %.1fx%.1f"
                           " method %d dist %d flags %d\n", dir, i, k, got, out[k * 3], out[k * 3 + 1], out[k * 3 + 2], wx,
                           wy, wz, width_f, height_f, method, (int)max_dist, flags);
                    pf_free(&f);
                    return 1;
                }
            }
        }

        off += 73 + (size_t)(want_result ? want_npts * 12 : 0);
    }

    if (off != clen && probe_cases < 0) /* --cases reads only a prefix */
    {
        printf("FAIL %s: cases.bin has %zu bytes left after %d cases\n", dir, clen - off, ncases);
        pf_free(&f);
        return 1;
    }

    pf_free(&f);
    return 0;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);

    if (argc != 2)
    {
        fprintf(stderr, "usage: test_paths [--cases N] PATH_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1024];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file2(path, &mlen);

    char kind[64];
    manifest_str(manifest, "kind", kind, sizeof kind);

    if (strcmp(kind, "paths") != 0)
    {
        printf("SKIP %s: kind %s is not a paths probe\n", dir, kind);
        free(manifest);
        return 0;
    }

    int64_t seed = (int64_t)manifest_int(manifest, "seed");
    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int radius = (int)manifest_int(manifest, "radius");
    long long opseed = manifest_int(manifest, "opseed");
    int ncases = (int)manifest_int(manifest, "cases");

    int nchunks = 0, *lcz = NULL;
    int *lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    struct world w;
    world_init(&w, seed);

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    /* The region's placements are already inside the loaded chunks, so the
     * shapes only need to be well formed; the region compare is the check. */
    snprintf(path, sizeof path, "%s/shapes.bin", dir);
    size_t slen;
    unsigned char *shapes = probe_read_file2(path, &slen);

    if (slen % 16 != 0)
    {
        fprintf(stderr, "%s: shapes.bin is %zu bytes, not a multiple of 16\n", dir, slen);
        return 2;
    }

    /* the placements, in order, through the same setBlock the oracle used */
    for (size_t s2 = 0; s2 < slen; s2 += 16)
    {
        int x = le_i32(shapes + s2), y = le_i32(shapes + s2 + 4), z = le_i32(shapes + s2 + 8);
        int id = shapes[s2 + 12] | shapes[s2 + 13] << 8, meta = shapes[s2 + 14];
        world_set_block(&w, x, y, z, id, meta, 2);
    }

    int width = (2 * radius + 1) * 16;
    int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;

    snprintf(path, sizeof path, "%s/cases.bin", dir);
    size_t clen;
    unsigned char *cases = probe_read_file2(path, &clen);

    /* The replay's Random: the shape stage's draws (the Fisher-Yates over the
     * cell grid and one pattern draw per cell) come first, then the cases.
     * The patterns' own block draws are not replayed here: the case inputs
     * are recorded in cases.bin itself, and the region compare is the
     * placement check. */
    jrand r;
    jr_seed(&r, opseed);

    int cells = (width / 5) * (width / 5);
    int *order = malloc((size_t)cells * sizeof *order);

    for (int i = 0; i < cells; ++i) order[i] = i;

    for (int i = cells - 1; i > 0; --i)
    {
        int j = (int)jr_int_n(&r, i + 1);
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }

    for (int i = 0; i < cells; ++i)
    {
        int pat = (int)jr_int_n(&r, 25);

        /* the pattern's own draws (PathProbe.placePattern) */
        switch (pat)
        {
        case 2: (void)jr_int_n(&r, 2); break;
        case 5: (void)jr_int_n(&r, 4); (void)jr_next(&r, 1); break;
        case 6:
        case 7:
        case 8: (void)jr_int_n(&r, 4); break;
        case 9: (void)jr_int_n(&r, 8); break;
        case 13: (void)jr_int_n(&r, 8); break;
        case 14: (void)jr_int_n(&r, 10); break;
        case 15: (void)jr_int_n(&r, 4); break;
        case 18: (void)jr_int_n(&r, 8); (void)jr_int_n(&r, 8); break;
        default: break;
        }
    }

    int run = probe_limit(ncases);
    int fail = run_cases(dir, cases, clen, run, &w, bx0, bz0, width, &r);

    if (!fail && run == ncases)
    {
        /* The final region: the placements landed the same way on both sides. */
        snprintf(path, sizeof path, "%s/final.bin.gz", dir);
        fail = cmp_final(dir, path, &w, lcx, lcz, nchunks);
    }

    if (fail) probe_localize(argv[0], dir);

    if (!fail)
        printf("PASS %s: %d%s cases, %d placed blocks, %d chunks, seed %lld\n", dir, run,
               run < ncases ? " (--cases)" : "", (int)(slen / 16), nchunks,
               (long long)seed);

    free(order);
    free(shapes);
    free(cases);
    free(lcx);
    free(lcz);
    free(manifest);
    world_free(&w);
    return fail;
}
