#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "../env/blaze_snapshot.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); ++failures; \
} } while (0)

static size_t volume(const CuSnapshot *s) {
    return (size_t)s->head.rnx * (size_t)s->head.rny * (size_t)s->head.rnz;
}

static size_t cell_index(const CuSnapshot *s, int wx, int wy, int wz) {
    return ((size_t)(wx - s->head.rx0) * (size_t)s->head.rny +
            (size_t)(wy - s->head.ry0)) * (size_t)s->head.rnz +
           (size_t)(wz - s->head.rz0);
}

static void set_cell(CuSnapshot *s, int wx, int wy, int wz, int id, int meta) {
    s->cells[cell_index(s, wx, wy, wz)] = (unsigned short)((id << 4) | meta);
}

static CuSnapshot *synthetic(int nx, int ny, int nz, int rx, int ry, int rz) {
    CuSnapshot *s = calloc(1, sizeof *s);
    if (!s) abort();
    memcpy(s->head.magic, "BSNP", 4);
    s->head.version = BLAZE_SNAP_VERSION;
    s->head.rx0 = rx; s->head.ry0 = ry; s->head.rz0 = rz;
    s->head.rnx = nx; s->head.rny = ny; s->head.rnz = nz;
    s->head.ox = -80; s->head.oz = -160;
    s->head.px = rx + nx / 2.0 - s->head.ox + 0.25;
    s->head.py = ry + ny / 2.0;
    s->head.pz = rz + nz / 2.0 - s->head.oz + 0.75;
    s->head.box[0] = s->head.px - 0.3;
    s->head.box[1] = s->head.py;
    s->head.box[2] = s->head.pz - 0.3;
    s->head.box[3] = s->head.px + 0.3;
    s->head.box[4] = s->head.py + 1.8;
    s->head.box[5] = s->head.pz + 0.3;
    s->head.tick = 873; s->head.seed = 77;
    s->head.mx = 0.125; s->head.mz = -0.25;
    s->head.dig_hx = 4; s->head.dig_hy = 2; s->head.dig_hz = 8;
    s->head.dig_progress = 0.375f; s->head.world_dirty = 3;
    s->head.container = 2; s->head.container_wx = rx + 1;
    s->head.n_items = 1; s->items[0].x = rx + 2.5;
    s->items[0].y = ry + 1.0; s->items[0].z = rz + 3.5;
    s->items[0].item = 16; s->items[0].count = 4;
    s->world_rand_seed = 4567; s->update_lcg = -12671;
    s->ww_total_time = 901; s->ww_world_time = 567;
    s->ww_rand_seed48 = 7890; s->rt_mutations = 34;
    s->n_mobs = 1; s->mobs[0].slot = 1; s->mobs[0].id = 27;
    s->mobs[0].x = rx + 1.5; s->mobs[0].y = ry + 1;
    s->mobs[0].z = rz + 1.5;
    s->mobs[0].path_n = 1; s->mobs[0].path_x[0] = (short)(rx + 2);
    s->mobs[0].path_z[0] = (short)(rz + 2);
    s->n_furn = 1; s->furn[0].active = 1; s->furn[0].wx = rx;
    s->furn[0].wz = rz; s->furn[0].in_item = 15;
    s->active_furnace = 0;
    s->n_chest = 1; s->chest[0].active = 1;
    s->chest[0].wx = rx; s->chest[0].wz = rz;
    s->chest[0].slot[0][0] = 264; s->chest[0].slot[0][1] = 2;
    s->active_chest = 0;
    s->n_fall_upd = 1; s->fall_upd[0].active = 1;
    s->fall_upd[0].x = rx; s->fall_upd[0].z = rz;
    s->fall_upd[0].due_tick = 876;
    s->fluid[0].active = 1; s->fluid[0].has_water = 1;
    s->fluid[0].x0 = rx; s->fluid[0].x1 = rx + 5;
    s->fluid[0].z0 = rz; s->fluid[0].z1 = rz + 7;
    s->fluid[0].quiet_steps = 1; s->fluid_mutations = 12;
    s->xtra.look_px = -0.25; s->xtra.mob_tick = 123;
    s->xtra.ew_path_tx[0] = rx + 3.5;
    s->n_potions = 1; s->potions[0].id = 1; s->potions[0].duration = 37;
    s->cells = malloc(volume(s) * sizeof *s->cells);
    s->light = malloc(volume(s));
    s->biome = malloc((size_t)nx * (size_t)nz);
    if (!s->cells || !s->light || !s->biome) abort();
    for (int x = 0; x < nx; ++x) {
        for (int z = 0; z < nz; ++z)
            s->biome[(size_t)x * nz + z] = (unsigned char)((x * 5 + z * 3) % 64);
        for (int y = 0; y < ny; ++y)
            for (int z = 0; z < nz; ++z) {
                size_t i = ((size_t)x * ny + y) * nz + z;
                s->cells[i] = (unsigned short)(16 | ((x * 7 + y * 3 + z) & 15));
                s->light[i] = (unsigned char)((x * 13 + y * 7 + z * 19) & 255);
            }
    }
    return s;
}

static void destroy(CuSnapshot *s) { blaze_snapshot_free(s); free(s); }

static void check_runtime_equal(const CuSnapshot *before, const CuSnapshot *after) {
    CuSnapshot *copy = malloc(sizeof *copy);
    if (!copy) abort();
    memcpy(copy, after, sizeof *copy);
    copy->head.rx0 = before->head.rx0; copy->head.rz0 = before->head.rz0;
    copy->head.rnx = before->head.rnx; copy->head.rnz = before->head.rnz;
    copy->cells = before->cells; copy->light = before->light;
    copy->biome = before->biome; copy->coal = before->coal;
    copy->xy_off = before->xy_off; copy->cont = before->cont;
    copy->ncoal = before->ncoal; copy->ncont = before->ncont;
    copy->has_liquid = before->has_liquid;
    CHECK(memcmp(copy, before, sizeof *copy) == 0,
          "every runtime field and player pose bit is unchanged");
    free(copy);
}

static void check_slices(const CuSnapshot *before, const CuSnapshot *after) {
    int same = 1;
    for (int x = 0; x < after->head.rnx; ++x) {
        int wx = after->head.rx0 + x;
        for (int z = 0; z < after->head.rnz; ++z) {
            int wz = after->head.rz0 + z;
            size_t src = (size_t)(wx - before->head.rx0) * before->head.rnz +
                         (size_t)(wz - before->head.rz0);
            unsigned char b = before->biome ? before->biome[src] : BLAZE_SNAP_BIOME_PLAINS;
            if (after->biome[(size_t)x * after->head.rnz + z] != b) same = 0;
            for (int y = 0; y < after->head.rny; ++y) {
                int wy = after->head.ry0 + y;
                size_t a = cell_index(after, wx, wy, wz);
                size_t old = cell_index(before, wx, wy, wz);
                if (after->cells[a] != before->cells[old]) same = 0;
                if (before->light && after->light[a] != before->light[old]) same = 0;
            }
        }
    }
    CHECK(same, "cells, metadata, packed light and biomes preserve exact world coordinates");
    CHECK((before->light != NULL) == (after->light != NULL), "light presence is preserved");
}

static void check_indexes(const CuSnapshot *s, int no_ore_xy) {
    unsigned coal = 0;
    int cont = 0, liquid = 0, valid = 1;
    for (int x = 0; x < s->head.rnx; ++x)
        for (int y = 0; y < s->head.rny; ++y) {
            size_t col = (size_t)x * s->head.rny + y;
            if (s->xy_off && s->xy_off[col] != (int)coal) valid = 0;
            for (int z = 0; z < s->head.rnz; ++z) {
                int id = s->cells[col * s->head.rnz + z] >> 4;
                if (id == 16) {
                    if (coal >= s->ncoal || !s->coal ||
                        s->coal[coal * 3] != s->head.rx0 + x ||
                        s->coal[coal * 3 + 1] != s->head.ry0 + y ||
                        s->coal[coal * 3 + 2] != s->head.rz0 + z) valid = 0;
                    ++coal;
                }
                if (id >= 8 && id <= 11) liquid = 1;
                if (id == 54 || id == 58 || id == 61 || id == 62) {
                    if (s->ncont >= 0 && (cont >= s->ncont || !s->cont ||
                        s->cont[cont * 3] != s->head.rx0 + x ||
                        s->cont[cont * 3 + 1] != s->head.ry0 + y ||
                        s->cont[cont * 3 + 2] != s->head.rz0 + z)) valid = 0;
                    ++cont;
                }
            }
            if (s->xy_off && s->xy_off[col + 1] != (int)coal) valid = 0;
        }
    CHECK(valid && coal == s->ncoal, "coal ordering and ore-column offsets match retained cells");
    CHECK((!coal || no_ore_xy) ? s->xy_off == NULL : s->xy_off != NULL,
          "ore index follows no_ore_xy and empty-list rules");
    CHECK(cont > BLAZE_SNAP_MAX_CONT ? s->ncont == -1 && !s->cont : s->ncont == cont,
          "container cache retains entries or selects overflow fallback");
    CHECK(liquid == s->has_liquid, "liquid requirement is rebuilt from retained cells");
}

static void check_write_reload(const CuSnapshot *s) {
    char path[] = "/tmp/blaze-world-crop-XXXXXX", err[256];
    CuSnapshot *loaded = calloc(1, sizeof *loaded);
    int fd = mkstemp(path);
    if (fd < 0 || !loaded) abort();
    close(fd);
    CHECK(blaze_snapshot_write(path, s, err, sizeof err), "cropped snapshot writes");
    if (blaze_snapshot_load(path, loaded, err, sizeof err, s->xy_off == NULL)) {
        CHECK(memcmp(&s->head, &loaded->head, sizeof s->head) == 0,
              "write/reload preserves snapshot header and original wire version");
        check_slices(s, loaded);
        check_indexes(loaded, s->xy_off == NULL);
        if (s->head.version == BLAZE_SNAP_VERSION) check_runtime_equal(s, loaded);
    } else { fprintf(stderr, "reload: %s\n", err); CHECK(0, "cropped snapshot reloads"); }
    unlink(path);
    destroy(loaded);
}

static uint64_t payload_hash(const CuSnapshot *s, size_t vol, size_t bvol) {
    uint64_t h = UINT64_C(14695981039346656037);
    const unsigned char *p[3] = {(const unsigned char *)s->cells, s->light, s->biome};
    size_t n[3] = {vol * sizeof *s->cells, vol, bvol};
    for (int k = 0; k < 3; ++k)
        if (p[k]) for (size_t i = 0; i < n[k]; ++i) { h ^= p[k][i]; h *= UINT64_C(1099511628211); }
    return h;
}

static void expect_atomic_failure(CuSnapshot *s, int size, size_t vol, size_t bvol) {
    CuSnapshot *before = malloc(sizeof *before);
    char err[256] = {0};
    if (!before) abort();
    memcpy(before, s, sizeof *before);
    uint64_t hash = payload_hash(s, vol, bvol);
    CHECK(!blaze_snapshot_crop(s, size, err, sizeof err, 0), "invalid crop is rejected");
    CHECK(err[0] != 0, "crop failure explains the rejection");
    CHECK(memcmp(s, before, sizeof *s) == 0 && hash == payload_hash(s, vol, bvol),
          "failed crop preserves every field, allocation and payload byte");
    free(before);
}

static void test_real_fixture(const char *path) {
    CuSnapshot *before = calloc(1, sizeof *before), *s = calloc(1, sizeof *s);
    char err[256];
    if (!before || !s) abort();
    if (!blaze_snapshot_load(path, before, err, sizeof err, 0) ||
        !blaze_snapshot_load(path, s, err, sizeof err, 0)) {
        fprintf(stderr, "fixture: %s\n", err); CHECK(0, "real fixture loads");
        destroy(before); destroy(s); return;
    }
    int ok = blaze_snapshot_crop(s, 64, err, sizeof err, 0);
    CHECK(ok, "real fixture crops to requested finite world");
    if (ok) {
        printf("real fixture: %dx%dx%d -> %dx%dx%d, origin (%d,%d,%d)\n",
               before->head.rnx, before->head.rny, before->head.rnz,
               s->head.rnx, s->head.rny, s->head.rnz,
               s->head.rx0, s->head.ry0, s->head.rz0);
        CHECK(s->head.rnx == 64 && s->head.rny == 128 && s->head.rnz == 64,
              "real fixture produces 64x128x64");
        check_runtime_equal(before, s); check_slices(before, s);
        check_indexes(s, 0); check_write_reload(s);
    }
    destroy(before); destroy(s);
}

static void test_negative_and_indexes(void) {
    CuSnapshot *before = synthetic(96, 4, 80, -103, -7, -201);
    CuSnapshot *s = synthetic(96, 4, 80, -103, -7, -201);
    char err[256];
    /* Non-integral negative player coordinates exercise floor, not truncation. */
    before->head.px = s->head.px = -75.25 - s->head.ox;
    before->head.pz = s->head.pz = -140.75 - s->head.oz;
    int positions[][5] = {
        {-80,-6,-150,16,2}, {-80,-6,-149,16,7}, {-79,-5,-149,16,0},
        {-81,-6,-151,54,0}, {-81,-6,-150,58,0},
        {-81,-6,-149,61,0}, {-81,-6,-148,62,0},
        {-102,-6,-200,16,0}, {-102,-6,-199,9,0}
    };
    for (size_t i = 0; i < sizeof positions / sizeof positions[0]; ++i) {
        int *p = positions[i];
        set_cell(before,p[0],p[1],p[2],p[3],p[4]);
        set_cell(s,p[0],p[1],p[2],p[3],p[4]);
    }
    s->has_liquid = 1; /* Deliberately stale caches must be derived again. */
    CHECK(blaze_snapshot_crop(s, 32, err, sizeof err, 0), "negative-origin crop succeeds");
    CHECK(s->head.rx0 == -92 && s->head.rz0 == -157, "crop centers on floored world pose");
    CHECK(s->ncoal == 3 && s->ncont == 4 && !s->has_liquid,
          "crop drops outside ore/liquid and rebuilds retained containers");
    check_runtime_equal(before, s); check_slices(before, s); check_indexes(s, 0);
    check_write_reload(s);
    CHECK(blaze_snapshot_crop(s, 32, err, sizeof err, 1), "same-size crop can disable ore index");
    check_indexes(s, 1);
    set_cell(s, -80, -6, -150, 8, 1);
    CHECK(blaze_snapshot_crop(s, 32, err, sizeof err, 0), "same-size crop rebuilds mutated caches");
    CHECK(s->has_liquid && s->ncoal == 2, "retained liquid is detected");
    check_indexes(s, 0);
    destroy(before); destroy(s);

    s = synthetic(96, 4, 80, -103, -7, -201);
    s->head.px = -102.875 - s->head.ox;
    s->head.pz = -121.125 - s->head.oz;
    CHECK(blaze_snapshot_crop(s, 32, err, sizeof err, 0), "edge crop succeeds");
    CHECK(s->head.rx0 == -103 && s->head.rz0 == -153,
          "centering clamps to both low and high source boundaries");
    destroy(s);

    s = synthetic(32, 2, 32, -16, 0, -16);
    for (int i = 0; i < 65; ++i) s->cells[i] = 54 << 4;
    CHECK(blaze_snapshot_crop(s, 32, err, sizeof err, 0), "container overflow crop succeeds");
    check_indexes(s, 0); check_write_reload(s); destroy(s);

    s = synthetic(256, 1, 256, -128, 0, -128);
    CHECK(blaze_snapshot_crop(s, 256, err, sizeof err, 0), "maximum supported crop succeeds");
    CHECK(s->head.rny == 1, "cropping preserves a source height below 128");
    destroy(s);
}

static void test_absent_and_failures(void) {
    char err[256];
    CuSnapshot *s = synthetic(64, 4, 64, -32, 0, -32);
    size_t vol = volume(s), bvol = (size_t)s->head.rnx * s->head.rnz;
    int bad_sizes[] = {-16, 1, 16, 33, 96, 272, INT_MAX};
    for (size_t i = 0; i < sizeof bad_sizes / sizeof bad_sizes[0]; ++i)
        expect_atomic_failure(s, bad_sizes[i], vol, bvol);
    RlSnapHead saved = s->head;
    s->head.px = NAN; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.py = INFINITY; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.pz = -INFINITY; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.yaw = NAN; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.box[5] = INFINITY; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.px = -33 - s->head.ox; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.pz = 32 - s->head.oz; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.py = 4; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.rny = 129; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.rny = 0; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.rnx = INT_MAX; s->head.rnz = INT_MAX;
    expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.rx0 = INT_MAX - 1; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.rnz = -1; expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    s->head.version = BLAZE_SNAP_VERSION + 1;
    expect_atomic_failure(s, 32, vol, bvol); s->head = saved;
    unsigned char *light = s->light;
    s->light = NULL; expect_atomic_failure(s, 32, vol, bvol); s->light = light;
    unsigned short *cells = s->cells;
    s->cells = NULL; expect_atomic_failure(s, 32, vol, bvol); s->cells = cells;
    CuSnapshot *copy = malloc(sizeof *copy);
    if (!copy) abort();
    memcpy(copy, s, sizeof *copy);
    CHECK(blaze_snapshot_crop(s, 0, err, sizeof err, 1) && memcmp(s, copy, sizeof *s) == 0,
          "world_size zero preserves every field and pointer");
    free(copy);
    free(s->light); s->light = NULL;
    free(s->biome); s->biome = NULL;
    s->head.version = 1;
    CHECK(blaze_snapshot_crop(s, 32, err, sizeof err, 0), "legacy snapshot with absent planes crops");
    CHECK(s->light == NULL, "v1 crop does not fabricate light evidence");
    int plains = 1;
    for (int i = 0; i < 32 * 32; ++i) if (s->biome[i] != BLAZE_SNAP_BIOME_PLAINS) plains = 0;
    CHECK(plains, "absent biome becomes plains exactly like the loader");
    check_write_reload(s); destroy(s);
    CuSnapshot empty = {0};
    CHECK(blaze_snapshot_crop(&empty, 0, err, sizeof err, 0), "zero does not validate unused geometry");
    CHECK(!blaze_snapshot_crop(NULL, 32, NULL, 0, 0), "null input fails without an error buffer");
}

int main(int argc, char **argv) {
    const char *fixture = argc > 1 ? argv[1] : "verify/fixtures/port/s10_t0_r64_no_liquid.bsnp";
    test_real_fixture(fixture);
    test_negative_and_indexes();
    test_absent_and_failures();
    if (failures) { fprintf(stderr, "world crop: %d failures\n", failures); return 1; }
    puts("world crop: PASS (real fixture, exact slices, caches, resume state, atomic failure, round trip)");
    return 0;
}
