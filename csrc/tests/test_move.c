/* Gate: native Entity.moveEntity and the block collision boxes against the
 * oracle's movement probe (oracle/harness/netherite/oracle/Move.java; recorded with
 * `make -C oracle probe KIND=move`).
 *
 * The region loads exactly as test_probe does (Probe.rawChunks, cx-major with
 * cz inner, no population), shapes.bin is replayed through world_set_block with
 * flag 2, the entity is spawned where Move.java spawns it, and then every
 * recorded move is replayed with the teleports Move.java performs every 50 ops.
 *
 * The teleport target is not in moves.bin, so the test repeats the probe's own
 * draw sequence: the shape scatter, then the spawn, then per op the teleport
 * draw, the move scale and the three deltas, from one java.util.Random seeded
 * with OPSEED. That stream is the probe's only randomness (Move.java's header
 * says so), and the test asserts every draw it can against the recorded file:
 * each shape's x/z/id/meta, the deltas of every move, and the teleport y the
 * height map gives. A divergence there is reported as such, not as a physics
 * failure. The setBlock side effects of the shapes are compared through the
 * light state, which the replayed moves then read.
 *
 * Every recorded field is compared bit for bit. The first divergence prints the
 * move index, the field, the oracle's value and the native one, and stops. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../engine/entity.h"
#include "../engine/world.h"

#define MOVE_BYTES 150
#define SHAPE_BYTES 16

static int failures = 0;

/* ------------------------------------------------------------- java Random
 * java.util.Random, the 48 bit LCG. nextInt(0) is a Java IllegalArgumentException
 * in Move.java's table rows, so the width draw is always guarded. */
struct jrandom {
    uint64_t seed;
};

static void pr_set_seed(struct jrandom *r, int64_t seed)
{
    r->seed = ((uint64_t)seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
}

static uint32_t pr_next(struct jrandom *r, int bits)
{
    r->seed = (r->seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    return (uint32_t)(r->seed >> (48 - bits));
}

static int pr_next_int(struct jrandom *r, int n)
{
    if ((n & -n) == n) return (int)(((int64_t)n * (int64_t)pr_next(r, 31)) >> 31);

    int bits, val;

    do
    {
        bits = (int)pr_next(r, 31);
        val = bits % n;
    } while (bits - val + (n - 1) < 0);

    return val;
}

static double pr_next_double(struct jrandom *r)
{
    uint64_t hi = pr_next(r, 26), lo = pr_next(r, 27);
    return (double)((hi << 27) + lo) / (double)(1ULL << 53);
}

/* ------------------------------------------------------------- byte reads */
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t le_i32(const unsigned char *p)
{
    return (int32_t)le32(p);
}

static uint64_t le64(const unsigned char *p)
{
    uint64_t v = 0;

    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);

    return v;
}

static double le_double(const unsigned char *p)
{
    uint64_t bits = le64(p);
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static float le_float(const unsigned char *p)
{
    uint32_t bits = le32(p);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static void show_double(char *out, size_t n, const char *tag, double d)
{
    uint64_t bits;
    memcpy(&bits, &d, sizeof bits);
    snprintf(out, n, "%s%a (raw %016llx)", tag, d, (unsigned long long)bits);
}

/* One integer value out of the manifest's single JSON line. */
static long long manifest_int(const char *json, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);

    const char *p = strstr(json, pat);

    if (p == NULL) return 0;

    return strtoll(p + strlen(pat), NULL, 10);
}

static void *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL)
    {
        perror(path);
        exit(2);
    }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);

    unsigned char *buf = malloc((size_t)n + 1);

    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
    {
        fprintf(stderr, "short read in %s\n", path);
        exit(2);
    }

    fclose(f);
    buf[n] = 0; /* the manifest is read as a string */
    *len = (size_t)n;
    return buf;
}

/* --------------------------------------------------------------- compares */
static void fail(int i, const char *dir, const char *field, const char *want, const char *got, double dx, double dy,
                 double dz)
{
    printf("FAIL %s move %d: %s: the oracle recorded %s, the native replay has %s; input motion (%a, %a, %a)\n",
           dir, i, field, want, got, dx, dy, dz);
    ++failures;
}

static void cmp_double(int i, const char *dir, const char *field, double want, double got, double dx, double dy,
                       double dz)
{
    if (want == got) return;

    char w[128], g[128];

    show_double(w, sizeof w, "", want);
    show_double(g, sizeof g, "", got);
    fail(i, dir, field, w, g, dx, dy, dz);
}

static void cmp_float(int i, const char *dir, const char *field, float want, float got, double dx, double dy,
                      double dz)
{
    if (want == got) return;

    char w[72], g[72];
    uint32_t wb, gb;

    memcpy(&wb, &want, sizeof wb);
    memcpy(&gb, &got, sizeof gb);
    snprintf(w, sizeof w, "%a (raw %08x)", (double)want, wb);
    snprintf(g, sizeof g, "%a (raw %08x)", (double)got, gb);
    fail(i, dir, field, w, g, dx, dy, dz);
}

static void cmp_int(int i, const char *dir, const char *field, long want, long got, double dx, double dy, double dz)
{
    if (want == got) return;

    char w[32], g[32];

    snprintf(w, sizeof w, "%ld", want);
    snprintf(g, sizeof g, "%ld", got);
    fail(i, dir, field, w, g, dx, dy, dz);
}

/* The probe's shape table, in the order nextInt(SHAPES.length) picks it:
 * id, meta base, meta width (0 draws the base only). The manifest carries the
 * same table as text; this is the copy the draw stream needs. */
static const int SHAPES[31][3] = {
    {44, 0, 1}, {44, 8, 1}, {53, 0, 8}, {85, 0, 0}, {102, 0, 0}, {139, 0, 0},
    {78, 0, 8}, {81, 0, 0}, {88, 0, 0}, {60, 0, 0}, {65, 3, 1}, {65, 4, 1},
    {106, 0, 0}, {30, 0, 0}, {171, 0, 0}, {96, 0, 16}, {107, 0, 8}, {26, 0, 12},
    {92, 0, 0}, {20, 0, 0}, {1, 0, 0}, {9, 0, 0}, {11, 0, 0}, {106, 7, 1},
    {106, 8, 1}, {19, 0, 0}, {111, 0, 0}, {11, 2, 1}, {0, 0, 0}, {78, 0, 1},
    {92, 0, 0},
};

static const double SCALES[4] = {0.05, 0.3, 1.0, 3.0};

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_move MOVE_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1024];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = read_file(path, &mlen);

    int64_t seed = manifest_int(manifest, "seed");
    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int radius = (int)manifest_int(manifest, "radius");
    int nprobe_shapes = (int)manifest_int(manifest, "shapes");
    long long opseed = manifest_int(manifest, "opseed");
    int ops = (int)manifest_int(manifest, "ops");

    /* "loaded":[ [cx,cz], ... ] in the order the oracle loaded them */
    char *p = strstr(manifest, "\"loaded\":[");
    int nchunks = 0, cap = 1024;
    int *lcx = malloc((size_t)cap * sizeof *lcx), *lcz = malloc((size_t)cap * sizeof *lcz);

    if (p != NULL)
    {
        p += strlen("\"loaded\":[");
        while (*p == '[')
        {
            if (nchunks == cap)
            {
                cap *= 2;
                lcx = realloc(lcx, (size_t)cap * sizeof *lcx);
                lcz = realloc(lcz, (size_t)cap * sizeof *lcz);
            }

            lcx[nchunks] = (int)strtol(p + 1, &p, 10);
            lcz[nchunks] = (int)strtol(p + 1, &p, 10);
            ++nchunks;

            if (*p == ']') ++p;
            if (*p == ',') ++p;
        }

        if (*p != ']')
        {
            fprintf(stderr, "%s: cannot parse the manifest's loaded list\n", dir);
            return 2;
        }
    }

    if (nchunks == 0)
    {
        fprintf(stderr, "%s: no loaded chunks in the manifest\n", dir);
        return 2;
    }

    struct world w;
    world_init(&w, seed);

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    /* ------------------------------------------------------------ shapes */
    snprintf(path, sizeof path, "%s/shapes.bin", dir);
    size_t slen;
    unsigned char *shapes = read_file(path, &slen);

    if (slen % SHAPE_BYTES != 0)
    {
        fprintf(stderr, "%s: shapes.bin is %zu bytes, not a multiple of %d\n", dir, slen, SHAPE_BYTES);
        return 2;
    }

    int nshapes = (int)(slen / SHAPE_BYTES);
    int width = (2 * radius + 1) * 16;
    int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;

    struct jrandom r;
    pr_set_seed(&r, opseed);

    long placed = 0;
    int shape_draw_ok = 1;

    for (int i = 0; i < nshapes; ++i)
    {
        const unsigned char *s = shapes + SHAPE_BYTES * i;
        int x = le_i32(s), y = le_i32(s + 4), z = le_i32(s + 8);
        int id = s[12] | s[13] << 8, meta = s[14];

        if (i < nprobe_shapes)
        {
            int dx = bx0 + pr_next_int(&r, width);
            int dz = bz0 + pr_next_int(&r, width);
            pr_next_int(&r, 46); /* yDrawn; the placed y comes from the surface */
            int pick = pr_next_int(&r, 31);
            int row_meta = SHAPES[pick][1];
            if (SHAPES[pick][2] > 0) row_meta += pr_next_int(&r, SHAPES[pick][2]);

            if (dx != x || dz != z || SHAPES[pick][0] != id || row_meta != meta)
            {
                printf("FAIL %s shape %d: the draw stream gives (%d,%d) id %d meta %d, shapes.bin has (%d,%d) id %d"
                       " meta %d\n",
                       dir, i, dx, dz, SHAPES[pick][0], row_meta, x, z, id, meta);
                shape_draw_ok = 0;
                break;
            }
        }

        if (world_set_block(&w, x, y, z, id, meta, 2)) ++placed;
    }

    if (!shape_draw_ok) return 1;

    /* ------------------------------------------------------------ entity */
    int spawn_x = bx0 + pr_next_int(&r, width);
    int spawn_z = bz0 + pr_next_int(&r, width);

    struct entity e;
    entity_init(&e, &w);
    entity_set_size(&e, 0.6F, 1.8F);
    e.step_height = 0.5F;
    entity_set_position(&e, (double)spawn_x + 0.5, (double)world_get_height_value(&w, spawn_x, spawn_z),
                        (double)spawn_z + 0.5);

    snprintf(path, sizeof path, "%s/moves.bin", dir);
    size_t olen;
    unsigned char *moves = read_file(path, &olen);

    if (olen % MOVE_BYTES != 0 || (int)(olen / MOVE_BYTES) != ops)
    {
        fprintf(stderr, "%s: moves.bin is %zu bytes, the manifest says %d moves of %d\n", dir, olen, ops, MOVE_BYTES);
        return 2;
    }

    int teleports = 0, bad_draws = 0;

    for (int i = 0; i < ops && failures == 0 && bad_draws == 0; ++i)
    {
        const unsigned char *m = moves + MOVE_BYTES * i;
        int flag = m[0];

        if (flag & 1)
        {
            int tx = bx0 + pr_next_int(&r, width);
            int tz = bz0 + pr_next_int(&r, width);
            int k = pr_next_int(&r, 6);
            double ty = (double)world_get_height_value(&w, tx, tz) + (double)(k - 1);

            entity_set_position(&e, (double)tx + 0.5, ty, (double)tz + 0.5);
            e.motion_x = e.motion_y = e.motion_z = 0.0;
            e.fall_distance = 0.0F;
            e.on_ground = 0;
            ++teleports;
        }

        int sc = pr_next_int(&r, 4);
        double s = SCALES[sc];
        double dx = (pr_next_double(&r) - 0.5) * s;
        double dz = (pr_next_double(&r) - 0.5) * s;
        double dy = (pr_next_double(&r) - 0.7) * s;

        double rx = le_double(m + 1), ry = le_double(m + 9), rz = le_double(m + 17);

        if (dx != rx || dy != ry || dz != rz)
        {
            printf("FAIL %s move %d: the draw stream gives (%a, %a, %a), moves.bin has (%a, %a, %a)\n", dir, i, dx, dy,
                   dz, rx, ry, rz);
            ++bad_draws;
            break;
        }

        entity_move(&e, dx, dy, dz);

        const unsigned char *want = m + 25; /* posX..posZ, box, motion */
        cmp_double(i, dir, "posX", le_double(want + 0), e.pos_x, rx, ry, rz);
        cmp_double(i, dir, "posY", le_double(want + 8), e.pos_y, rx, ry, rz);
        cmp_double(i, dir, "posZ", le_double(want + 16), e.pos_z, rx, ry, rz);
        cmp_double(i, dir, "minX", le_double(want + 24), e.bounding_box.min_x, rx, ry, rz);
        cmp_double(i, dir, "minY", le_double(want + 32), e.bounding_box.min_y, rx, ry, rz);
        cmp_double(i, dir, "minZ", le_double(want + 40), e.bounding_box.min_z, rx, ry, rz);
        cmp_double(i, dir, "maxX", le_double(want + 48), e.bounding_box.max_x, rx, ry, rz);
        cmp_double(i, dir, "maxY", le_double(want + 56), e.bounding_box.max_y, rx, ry, rz);
        cmp_double(i, dir, "maxZ", le_double(want + 64), e.bounding_box.max_z, rx, ry, rz);
        cmp_double(i, dir, "motionX", le_double(want + 72), e.motion_x, rx, ry, rz);
        cmp_double(i, dir, "motionY", le_double(want + 80), e.motion_y, rx, ry, rz);
        cmp_double(i, dir, "motionZ", le_double(want + 88), e.motion_z, rx, ry, rz);
        cmp_float(i, dir, "ySize", le_float(want + 96), e.y_size, rx, ry, rz);
        cmp_float(i, dir, "fallDistance", le_float(want + 100), e.fall_distance, rx, ry, rz);
        cmp_float(i, dir, "distanceWalkedModified", le_float(want + 104), e.distance_walked_modified, rx, ry, rz);
        cmp_float(i, dir, "distanceWalkedOnStepModified", le_float(want + 108), e.distance_walked_on_step_modified, rx,
                  ry, rz);
        cmp_int(i, dir, "nextStepDistance", le_i32(want + 112), e.next_step_distance, rx, ry, rz);
        cmp_int(i, dir, "fire", le_i32(want + 116), e.fire, rx, ry, rz);
        cmp_int(i, dir, "onGround", want[120], e.on_ground, rx, ry, rz);
        cmp_int(i, dir, "isCollidedHorizontally", want[121], e.is_collided_horizontally, rx, ry, rz);
        cmp_int(i, dir, "isCollidedVertically", want[122], e.is_collided_vertically, rx, ry, rz);
        cmp_int(i, dir, "isCollided", want[123], e.is_collided, rx, ry, rz);
        cmp_int(i, dir, "isInWeb", want[124], e.is_in_web, rx, ry, rz);
    }

    if (failures == 0 && bad_draws == 0)
        printf("PASS %s: %d moves, %d shapes (%ld changed a block), %d teleports\n", dir, ops, nshapes, placed,
               teleports);

    return failures != 0 || bad_draws != 0;
}