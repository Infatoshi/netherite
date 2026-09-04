#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "world_recipe.h"
#include "blaze_snapshot.h"
#include "blaze_io.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(char *err, size_t cap, const char *why, const char *path) {
    if (err && cap) snprintf(err, cap, "%s: %s", why, path ? path : "");
    return -1;
}

static int output_path(char *out, const char *dir, const char *name) {
    return snprintf(out, WORLD_RECIPE_PATH, "%s/%s", dir, name) <
           WORLD_RECIPE_PATH ? 0 : -1;
}

static int make_dir(const char *path) {
    struct stat st;
    if (mkdir(path, 0755) == 0) return 0;
    return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)
        ? 0 : -1;
}

static int file_hash(const char *path, uint64_t *out) {
    unsigned char buf[8192];
    uint64_t h = UINT64_C(14695981039346656037);
    size_t n, i;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        for (i = 0; i < n; ++i) {
            h ^= buf[i]; h *= UINT64_C(1099511628211);
        }
    int bad = ferror(f);
    if (fclose(f) != 0) bad = 1;
    if (bad) return -1;
    *out = h;
    return 0;
}

static int prepare_one(const char *src, const char *dst, int size,
                       FILE *manifest, char *err, size_t cap) {
    CuSnapshot *s = calloc(1, sizeof *s);
    uint64_t before, after, prepared;
    unsigned logs = 0;
    int rc = -1;
    if (!s) return fail(err, cap, "world snapshot allocation failed", src);
    if (file_hash(src, &before)) { fail(err, cap, "cannot hash source", src); goto done; }
    if (!blaze_snapshot_load(src, s, err, (int)cap, 0) ||
        !blaze_snapshot_crop(s, size, err, (int)cap, 0)) goto done;
    if (!blaze_snapshot_write(dst, s, err, (int)cap)) goto done;
    if (file_hash(src, &after) || before != after) {
        fail(err, cap, "source changed during world preparation", src); goto done;
    }
    if (file_hash(dst, &prepared)) { fail(err, cap, "cannot hash prepared world", dst); goto done; }
    long cells = (long)s->head.rnx * s->head.rny * s->head.rnz;
    for (long i = 0; i < cells; ++i) {
        int id = s->cells[i] >> 4;
        if (id == 17 || id == 162) ++logs;
    }
    fprintf(manifest, "%s\t%016llx\t%s\t%016llx\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%u\n",
            src, (unsigned long long)before, dst, (unsigned long long)prepared,
            s->head.rx0, s->head.ry0, s->head.rz0,
            s->head.rnx, s->head.rny, s->head.rnz, logs, s->ncoal);
    printf("world: %dx%dx%d cells=%ld logs=%u coal=%u snapshot=%s\n",
           s->head.rnx, s->head.rny, s->head.rnz, cells, logs, s->ncoal, dst);
    rc = 0;
done:
    blaze_snapshot_free(s);
    free(s);
    return rc;
}

/* Failure cleanup is limited to files created in our fresh private directory. */
static void discard(WorldRecipe *r) {
    char p[WORLD_RECIPE_PATH], name[64];
    if (!r->directory[0]) return;
    for (int i = 0; i < r->count; ++i) {
        snprintf(name, sizeof name, "snapshot_%03d.bsnp", i);
        if (!output_path(p, r->directory, name)) unlink(p);
        snprintf(name, sizeof name, "snapshot_%03d.bsnp.banks", i);
        if (!output_path(p, r->directory, name)) unlink(p);
    }
    const char *extra[] = {"nether.bsnp", "end.bsnp", "manifest.tsv"};
    for (size_t i = 0; i < sizeof extra / sizeof *extra; ++i)
        if (!output_path(p, r->directory, extra[i])) unlink(p);
    rmdir(r->directory);
    memset(r, 0, sizeof *r);
}

int world_recipe_prepare(WorldRecipe *r, const char *const *sources, int count,
                         int world_size, char *err, size_t cap) {
    char nether[1024], end[1024], manifest_path[WORLD_RECIPE_PATH];
    char bank_path[WORLD_RECIPE_PATH], side[WORLD_RECIPE_PATH], name[64];
    FILE *manifest = NULL;
    if (!r || !sources || count < 1 || count > WORLD_RECIPE_MAX ||
        (world_size != 0 && (world_size < 32 || world_size > 256 || world_size % 16)))
        return fail(err, cap, "invalid world recipe", "");
    memset(r, 0, sizeof *r);
    r->count = count;
    for (int i = 0; i < count; ++i) {
        if (!sources[i] || strlen(sources[i]) >= WORLD_RECIPE_PATH)
            return fail(err, cap, "invalid source path", "");
        if (!world_size) strcpy(r->paths[i], sources[i]);
    }
    if (!world_size) return 0;
    if (cu_resolve_banks(sources, count, "", "", "", "", nether, end, err, (int)cap))
        return -1;
    if (make_dir("out") || make_dir("out/blaze") || make_dir("out/blaze/rl") ||
        make_dir("out/blaze/rl/worlds"))
        return fail(err, cap, "cannot create world output directory", strerror(errno));
    strcpy(r->directory, "out/blaze/rl/worlds/run-XXXXXX");
    if (!mkdtemp(r->directory)) {
        r->directory[0] = 0;
        return fail(err, cap, "cannot create private world directory", strerror(errno));
    }
    if (output_path(manifest_path, r->directory, "manifest.tsv")) goto bad_path;
    manifest = fopen(manifest_path, "w");
    if (!manifest) { fail(err, cap, "cannot write manifest", manifest_path); goto failed; }
    fprintf(manifest, "source\tsource_fnv1a64\tprepared\tprepared_fnv1a64\tx0\ty0\tz0\tnx\tny\tnz\tlogs\tcoal\n");
    const char *banks[] = {nether, end};
    const char *bank_names[] = {"nether.bsnp", "end.bsnp"};
    for (int b = 0; b < 2; ++b) if (banks[b][0]) {
        if (output_path(bank_path, r->directory, bank_names[b])) goto bad_path;
        if (prepare_one(banks[b], bank_path, world_size, manifest, err, cap)) goto failed;
    }
    for (int i = 0; i < count; ++i) {
        int duplicate = -1;
        for (int j = 0; j < i; ++j)
            if (!strcmp(sources[j], sources[i])) { duplicate = j; break; }
        if (duplicate >= 0) { strcpy(r->paths[i], r->paths[duplicate]); continue; }
        snprintf(name, sizeof name, "snapshot_%03d.bsnp", i);
        if (output_path(r->paths[i], r->directory, name)) goto bad_path;
        if (prepare_one(sources[i], r->paths[i], world_size, manifest, err, cap)) goto failed;
        if (nether[0] || end[0]) {
            snprintf(name, sizeof name, "snapshot_%03d.bsnp.banks", i);
            if (output_path(side, r->directory, name)) goto bad_path;
            FILE *f = fopen(side, "w");
            if (!f) { fail(err, cap, "cannot write bank sidecar", side); goto failed; }
            if (nether[0]) fprintf(f, "nether=nether.bsnp\n");
            if (end[0]) fprintf(f, "end=end.bsnp\n");
            int bad = ferror(f);
            if (fclose(f) != 0) bad = 1;
            if (bad) { fail(err, cap, "bank sidecar write failed", side); goto failed; }
        }
    }
    int bad = ferror(manifest);
    if (fclose(manifest) != 0) bad = 1;
    manifest = NULL;
    if (bad) { fail(err, cap, "manifest write failed", manifest_path); goto failed; }
    printf("world: finite region; reward targets use prepared inputs; manifest=%s\n", manifest_path);
    return 0;
bad_path:
    fail(err, cap, "prepared world path too long", r->directory);
failed:
    if (manifest) fclose(manifest);
    discard(r);
    return -1;
}
