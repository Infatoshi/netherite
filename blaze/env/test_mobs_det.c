/* Detmob A* fixture baker.
 *
 * --write-fixture FROM OUT copies the passives region and plants
 * panic_ticks=PL_REVENGE_TICKS on the sheep so EntityAIPanic.shouldExecute
 * calls PathFinder.findPath on the first setup tick. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "entity_spine.h"
#include "passive_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    unsigned i;
    int n_panic = 0;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    for (i = 0; i < s.n_mobs; ++i) {
        if (s.mobs[i].type == EW_TYPE_SHEEP && s.mobs[i].alive) {
            s.mobs[i].panic = PL_REVENGE_TICKS;
            s.mobs[i].on_ground = 1;
            ++n_panic;
        }
    }
    if (n_panic < 1) {
        fprintf(stderr, "no live sheep in %s to plant panic\n", from);
        blaze_snapshot_free(&s);
        return 0;
    }
    s.head.version = BLAZE_SNAP_VERSION;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s panic=%d on %d sheep n_mobs=%u digest=0x%016llx\n",
            out_path, PL_REVENGE_TICKS, n_panic, s.n_mobs,
            (unsigned long long)blaze_snap_mobs_digest(s.mobs, s.n_mobs));
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    expect(PL_REVENGE_TICKS == 101, "EntityCreature revenge ~100 + 1");
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int i;
    const char *from = NULL, *out = NULL;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write-fixture") && i + 2 < argc) {
            from = argv[++i];
            out = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--write-fixture FROM.bsnp OUT.bsnp]\n",
                    argv[0]);
            return 2;
        }
    }
    if (run_units())
        return 1;
    if (from && out && !write_fixture(from, out))
        return 1;
    return 0;
}
