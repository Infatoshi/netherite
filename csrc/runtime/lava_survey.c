/* lava_survey (lane/obsidian5): where the lava a player can reach from the surface is, on each seed's world, measured
 * on the C engine's own world generator (seedworld.c's spawn area, then every chunk out to the radius loaded through
 * the same provider, each populated by Chunk.populateChunk's rule: lakes, springs, caves and ravines as the server makes
 * them). A measurement of the world for choosing seeds and routes, as speedrunners do; no macro reads it.
 *
 *   lava_survey [--seeds A-B[,C...]] [--radius R] [--jobs N] [-v]
 *       One LAVA line a seed (forked jobs, one write a line): the spawn, then for 128, 256 and R (default 512) blocks of
 *       spawn (horizontal distance to the pool's nearest reachable cell), for each reach class (open, walk, dig): the
 *       castable pools (LS_CAST sources or more) of that class, the nearest one's distance, y and sources. -v: every
 *       pool of LS_LIST sources or more with a reach, a POOL line each.
 *   lava_survey --summary FILE [--best N]
 *       The table over a file of LAVA lines: the share of seeds with a castable pool at each distance by class, the
 *       median nearest distance, seed 42's line, and the N best seeds (nearest open or walk-in castable pool).
 *
 * A pool: lava sources (ids 10 and 11, meta 0) joined face to face. Its reach, the best of its cells':
 *   open: the sky over the cell (every cell above it air-like: air, plants, snow layers, torches) or over an air cell
 *         beside it (a lava fall's face, a ravine floor's edge, a lake shore): seen from the surface or a ravine's side;
 *   walk: no open cell, but the air touching the pool joins air with the sky over it within LS_WALK steps face to face
 *         (a lake under an overhang, a cave mouth): walked into from the surface;
 *   dig:  neither, and LS_DIG blocks or fewer over a source.
 * A frame needs 10 casts, so a pool of 10 sources or more is castable from one place. */
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../engine/seedworld.h"
#include "../engine/world.h"

#define LS_RADIUS 512      /* blocks of spawn surveyed by default */
#define LS_CAST 10         /* the sources a frame's casts need */
#define LS_DIG 4           /* the most blocks over a source that count as a short dig */
#define LS_WALK 24         /* the air steps from a pool to the sky that count as walking in */
#define LS_WALK_NODES 60000 /* the air cells one walk-in search visits at most */
#define LS_LIST 4          /* -v lists pools of this many sources or more */
#define LS_MAXPOOL 65536   /* the cells one pool's flood holds */

enum { R_OPEN, R_WALK, R_DIG, R_CLASSES };
static const char *R_NAME[R_CLASSES] = {"open", "walk", "dig"};
static const int LS_D[2] = {128, 256}; /* the distances reported before the radius */

static int air_like(int b)
{
    return b == 0 || b == 31 || b == 32 || b == 37 || b == 38 || b == 39 || b == 40 || b == 78 || b == 175 ||
           b == 106 || b == 83 || b == 6 || b == 50;
}

static int block_at(struct world *w, int x, int y, int z)
{
    if (y < 0 || y > 255) return 0;
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);
    return c ? chunk_xyz_id(c, x & 15, y, z & 15) : -1;
}

static int lava_src(struct world *w, int x, int y, int z)
{
    if (y < 0 || y > 255) return 0;
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);
    if (!c) return 0;
    int b = chunk_xyz_id(c, x & 15, y, z & 15);
    return (b == 10 || b == 11) && chunk_xyz_meta(c, x & 15, y, z & 15) == 0;
}

/* the column's highest cell that is not air-like (-1 none, -2 its chunk not loaded) */
static int col_top(struct world *w, int x, int z)
{
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);
    if (!c) return -2;
    for (int y = 255; y >= 0; --y)
        if (!air_like(chunk_xyz_id(c, x & 15, y, z & 15))) return y;
    return -1;
}

struct box { int x0, z0, nx, nz; };

struct survey_ctx {
    struct world *w;
    struct box b;
    uint8_t *seen;     /* lava cells flooded, one bit a cell of the box */
    int *tops;         /* col_top, -3 not computed */
    int *pool;         /* the pool's cells, 3 ints each */
    /* the walk-in search: an open-addressed set of visited air cells and its queue */
    uint64_t *hset;
    int hcap;
    int *queue;
};

static int in_box(const struct box *b, int x, int z)
{
    return x >= b->x0 && x < b->x0 + b->nx && z >= b->z0 && z < b->z0 + b->nz;
}

static int top_at(struct survey_ctx *S, int x, int z)
{
    if (!in_box(&S->b, x, z)) return col_top(S->w, x, z);
    int *t = &S->tops[(x - S->b.x0) * S->b.nz + (z - S->b.z0)];
    if (*t == -3) *t = col_top(S->w, x, z);
    return *t;
}

static int seen_test_set(struct survey_ctx *S, int x, int y, int z)
{
    long i = ((long)(x - S->b.x0) * S->b.nz + (z - S->b.z0)) * 256 + y;
    int m = 1 << (i & 7);
    if (S->seen[i >> 3] & m) return 1;
    S->seen[i >> 3] |= m;
    return 0;
}

static int hset_add(struct survey_ctx *S, int x, int y, int z)
{
    uint64_t k = ((uint64_t)(uint32_t)x << 32 | (uint64_t)(uint32_t)z << 8 | (uint64_t)y) + 1;
    uint64_t h = k * 0x9e3779b97f4a7c15ull;
    for (int i = (int)(h >> 40) & (S->hcap - 1);; i = (i + 1) & (S->hcap - 1))
    {
        if (S->hset[i] == k) return 0;
        if (S->hset[i] == 0) { S->hset[i] = k; return 1; }
    }
}

/* the fewest air steps from the pool's touching air to a cell with the sky over it; LS_WALK + 1 when beyond */
static int walk_in(struct survey_ctx *S, int ncell)
{
    memset(S->hset, 0, sizeof(uint64_t) * (size_t)S->hcap);
    static const int N6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int qh = 0, qt = 0;
    for (int i = 0; i < ncell && qt < LS_WALK_NODES; ++i)
        for (int q = 0; q < 6; ++q)
        {
            int x = S->pool[3 * i] + N6[q][0], y = S->pool[3 * i + 1] + N6[q][1], z = S->pool[3 * i + 2] + N6[q][2];
            if (y < 1 || y > 254 || !air_like(block_at(S->w, x, y, z)) || block_at(S->w, x, y, z) < 0) continue;
            if (!hset_add(S, x, y, z) || qt >= LS_WALK_NODES) continue;
            S->queue[4 * qt] = x, S->queue[4 * qt + 1] = y, S->queue[4 * qt + 2] = z, S->queue[4 * qt + 3] = 0, ++qt;
        }
    while (qh < qt)
    {
        int x = S->queue[4 * qh], y = S->queue[4 * qh + 1], z = S->queue[4 * qh + 2], d = S->queue[4 * qh + 3];
        ++qh;
        int t = top_at(S, x, z);
        if (t >= -1 && t < y) return d;
        if (d >= LS_WALK) continue;
        for (int q = 0; q < 6; ++q)
        {
            int nx = x + N6[q][0], ny = y + N6[q][1], nz = z + N6[q][2];
            if (ny < 1 || ny > 254) continue;
            int b = block_at(S->w, nx, ny, nz);
            if (b < 0 || !air_like(b)) continue;
            if (qt >= LS_WALK_NODES || !hset_add(S, nx, ny, nz)) continue;
            S->queue[4 * qt] = nx, S->queue[4 * qt + 1] = ny, S->queue[4 * qt + 2] = nz, S->queue[4 * qt + 3] = d + 1,
                      ++qt;
        }
    }
    return LS_WALK + 1;
}

struct band { int n[R_CLASSES]; double near[R_CLASSES]; int y[R_CLASSES], src[R_CLASSES]; };

static void survey(long long seed, int radius, int verbose)
{
    static struct seedworld sw;
    seedworld_init(&sw, (int64_t)seed);
    seedworld_build(&sw);
    struct world *w = &sw.p.world;
    int sx = (int)sw.spawn_x, sz = (int)sw.spawn_z;
    int cx0 = sx >> 4, cz0 = sz >> 4, rc = (radius >> 4) + 3;
    /* rings outward, so every chunk within the radius has its populate companions loaded */
    for (int r = 0; r <= rc; ++r)
        for (int cx = cx0 - r; cx <= cx0 + r; ++cx)
            for (int cz = cz0 - r; cz <= cz0 + r; ++cz)
            {
                if (abs(cx - cx0) != r && abs(cz - cz0) != r) continue;
                if (!world_chunk_loaded(w, cx, cz)) world_load_chunk(w, cx, cz);
            }
    struct survey_ctx S;
    S.w = w;
    S.b = (struct box){sx - radius - 1, sz - radius - 1, 2 * radius + 3, 2 * radius + 3};
    S.seen = calloc((size_t)S.b.nx * S.b.nz * 256 / 8 + 1, 1);
    S.tops = malloc(sizeof(int) * (size_t)S.b.nx * S.b.nz);
    for (long i = 0; i < (long)S.b.nx * S.b.nz; ++i) S.tops[i] = -3;
    S.pool = malloc(sizeof(int) * 3 * LS_MAXPOOL);
    S.hcap = 1 << 18;
    S.hset = malloc(sizeof(uint64_t) * (size_t)S.hcap);
    S.queue = malloc(sizeof(int) * 4 * LS_WALK_NODES);
    int *stk = malloc(sizeof(int) * 3 * LS_MAXPOOL);
    struct band bd[3];
    for (int k = 0; k < 3; ++k)
        for (int c = 0; c < R_CLASSES; ++c) bd[k].n[c] = 0, bd[k].near[c] = 1e9, bd[k].y[c] = 0, bd[k].src[c] = 0;
    int dk[3] = {LS_D[0], LS_D[1], radius};
    long r2 = (long)radius * radius;
    int npools = 0;
    char line[8192];
    static const int N6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int x = sx - radius; x <= sx + radius; ++x)
        for (int z = sz - radius; z <= sz + radius; ++z)
        {
            long ddx = x - sx, ddz = z - sz;
            if (ddx * ddx + ddz * ddz > r2) continue;
            struct chunk *c = world_chunk(w, x >> 4, z >> 4);
            if (!c || !c->terrain_populated) continue;
            int top = top_at(&S, x, z);
            for (int y = 1; y <= top && y < 255; ++y)
            {
                int b = chunk_xyz_id(c, x & 15, y, z & 15);
                if (b != 10 && b != 11) continue;
                if (chunk_xyz_meta(c, x & 15, y, z & 15) != 0) continue;
                if (seen_test_set(&S, x, y, z)) continue;
                /* the pool's flood, face to face over sources */
                int n = 0, ns = 1, nopen = 0, ndig = 0;
                double d_open = 1e9, d_dig = 1e9, d_any = 1e9;
                int y_open = 0, y_dig = 0, y_any = 0, ox = 0, oz = 0;
                stk[0] = x, stk[1] = y, stk[2] = z;
                while (ns > 0)
                {
                    --ns;
                    int px = stk[3 * ns], py = stk[3 * ns + 1], pz = stk[3 * ns + 2];
                    if (n < LS_MAXPOOL) S.pool[3 * n] = px, S.pool[3 * n + 1] = py, S.pool[3 * n + 2] = pz;
                    n += 1;
                    double hd = sqrt((double)(px - sx) * (px - sx) + (double)(pz - sz) * (pz - sz));
                    if (hd < d_any) d_any = hd, y_any = py;
                    int t = top_at(&S, px, pz);
                    int open = t == py;
                    for (int q = 0; q < 4 && !open; ++q)
                    {
                        int qx = px + (q == 0) - (q == 1), qz = pz + (q == 2) - (q == 3);
                        int tq = top_at(&S, qx, qz);
                        if (tq >= -1 && tq < py) open = 1;
                    }
                    if (open)
                    {
                        nopen += 1;
                        if (hd < d_open) d_open = hd, y_open = py, ox = px, oz = pz;
                    }
                    else if (t >= py && t - py <= LS_DIG)
                    {
                        ndig += 1;
                        if (hd < d_dig) d_dig = hd, y_dig = py;
                    }
                    for (int q = 0; q < 6; ++q)
                    {
                        int qx = px + N6[q][0], qy = py + N6[q][1], qz = pz + N6[q][2];
                        if (!in_box(&S.b, qx, qz) || qy < 0 || qy > 255) continue;
                        if (!lava_src(w, qx, qy, qz)) continue;
                        if (seen_test_set(&S, qx, qy, qz)) continue;
                        if (ns < LS_MAXPOOL) stk[3 * ns] = qx, stk[3 * ns + 1] = qy, stk[3 * ns + 2] = qz, ++ns;
                    }
                }
                ++npools;
                int cls = -1, walk = -1;
                double d = 1e9;
                int yy = 0;
                if (nopen) cls = R_OPEN, d = d_open, yy = y_open;
                else if (n >= LS_CAST && (walk = walk_in(&S, n < LS_MAXPOOL ? n : LS_MAXPOOL)) <= LS_WALK)
                    cls = R_WALK, d = d_any, yy = y_any;
                else if (ndig) cls = R_DIG, d = d_dig, yy = y_dig;
                if (cls < 0) continue;
                for (int k = 0; k < 3; ++k)
                {
                    if (d > dk[k]) continue;
                    bd[k].src[cls] += n;
                    if (n < LS_CAST) continue;
                    bd[k].n[cls] += 1;
                    if (d < bd[k].near[cls]) bd[k].near[cls] = d, bd[k].y[cls] = yy;
                }
                if (verbose && n >= LS_LIST)
                {
                    int m = snprintf(line, sizeof line, "POOL seed %lld n %d class %s d %.0f y %d open %d dig %d walk %d "
                                     "open_at %d %d\n", seed, n, R_NAME[cls], d, yy, nopen, ndig, walk, ox, oz);
                    if (write(1, line, (size_t)m) < 0) {}
                }
            }
        }
    int m = snprintf(line, sizeof line, "LAVA seed %lld spawn %d %d pools %d", seed, sx, sz, npools);
    for (int k = 0; k < 3; ++k)
    {
        m += snprintf(line + m, sizeof line - (size_t)m, " | d %d", dk[k]);
        for (int c = 0; c < R_CLASSES; ++c)
            m += snprintf(line + m, sizeof line - (size_t)m, " %s %d %.0f %d %d", R_NAME[c], bd[k].n[c],
                          bd[k].n[c] ? bd[k].near[c] : -1.0, bd[k].y[c], bd[k].src[c]);
    }
    m += snprintf(line + m, sizeof line - (size_t)m, "\n");
    if (write(1, line, (size_t)m) < 0) {}
    free(stk);
    free(S.queue);
    free(S.hset);
    free(S.pool);
    free(S.tops);
    free(S.seen);
    seedworld_free(&sw);
}

/* ---- the summary over LAVA lines ---- */

struct row { long long seed; int dmax; int n[3][R_CLASSES], y[3][R_CLASSES]; double near[3][R_CLASSES]; };

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* the nearest castable pool a class set reaches at band k (1e9 none) */
static double nearest(const struct row *r, int k, int classes)
{
    double d = 1e9;
    for (int c = 0; c < R_CLASSES; ++c)
        if ((classes >> c & 1) && r->n[k][c] && r->near[k][c] < d) d = r->near[k][c];
    return d;
}

static int summary(const char *path, int best)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 2; }
    int cap = 4096, n = 0;
    struct row *R = malloc(sizeof *R * (size_t)cap);
    char buf[8192];
    while (fgets(buf, sizeof buf, f))
    {
        if (strncmp(buf, "LAVA ", 5)) continue;
        struct row r;
        memset(&r, 0, sizeof r);
        char *p = buf;
        if (sscanf(p, "LAVA seed %lld", &r.seed) != 1) continue;
        int ok = 1;
        for (int k = 0; k < 3 && ok; ++k)
        {
            p = strstr(p, "| d ");
            if (!p) { ok = 0; break; }
            int src[R_CLASSES];
            if (sscanf(p, "| d %d open %d %lf %d %d walk %d %lf %d %d dig %d %lf %d %d", &r.dmax, &r.n[k][0],
                       &r.near[k][0], &r.y[k][0], &src[0], &r.n[k][1], &r.near[k][1], &r.y[k][1], &src[1], &r.n[k][2],
                       &r.near[k][2], &r.y[k][2], &src[2]) != 13)
                ok = 0;
            p += 4;
        }
        if (!ok) continue;
        if (n == cap) R = realloc(R, sizeof *R * (size_t)(cap *= 2));
        R[n++] = r;
    }
    fclose(f);
    if (n == 0) { fprintf(stderr, "lava_survey: no LAVA lines in %s\n", path); return 1; }
    int dk[3] = {LS_D[0], LS_D[1], R[0].dmax};
    printf("%d seeds; a castable pool: %d sources or more joined face to face; open: the sky over a source or an air "
           "cell beside it; walk: air to the sky within %d steps; dig: %d blocks or fewer over it\n", n, LS_CAST,
           LS_WALK, LS_DIG);
    printf("%-7s %13s %13s %13s %13s %13s %15s\n", "within", "open", "walk", "dig", "open|walk", "any",
           "median o|w (m)");
    double *v = malloc(sizeof(double) * (size_t)n);
    for (int k = 0; k < 3; ++k)
    {
        int cnt[5] = {0}, nv = 0;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < R_CLASSES; ++c) cnt[c] += R[i].n[k][c] > 0;
            double dw = nearest(&R[i], k, 3);
            cnt[3] += dw < 1e9;
            cnt[4] += nearest(&R[i], k, 7) < 1e9;
            if (dw < 1e9) v[nv++] = dw;
        }
        qsort(v, (size_t)nv, sizeof *v, cmp_d);
        printf("%-7d", dk[k]);
        for (int c = 0; c < 5; ++c) printf(" %6d %5.1f%%", cnt[c], 100.0 * cnt[c] / n);
        printf(" %15.0f\n", nv ? v[nv / 2] : -1.0);
    }
    free(v);
    for (int i = 0; i < n; ++i)
        if (R[i].seed == 42)
        {
            printf("seed 42:");
            for (int k = 0; k < 3; ++k)
                printf(" within %d open %d walk %d dig %d (nearest o|w %.0f);", dk[k], R[i].n[k][0], R[i].n[k][1],
                       R[i].n[k][2], nearest(&R[i], k, 3) < 1e9 ? nearest(&R[i], k, 3) : -1.0);
            printf("\n");
        }
    /* the best: the nearest open or walk-in castable pool first */
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0; --j)
        {
            if (nearest(&R[j - 1], 2, 3) <= nearest(&R[j], 2, 3)) break;
            struct row t = R[j - 1];
            R[j - 1] = R[j];
            R[j] = t;
        }
    printf("the best %d (nearest open or walk-in castable pool):\n%8s %8s %6s %5s %9s %9s %9s\n", best, "seed",
           "nearest", "class", "y", "o|w<128", "o|w<256", "o|w<R");
    for (int i = 0; i < n && i < best; ++i)
    {
        int c = R[i].n[2][0] && (!R[i].n[2][1] || R[i].near[2][0] <= R[i].near[2][1]) ? 0 : 1;
        printf("%8lld %8.0f %6s %5d %9d %9d %9d\n", R[i].seed, nearest(&R[i], 2, 3), R_NAME[c], R[i].y[2][c],
               R[i].n[0][0] + R[i].n[0][1], R[i].n[1][0] + R[i].n[1][1], R[i].n[2][0] + R[i].n[2][1]);
    }
    free(R);
    return 0;
}

/* lane/obsidian6: --column SEED X Z R: the column tops (the highest cell not air-like, its block and the block over the
 * one under it) in the square of R round (X, Z), the chunks 3 round it loaded and populated; a line a column "x z top id
 * under". For placing a case's start (a dev tp beside a pool the survey named); no macro reads it */
static int column(long long seed, int x0, int z0, int rad)
{
    static struct seedworld sw;
    seedworld_init(&sw, (int64_t)seed);
    seedworld_build(&sw);
    struct world *w = &sw.p.world;
    int cx0 = x0 >> 4, cz0 = z0 >> 4, rc = (rad >> 4) + 3;
    for (int r = 0; r <= rc; ++r)
        for (int cx = cx0 - r; cx <= cx0 + r; ++cx)
            for (int cz = cz0 - r; cz <= cz0 + r; ++cz)
            {
                if (abs(cx - cx0) != r && abs(cz - cz0) != r) continue;
                if (!world_chunk_loaded(w, cx, cz)) world_load_chunk(w, cx, cz);
            }
    for (int x = x0 - rad; x <= x0 + rad; ++x)
        for (int z = z0 - rad; z <= z0 + rad; ++z)
        {
            int t = col_top(w, x, z);
            printf("%d %d %d %d %d\n", x, z, t, t >= 0 ? block_at(w, x, t, z) : -1,
                   t >= 1 ? block_at(w, x, t - 1, z) : -1);
        }
    return 0;
}

int main(int argc, char **argv)
{
    const char *seeds = "1-1000", *sum = NULL;
    int radius = LS_RADIUS, jobs = 1, verbose = 0, best = 20;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--column") && i + 4 < argc)
            return column(atoll(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]), atoi(argv[i + 4]));
        if (!strcmp(argv[i], "--seeds") && i + 1 < argc) seeds = argv[++i];
        else if (!strcmp(argv[i], "--radius") && i + 1 < argc) radius = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--jobs") && i + 1 < argc) jobs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--summary") && i + 1 < argc) sum = argv[++i];
        else if (!strcmp(argv[i], "--best") && i + 1 < argc) best = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else
        {
            fprintf(stderr, "usage: lava_survey [--seeds A-B,C] [--radius R] [--jobs N] [-v] | --summary FILE [--best N]\n");
            return 2;
        }
    }
    if (sum) return summary(sum, best);
    if (radius < 256) radius = 256;
    long long *list = NULL;
    int nl = 0, cl = 0;
    char *s = strdup(seeds);
    for (char *t = strtok(s, ","); t; t = strtok(NULL, ","))
    {
        long long a, b;
        if (sscanf(t, "%lld-%lld", &a, &b) != 2) b = a = atoll(t);
        for (long long v = a; v <= b; ++v)
        {
            if (nl == cl) list = realloc(list, sizeof *list * (size_t)(cl = cl ? 2 * cl : 256));
            list[nl++] = v;
        }
    }
    free(s);
    if (jobs < 1) jobs = 1;
    for (int j = 0; j < jobs; ++j)
    {
        pid_t pid = jobs == 1 ? 0 : fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0)
        {
            for (int i = j; i < nl; i += jobs) survey(list[i], radius, verbose);
            if (jobs > 1) _exit(0);
            return 0;
        }
    }
    int rc = 0, st;
    while (wait(&st) > 0)
        if (!WIFEXITED(st) || WEXITSTATUS(st)) rc = 1;
    return rc;
}
