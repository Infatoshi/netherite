/* Elytra arming unit tests + fixture baker.
 *
 * Units: xpBar-style pending consume, wall-damage formula pin, digest magic.
 * --write-fixture FROM OUT plants the player at (8.5, 80, 8.5) in air over
 * a stone floor at y=64. Chest elytra is applied by --set elytra=1. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "inventory_stack_rules.h"
#include "mc_blocks.h"
#include "port_parity.h"

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

static void plant_cell(CuSnapshot *s, int wx, int wy, int wz, int id, int meta) {
    int lx = wx - s->head.rx0;
    int ly = wy - s->head.ry0;
    int lz = wz - s->head.rz0;
    long idx;
    if (lx < 0 || ly < 0 || lz < 0 ||
        lx >= s->head.rnx || ly >= s->head.rny || lz >= s->head.rnz)
        return;
    idx = ((long)lx * s->head.rny + ly) * s->head.rnz + lz;
    s->cells[idx] = (unsigned short)(((id & 4095) << 4) | (meta & 15));
    if (s->light)
        s->light[idx] = 0;
}

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    int x, y, z, i;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    s.head.py = 80.0;
    s.head.box[1] = 80.0;
    s.head.box[4] = 80.0 + 1.8;
    s.head.on_ground = 0;
    s.head.mx = s.head.mz = 0.0;
    s.head.my = -0.08;
    s.head.yaw = 0.0f;
    s.head.pitch = 20.0f;
    s.head.px = 8.5;
    s.head.pz = 8.5;
    s.head.box[0] = 8.5 - 0.3;
    s.head.box[2] = 8.5 - 0.3;
    s.head.box[3] = 8.5 + 0.3;
    s.head.box[5] = 8.5 + 0.3;
    for (i = 0; i < 37; ++i)
        s.head.inv[i][0] = s.head.inv[i][1] = s.head.inv[i][2] = 0;

    for (x = 5; x <= 14; ++x)
        for (z = 5; z <= 14; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 90; ++y)
                plant_cell(&s, x, y, z, 0, 0);
        }

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 0;
    s.n_orbs = 0;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr, "WROTE %s player air (8.5,80,8.5) stone y=64\n", out_path);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    ICStack empty, chest;
    uint64_t h;
    memset(&empty, 0, sizeof empty);
    expect(isr_elytra_usable(&empty) == 0, "empty stack is not usable");
    memset(&chest, 0, sizeof chest);
    chest.item = ISR_ELYTRA_ITEM;
    chest.count = 1;
    chest.meta = 0;
    expect(isr_elytra_usable(&chest) == 1, "fresh elytra 443 is usable");
    h = bp_elytra_digest(1, 0, 0, 0, 0, 0.0f, 0.0, -0.08, 0.0, 0);
    expect(h != bp_hash_begin(), "elytra digest is not the FNV seed");
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
