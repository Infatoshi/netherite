/* Detmob A* units + fixture baker.
 *
 * --write-fixture FROM OUT copies the passives region and plants
 * panic_ticks=PL_REVENGE_TICKS on the sheep so EntityAIPanic.shouldExecute
 * calls PathFinder.findPath on the first setup tick.
 *
 * Units pin mai_path_to_pos (PathNavigateGround.getPathToPos) against the
 * magma reference pai_path_to_pos in magma/game/mob_live.c:916-937. The
 * mobs_det M1 fixture is a grass pad: every findPath destination there is
 * air-over-solid, where a two-way solid test and the real three-way Material
 * dispatch agree, so the lockstep digest cannot see the difference. These
 * columns are the cases where it can. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_core.h"
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

/* --- mai_path_to_pos on a synthetic 4 x 64 x 4 region --- */
#define PTP_NX 4
#define PTP_NY 64
#define PTP_NZ 4

static u16 ptp_cells[PTP_NX * PTP_NY * PTP_NZ];

static void ptp_set(int x, int y, int z, int id) {
    ptp_cells[((long)x * PTP_NY + y) * PTP_NZ + z] = (u16)(id << 4);
}

static int ptp_y(Blaze *e, int x, int y, int z) {
    int bx = x, by = y, bz = z;
    mai_path_to_pos(e, &bx, &by, &bz);
    return by;
}

static int run_path_to_pos(void) {
    Blaze *e = (Blaze *)calloc(1, sizeof *e);
    int y;
    if (!e) {
        fprintf(stderr, "FAIL: out of memory\n");
        return 1;
    }
    memset(ptp_cells, 0, sizeof ptp_cells);
    e->cells = ptp_cells;
    e->rx0 = 0; e->ry0 = 0; e->rz0 = 0;
    e->rnx = PTP_NX; e->rny = PTP_NY; e->rnz = PTP_NZ;
    e->rvol = (long)PTP_NX * PTP_NY * PTP_NZ;

    /* col (1,1): stone 0..9, air above. */
    for (y = 0; y <= 9; ++y) ptp_set(1, y, 1, 1);
    /* col (2,2): stone 0..4, still water at 5, air above. */
    for (y = 0; y <= 4; ++y) ptp_set(2, y, 2, 1);
    ptp_set(2, 5, 2, 9);
    /* col (3,3): stone 0..3, water 4..8, air above. */
    for (y = 0; y <= 3; ++y) ptp_set(3, y, 3, 1);
    for (y = 4; y <= 8; ++y) ptp_set(3, y, 3, 9);
    /* col (0,0): air all the way down, stone at 30 for the up-scan. */
    ptp_set(0, 30, 0, 1);

    /* AIR dest over solid ground: down-scan stops on the stone. */
    expect(ptp_y(e, 1, 20, 1) == 10, "air over stone -> ground + 1");
    /* AIR dest over WATER: Material.AIR down-scan stops at the first
     * non-AIR block, so the surface, not the lakebed (was 5). */
    expect(ptp_y(e, 2, 20, 2) == 6, "air over water -> water surface + 1");
    /* Non-air non-solid dest: Java falls through with pos unchanged. */
    expect(ptp_y(e, 3, 6, 3) == 6, "water dest stays put");
    /* Solid dest: walk up to the first non-solid. */
    expect(ptp_y(e, 1, 5, 1) == 10, "solid dest -> first non-solid above");
    /* AIR dest with no floor: down-scan bottoms out, up-scan takes over. */
    expect(ptp_y(e, 0, 20, 0) == 30, "bottomless air -> first non-air above");

    free(e);
    return fails ? 1 : 0;
}

static int run_units(void) {
    expect(PL_REVENGE_TICKS == 101, "EntityCreature revenge ~100 + 1");
    if (run_path_to_pos()) return 1;
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
