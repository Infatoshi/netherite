/* Finite recipe worlds must be the inputs seen by both rewards and the live
 * CPU environment. Run from the repository root; argv[1] can select its .so. */
#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "world_recipe.h"
#include "blaze_snapshot.h"
#include "blaze_abi.h"
#include "blaze_io.h"
#include "chain_reward.h"
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); ++failures; \
} } while (0)

static uint64_t hash_file(const char *path) {
    uint64_t h = UINT64_C(14695981039346656037);
    unsigned char b[8192];
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) { CHECK(0, "open fixture for hash"); return 0; }
    while ((n = fread(b, 1, sizeof b, f)))
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= UINT64_C(1099511628211); }
    CHECK(!ferror(f), "read fixture for hash");
    CHECK(fclose(f) == 0, "close fixture hash");
    return h;
}

static int copy_file(const char *src, const char *dst) {
    unsigned char b[8192];
    FILE *in = fopen(src, "rb"), *out = fopen(dst, "wb");
    int rc = 0;
    size_t n;
    if (!in || !out) rc = -1;
    if (!rc) {
        while ((n = fread(b, 1, sizeof b, in)))
            if (fwrite(b, 1, n, out) != n) { rc = -1; break; }
        if (ferror(in)) rc = -1;
    }
    if (in && fclose(in)) rc = -1;
    if (out && fclose(out)) rc = -1;
    return rc;
}

static int entries(const char *path) {
    DIR *d = opendir(path);
    struct dirent *e;
    int n = 0;
    if (!d) return errno == ENOENT ? 0 : -1;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) ++n;
    closedir(d);
    return n;
}

/* Called only on the directory this test obtained from mkdtemp. Never follow
 * symlinks, and never traverse the fixture directory. */
static int remove_private_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st)) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *d = opendir(path);
    struct dirent *e;
    int rc = 0;
    if (!d) return -1;
    while ((e = readdir(d))) {
        char child[4096];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) >= (int)sizeof child ||
            remove_private_tree(child)) rc = -1;
    }
    closedir(d);
    if (rmdir(path)) rc = -1;
    return rc;
}

static CuSnapshot *load_snapshot(const char *path) {
    CuSnapshot *s = calloc(1, sizeof *s);
    char err[1024] = {0};
    if (!s || !blaze_snapshot_load(path, s, err, sizeof err, 0)) {
        fprintf(stderr, "snapshot load: %s: %s\n", path, err);
        CHECK(0, "reload snapshot"); free(s); return NULL;
    }
    return s;
}

static void free_snapshot(CuSnapshot *s) {
    if (s) { blaze_snapshot_free(s); free(s); }
}

static void check_pose_inventory(const CuSnapshot *a, const CuSnapshot *b) {
    CHECK(a->head.ox == b->head.ox && a->head.oz == b->head.oz,
          "crop preserves runtime window origin");
    CHECK(!memcmp(&a->head.px, &b->head.px, 3 * sizeof(double)) &&
          !memcmp(a->head.box, b->head.box, sizeof a->head.box),
          "crop preserves exact runtime pose and box");
    CHECK(a->head.yaw == b->head.yaw && a->head.pitch == b->head.pitch,
          "crop preserves view angles");
    CHECK(!memcmp(a->head.inv, b->head.inv, sizeof a->head.inv) &&
          a->head.hotbar_sel == b->head.hotbar_sel,
          "crop preserves inventory items and selection");
    CHECK(a->head.n_items == b->head.n_items &&
          !memcmp(a->items, b->items, (size_t)a->head.n_items * sizeof a->items[0]),
          "crop preserves runtime item records");
    CHECK(a->head.seed == b->head.seed && a->head.tick == b->head.tick,
          "crop preserves seed and runtime tick");
}

static void check_rewards(const char *path, const CuSnapshot *source,
                          const CuSnapshot *crop) {
    int cap = crop->head.rnx * crop->head.rny * crop->head.rnz;
    float *xyz = malloc((size_t)cap * 3 * sizeof *xyz);
    int n = 0, expected = 0, original = 0;
    CHECK(xyz != NULL, "reward target allocation");
    if (!xyz) return;
    CHECK(cr_logs_from_bsnp(path, xyz, cap, &n) == 0,
          "reward reader uses prepared snapshot");
    for (int x = 0; x < source->head.rnx; ++x)
        for (int y = 0; y < source->head.rny; ++y)
            for (int z = 0; z < source->head.rnz; ++z) {
                long i = ((long)x * source->head.rny + y) * source->head.rnz + z;
                if ((source->cells[i] >> 4) != 17) continue;
                ++original;
                int wx = x + source->head.rx0, wy = y + source->head.ry0;
                int wz = z + source->head.rz0;
                if (wx < crop->head.rx0 || wx >= crop->head.rx0 + crop->head.rnx ||
                    wy < crop->head.ry0 || wy >= crop->head.ry0 + crop->head.rny ||
                    wz < crop->head.rz0 || wz >= crop->head.rz0 + crop->head.rnz) continue;
                CHECK(expected < n, "retained log exists in reward targets");
                if (expected < n)
                    CHECK(xyz[expected * 3] == wx + 0.5f &&
                          xyz[expected * 3 + 1] == wy + 0.5f &&
                          xyz[expected * 3 + 2] == wz + 0.5f,
                          "reward target exactly matches retained source coordinate");
                ++expected;
            }
    CHECK(n == expected && n > 0 && n < original,
          "reward targets include all retained logs and exclude cropped logs");
    printf("recipe reward logs: source=%d retained=%d\n", original, n);
    free(xyz);
}

static void *symbol(void *lib, const char *name) {
    void *p = dlsym(lib, name);
    if (!p) { fprintf(stderr, "missing CPU symbol: %s\n", name); ++failures; }
    return p;
}

static void check_cpu(const char *so, const WorldRecipe *r, const CuSnapshot *crop) {
    void *lib = dlopen(so, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "dlopen %s: %s\n", so, dlerror()); ++failures; return; }
    void *(*create)(int, int, const BlazeCreateOpts *) =
        (void *(*)(int, int, const BlazeCreateOpts *))symbol(lib, "blaze_create");
    void (*destroy)(void *) = (void (*)(void *))symbol(lib, "blaze_destroy");
    int (*load)(void *, const char *const *, int, char *, int) =
        (int (*)(void *, const char *const *, int, char *, int))symbol(lib, "blaze_load_snapshots");
    int (*assign)(void *, const int *) = (int (*)(void *, const int *))symbol(lib, "blaze_assign");
    int (*reset)(void *, const unsigned char *) =
        (int (*)(void *, const unsigned char *))symbol(lib, "blaze_reset");
    int (*region)(void *, int, double *, double *, double *, float *, float *,
                  int *, int *, int *, int *, int *, int *, const unsigned short **) =
        (int (*)(void *, int, double *, double *, double *, float *, float *,
                  int *, int *, int *, int *, int *, int *, const unsigned short **))
        symbol(lib, "blaze_obs_cam_inputs");
    int (*step)(void *, const double *, int, unsigned short *, unsigned char *,
                unsigned char *, float *, float *, unsigned char *, float *) =
        (int (*)(void *, const double *, int, unsigned short *, unsigned char *,
                unsigned char *, float *, float *, unsigned char *, float *))
        symbol(lib, "blaze_step");
    if (!create || !destroy || !load || !assign || !reset || !region || !step) {
        dlclose(lib); return;
    }
    void *env = create(0, 1, NULL);
    CHECK(env != NULL, "create real CPU environment");
    if (env) {
        char err[1024] = {0};
        const char *paths[] = {r->paths[0]};
        int index = 0, x0 = 0, y0 = 0, z0 = 0, nx = 0, ny = 0, nz = 0;
        const unsigned short *cells = NULL;
        int rc = load(env, paths, 1, err, sizeof err);
        if (rc != 1) fprintf(stderr, "CPU load: %s\n", err);
        CHECK(rc == 1, "CPU loads prepared source");
        if (rc == 1 && assign(env, &index) == 0 && reset(env, NULL) == 0) {
            CHECK(region(env, 0, NULL, NULL, NULL, NULL, NULL,
                         &x0, &y0, &z0, &nx, &ny, &nz, &cells) == 0,
                  "read live CPU region");
            CHECK(nx == 64 && ny == 128 && nz == 64,
                  "actual CPU allocation is 64x128x64");
            CHECK(x0 == crop->head.rx0 && y0 == crop->head.ry0 && z0 == crop->head.rz0,
                  "live region origin matches prepared snapshot");
            CHECK(cells && !memcmp(cells, crop->cells, (size_t)64 * 128 * 64 * sizeof *cells),
                  "live reset cells match prepared snapshot");
            double act[13] = {0};
            float pose[5] = {0};
            unsigned char done = 255;
            act[9] = -1; act[10] = -1;
            CHECK(step(env, act, 1, NULL, NULL, NULL, NULL, NULL, &done, pose) == 0,
                  "production CPU step succeeds on finite world");
            CHECK(done == 0 && isfinite(pose[0]) && isfinite(pose[1]) && isfinite(pose[2]),
                  "production CPU step remains live with finite pose");
            printf("recipe CPU region: %dx%dx%d done=%u pose=%.6f,%.6f,%.6f\n",
                   nx, ny, nz, (unsigned)done, pose[0], pose[1], pose[2]);
        } else CHECK(0, "CPU assign and reset prepared snapshot");
        destroy(env);
    }
    dlclose(lib);
}

int main(int argc, char **argv) {
    char repo[4096], so[4096], scratch[4096];
    char source[4096], portal[4096], nether[4096], side[4096];
    char err[2048] = {0};
    WorldRecipe *r = calloc(1, sizeof *r), *p = calloc(1, sizeof *p), *f = calloc(1, sizeof *f);
    CHECK(r && p && f, "recipe allocations");
    if (!r || !p || !f || !getcwd(repo, sizeof repo)) return 2;
    if (!realpath(argc > 1 ? argv[1] : "out/blaze/env/blaze_cpu.so", so) ||
        !realpath("verify/fixtures/port/s10_t0_r64_no_liquid.bsnp", source) ||
        !realpath("verify/fixtures/port/s10_t0_r64_portals.bsnp", portal) ||
        !realpath("verify/fixtures/port/s10_t0_r64_nether.bsnp", nether) ||
        !realpath("verify/fixtures/port/s10_t0_r64_portals.bsnp.banks", side)) {
        perror("world recipe test input"); return 2;
    }
    const char *originals[] = {source, portal, nether, side};
    uint64_t before[4];
    for (int i = 0; i < 4; ++i) before[i] = hash_file(originals[i]);
    if ((mkdir("out", 0755) && errno != EEXIST) ||
        (mkdir("out/verify", 0755) && errno != EEXIST) ||
        snprintf(scratch, sizeof scratch, "%s/out/verify/world-recipe-test-XXXXXX", repo) >= (int)sizeof scratch ||
        !mkdtemp(scratch) || chdir(scratch)) { perror("test directory"); return 2; }
    const char *sources[] = {source, source};
    CHECK(world_recipe_prepare(r, sources, 2, 0, err, sizeof err) == 0 &&
          !strcmp(r->paths[0], source) && !strcmp(r->paths[1], source) && !r->directory[0],
          "world_size=0 preserves original input paths without output");
    CHECK(entries("out/blaze/rl/worlds") == 0, "no output for world_size=0");
    CHECK(world_recipe_prepare(r, sources, 2, 64, err, sizeof err) == 0,
          "prepare actual s10 as a 64-block finite world");
    CHECK(r->count == 2 && !strcmp(r->paths[0], r->paths[1]),
          "duplicate sources reuse one prepared snapshot");
    CuSnapshot *a = load_snapshot(source), *b = load_snapshot(r->paths[0]);
    if (a && b) {
        CHECK(b->head.rnx == 64 && b->head.rny == 128 && b->head.rnz == 64,
              "prepared s10 reloads as64x128x64");
        check_pose_inventory(a, b);
        check_rewards(r->paths[0], a, b);
        check_cpu(so, r, b);
    }
    CHECK(copy_file(portal, "portal_copy.bsnp") == 0, "copy portal fixture into private test directory");
    FILE *sf = fopen("portal_copy.bsnp.banks", "w");
    CHECK(sf != NULL, "write private portal sidecar");
    if (sf) { fprintf(sf, "nether=%s\n", nether); CHECK(fclose(sf) == 0, "close private sidecar"); }
    const char *portals[] = {portal, "portal_copy.bsnp"};
    CHECK(world_recipe_prepare(p, portals, 2, 64, err, sizeof err) == 0,
          "prepare real portal fixtures and shared nether bank");
    char banks[2][1024] = {{0}}, ends[2][1024] = {{0}};
    for (int i = 0; i < 2; ++i) {
        const char *prepared[] = {p->paths[i]};
        CHECK(cu_resolve_banks(prepared, 1, "", "", "", "", banks[i], ends[i],
                               err, sizeof err) == 0 && banks[i][0],
              "prepared portal sidecar resolves a bank");
    }
    CHECK(strcmp(p->paths[0], p->paths[1]) && !strcmp(banks[0], banks[1]) && strcmp(banks[0], nether),
          "distinct prepared snapshots share the same prepared bank path");
    CuSnapshot *bank = load_snapshot(banks[0]), *original_bank = load_snapshot(nether);
    if (bank && original_bank) {
        CHECK(bank->head.rnx == 64 && bank->head.rny == 128 && bank->head.rnz == 64,
              "prepared nether bank is64x128x64");
        check_pose_inventory(original_bank, bank);
    }
    int dirs = entries("out/blaze/rl/worlds");
    const char *one[] = {source};
    CHECK(world_recipe_prepare(f, one, 1, 256, err, sizeof err) == -1,
          "recipe rejects expansion beyond source bounds");
    CHECK(!f->directory[0] && f->count == 0 && entries("out/blaze/rl/worlds") == dirs,
          "expansion failure leaves no generated directory");
    const char *broken[] = {source, "missing-source.bsnp"};
    CHECK(world_recipe_prepare(f, broken, 2, 64, err, sizeof err) == -1,
          "partial preparation fails on missing second input");
    CHECK(!f->directory[0] && f->count == 0 && entries("out/blaze/rl/worlds") == dirs,
          "partial failure removes already prepared files and directory");
    for (int i = 0; i < 4; ++i)
        CHECK(hash_file(originals[i]) == before[i], "original snapshot or sidecar hash unchanged");
    free_snapshot(a); free_snapshot(b); free_snapshot(bank); free_snapshot(original_bank);
    free(r); free(p); free(f);
    CHECK(chdir(repo) == 0, "restore working directory");
    CHECK(remove_private_tree(scratch) == 0, "remove private test outputs");
    printf("test_world_recipe: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
