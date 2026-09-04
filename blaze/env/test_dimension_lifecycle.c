#define _POSIX_C_SOURCE 200809L
#include "blaze_abi.h"
#include "port_parity.h"
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Public ABI test: no access to Blaze/CuVec layout, so the same executable
 * exercises CPU and CUDA libraries. The supplied snapshot must be the
 * portals fixture with its .banks sidecar. */
typedef struct {
    void *(*create)(int, int, const BlazeCreateOpts *);
    void (*destroy)(void *);
    int (*load)(void *, const char *const *, int, char *, int);
    int (*assign)(void *, const int *);
    int (*reset)(void *, const unsigned char *);
    int (*capture)(void *, int, int);
    int (*dump)(void *, int, const char *, char *, int);
    int (*has_liquid)(void *, int);
    int (*parity)(void *, int, void *);
    int (*parity_size)(void);
    int (*raw)(void *, int, const double *, int, void *);
    int (*tick)(void *, int, const double *, int, void *);
    int (*mobs)(void *, int);
} Api;
static Api api;
static int checks;
static void check(int cond, const char *what) {
    if (!cond) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
    checks++;
}
#define LOAD(field, name) do { \
    void *sym = dlsym(lib, name); \
    check(sym != NULL, name); \
    memcpy(&api.field, &sym, sizeof sym); \
} while (0)

static void action(double a[17], int t) {
    memset(a, 0, 17 * sizeof *a);
    a[9] = a[10] = -1;
    if (t < 10 || (t >= 90 && t < 98)) a[0] = 1;
    if (t >= 208 && t < 216) a[0] = -1;
}
static BpParityRecord record(void *h, int env) {
    BpParityRecord p;
    memset(&p, 0, sizeof p);
    check(api.parity(h, env, &p) == 0, "parity read succeeds");
    return p;
}
static const int compared[] = {BP_PLAYER, BP_WORLD, BP_PORTALS, BP_DIMENSIONS,
                                BP_RANDOM_TICKS};
static void same(const BpParityRecord *a, const BpParityRecord *b,
                 const char *where, int tick) {
    for (size_t i = 0; i < sizeof compared / sizeof compared[0]; i++) {
        int s = compared[i];
        if (!(a->measured_mask & BP_BIT(s)) || !(b->measured_mask & BP_BIT(s)) ||
            a->digest[s] != b->digest[s]) {
            fprintf(stderr, "FAIL: %s tick=%d subsystem=%d measured=%llx/%llx digest=%llx/%llx\n",
                    where, tick, s, (unsigned long long)a->measured_mask,
                    (unsigned long long)b->measured_mask,
                    (unsigned long long)a->digest[s], (unsigned long long)b->digest[s]);
            exit(1);
        }
        checks++;
    }
}
static void *setup(const char *snapshot, int n) {
    BlazeCreateOpts opts;
    char err[512] = "";
    int idx[2] = {0, 0};
    blaze_create_opts_default(&opts);
    void *h = api.create(0, n, &opts);
    check(h != NULL, "create succeeds");
    int rc = api.load(h, &snapshot, 1, err, sizeof err);
    if (rc != 1) fprintf(stderr, "snapshot load: %s\n", err);
    check(rc == 1, "load one snapshot and banks");
    check(api.assign(h, idx) == 0, "assign initial snapshot");
    check(api.reset(h, NULL) == 0, "initial reset succeeds");
    check(api.mobs(h, 0) == 0, "disable mobs");
    return h;
}
static void reject_capture(void *h, const char *why) {
    check(api.has_liquid(h, 2) == -1, "unused slot absent before capture");
    check(api.capture(h, 0, 2) < 0, why);
    check(api.has_liquid(h, 2) == -1, "rejected capture does not append snapshot slot");
    char path[] = "/tmp/netherite-dimension-dump-XXXXXX", err[256] = "";
    int fd = mkstemp(path);
    check(fd >= 0, "reserve unique dump path");
    close(fd);
    check(unlink(path) == 0, "clear reserved dump path");
    int rc = api.dump(h, 0, path, err, sizeof err);
    int created = access(path, F_OK) == 0;
    if (created) unlink(path);
    check(rc < 0 && strstr(err, "dimension"), "unrepresentable dump rejected with diagnosis");
    check(!created, "rejected dump does not create output file");
}
static void walk(void *h, int env, int lo, int hi) {
    for (int t = lo; t < hi; t++) {
        double a[17]; action(a, t);
        check(api.raw(h, env, a, 0, NULL) == 0, "raw lifecycle step succeeds");
    }
}
static void conflicts(const char *snapshot) {
    char dir[4096], a[4096], b[4096], aside[4096], bside[4096],
         source_side[4096], bank[4096] = "", line[4096], err[512] = "";
    const char *slash = strrchr(snapshot, '/');
    size_t prefix = slash ? (size_t)(slash - snapshot + 1) : 0;
    check(snprintf(dir, sizeof dir, "%s.lifecycle-XXXXXX", snapshot) < (int)sizeof dir,
          "conflict temp path fits");
    check(snprintf(source_side, sizeof source_side, "%s.banks", snapshot) < (int)sizeof source_side,
          "source sidecar path fits");
    FILE *f = fopen(source_side, "r");
    check(f != NULL, "source fixture sidecar opens");
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "nether=", 7)) continue;
        char *value = line + 7;
        value[strcspn(value, "\r\n")] = 0;
        check(snprintf(bank, sizeof bank, "%.*s%s", value[0] == '/' ? 0 : (int)prefix,
                       snapshot, value) < (int)sizeof bank, "bank path fits");
    }
    fclose(f);
    check(bank[0] && access(bank, R_OK) == 0, "source Nether bank exists");
    /* Place the temporary links next to the fixture so link() stays on its
     * filesystem even when /tmp is tmpfs. All five files are removed below. */
    check(mkdtemp(dir) != NULL, "create conflict fixture directory");
    check(snprintf(a, sizeof a, "%s/a.bsnp", dir) < (int)sizeof a &&
          snprintf(b, sizeof b, "%s/b.bsnp", dir) < (int)sizeof b &&
          snprintf(aside, sizeof aside, "%s.banks", a) < (int)sizeof aside &&
          snprintf(bside, sizeof bside, "%s.banks", b) < (int)sizeof bside,
          "conflict fixture paths fit");
    check(link(snapshot, a) == 0 && link(snapshot, b) == 0, "hardlink immutable fixture twice");
    f = fopen(aside, "w"); check(f != NULL, "write first sidecar");
    fprintf(f, "nether=%s\n", bank); fclose(f);
    f = fopen(bside, "w"); check(f != NULL, "write conflicting sidecar");
    fprintf(f, "nether=nonexistent-conflicting-bank.bsnp\n"); fclose(f);
    BlazeCreateOpts opts; blaze_create_opts_default(&opts);
    void *h = api.create(0, 2, &opts);
    check(h != NULL, "create conflict test handle");
    const char *paths[2] = {a, b};
    int rc = api.load(h, paths, 2, err, sizeof err);
    int count_unchanged = api.has_liquid(h, 0) == -1;
    api.destroy(h);
    check(unlink(aside) == 0 && unlink(bside) == 0 && unlink(a) == 0 &&
          unlink(b) == 0 && rmdir(dir) == 0, "remove temporary conflict fixtures");
    if (rc >= 0 || !strstr(err, "conflict")) fprintf(stderr, "conflict load rc=%d err=%s\n", rc, err);
    check(rc < 0 && strstr(err, "conflict"), "same-call conflicting sidecars rejected before bank loading");
    check(count_unchanged, "conflicting load does not append any snapshot slot");
    printf("dimension lifecycle: conflicting sidecars fail before slot mutation PASS\n");
}
int main(int argc, char **argv) {
    void *lib, *single, *batch;
    BpParityRecord initial, nether;
    if (argc != 3 && !(argc == 4 && !strcmp(argv[3], "--lifecycle-only"))) {
        fprintf(stderr, "usage: %s BLAZE_LIBRARY PORTALS_SNAPSHOT [--lifecycle-only]\n", argv[0]);
        return 2;
    }
    lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    LOAD(create, "blaze_create"); LOAD(destroy, "blaze_destroy");
    LOAD(load, "blaze_load_snapshots"); LOAD(assign, "blaze_assign");
    LOAD(reset, "blaze_reset"); LOAD(capture, "blaze_capture");
    LOAD(dump, "blaze_dump_snapshot");
    LOAD(has_liquid, "blaze_snapshot_has_liquid"); LOAD(parity, "blaze_parity_state");
    LOAD(parity_size, "blaze_parity_size"); LOAD(raw, "blaze_tick_raw");
    LOAD(tick, "blaze_tick"); LOAD(mobs, "blaze_set_mobs_enabled");
    check(api.parity_size() == (int)sizeof(BpParityRecord), "parity ABI size");
    if (argc == 3) conflicts(argv[2]);
    single = setup(argv[2], 1); batch = setup(argv[2], 2);
    check(api.capture(single, 0, 1) == 0, "initial single-world capture allowed");
    check(api.capture(batch, 0, 1) == 0, "initial batch single-world capture allowed");
    initial = record(single, 0);
    for (int mode = 0; mode < 2; mode++) {
        int (*step)(void *, int, const double *, int, void *) = mode ? api.tick : api.raw;
        check(api.reset(single, NULL) == 0 && api.reset(batch, NULL) == 0,
              "reset before path comparison");
        for (int t = 0; t < 346; t++) {
            double a[17]; action(a, t);
            check(step(single, 0, a, 0, NULL) == 0, "individual step succeeds");
            check(step(batch, -1, a, 0, NULL) == 0, "broadcast step succeeds");
            BpParityRecord p = record(single, 0);
            for (int env = 0; env < 2; env++) {
                BpParityRecord q = record(batch, env);
                same(&p, &q, mode ? "production broadcast" : "raw broadcast", t);
            }
            if (t == 89) {
                check(p.digest[BP_DIMENSIONS] != initial.digest[BP_DIMENSIONS], "outbound transit reached Nether");
                reject_capture(single, "capture while in Nether rejected");
            }
        }
        BpParityRecord p = record(single, 0);
        check(p.digest[BP_DIMENSIONS] == initial.digest[BP_DIMENSIONS], "346-action round trip returns to Overworld");
        reject_capture(single, "capture after returning from visited Nether rejected");
        printf("dimension lifecycle: %s individual/broadcast round trip PASS\n", mode ? "production" : "raw");
    }
    check(api.reset(single, NULL) == 0 && api.reset(batch, NULL) == 0, "reset for mutation check");
    walk(single, 0, 0, 90); walk(batch, -1, 0, 90);
    BpParityRecord before_peer = record(batch, 1);
    /* Leave the arrival pane before digging: its obsidian bottom is not
     * mineable by the empty-handed fixture in this bounded test. */
    walk(single, 0, 90, 98); walk(batch, 0, 90, 98);
    nether = record(single, 0);
    int mutated = 0;
    for (int t = 0; t < 240; t++) {
        double a[17]; action(a, 30);
        check(api.raw(single, 0, a, 0, NULL) == 0, "unmodified Nether control tick");
        a[7] = 1; a[3] = t == 0 ? 90 : 0;
        check(api.raw(batch, 0, a, 0, NULL) == 0, "dig Nether floor");
        BpParityRecord p = record(batch, 0);
        BpParityRecord control = record(single, 0);
        if (p.digest[BP_WORLD] != control.digest[BP_WORLD]) { mutated = 1; break; }
    }
    check(mutated, "public actions mutate private Nether blocks");
    BpParityRecord untouched = record(batch, 1);
    same(&before_peer, &untouched, "mutation isolated from peer", 0);
    unsigned char mask[2] = {1, 0};
    check(api.reset(batch, mask) == 0, "masked reset from Nether");
    BpParityRecord reset = record(batch, 0);
    same(&initial, &reset, "reset restores initial Overworld", 0);
    untouched = record(batch, 1);
    same(&before_peer, &untouched, "masked reset leaves peer unchanged", 0);
    check(api.capture(batch, 0, 1) == 0, "reset clears visited-world capture restriction");
    walk(batch, 0, 0, 98);
    reset = record(batch, 0);
    same(&nether, &reset, "reset removes private Nether mutations on next visit", 98);
    printf("dimension lifecycle: private mutation, masked reset, bank restoration PASS\n");
    api.destroy(batch); api.destroy(single); dlclose(lib);
    printf("dimension lifecycle: %d checks PASS\n", checks);
    return 0;
}
