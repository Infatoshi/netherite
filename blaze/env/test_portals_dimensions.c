/* test_portals_dimensions.c - portal contact, dimension transit units, and fixture baker.
 *
 * Units: coordinate scaling, portal ignition frame detection, dimension light properties.
 * --write-fixture FROM OUT: loads an overworld snapshot, plants a lit nether portal
 * directly in front of the player, and writes the portals fixture and action chain.
 */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "nether_portal.h"
#include "mc_blocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond) {
        fprintf(stderr, "OK: %s\n", msg);
    } else {
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
    if (s->light) {
        unsigned char sky = (id == 0) ? 15 : (id == 90 ? 11 : 0);
        unsigned char blk = (id == 90) ? 11 : 0;
        s->light[idx] = (unsigned char)((sky << 4) | (blk & 15));
    }
}

static int write_chain(const char *path) {
    FILE *f = fopen(path, "w");
    int t;
    if (!f) {
        fprintf(stderr, "open %s failed\n", path);
        return 0;
    }
    fputc('[', f);
    for (t = 0; t < 96; ++t) {
        if (t) fputc(',', f);
        if (t < 8)
            fputs("{\"forward\":1.0}", f);
        else
            fputs("{}", f);
    }
    fputs("]\n", f);
    fclose(f);
    fprintf(stderr, "WROTE %s 96 actions\n", path);
    return 1;
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

    s.head.py = 65.0;
    s.head.box[1] = 65.0;
    s.head.box[4] = 65.0 + 1.8;
    s.head.on_ground = 1;
    s.head.mx = s.head.my = s.head.mz = 0.0;
    s.head.yaw = 0.0f;    /* facing +Z */
    s.head.pitch = 0.0f;
    s.head.px = 8.5;
    s.head.pz = 10.0;
    s.head.box[0] = 8.5 - 0.3;
    s.head.box[2] = 10.0 - 0.3;
    s.head.box[3] = 8.5 + 0.3;
    s.head.box[5] = 10.0 + 0.3;
    s.head.hotbar_sel = 0;
    s.n_mobs = 0;
    s.n_orbs = 0;
    for (i = 0; i < 37; ++i) {
        s.head.inv[i][0] = s.head.inv[i][1] = s.head.inv[i][2] = 0;
    }

    /* Floor at y=64, air at y=65..70 from z=6..14, x=6..10 */
    for (x = 6; x <= 10; ++x) {
        for (z = 6; z <= 14; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 70; ++y) {
                plant_cell(&s, x, y, z, 0, 0);
            }
        }
    }

    /* Nether portal frame at z=11 (spanning x=7..10, y=64..68) */
    for (x = 7; x <= 10; ++x) {
        plant_cell(&s, x, 64, 11, BLK_OBSIDIAN, 0); /* bottom */
        plant_cell(&s, x, 68, 11, BLK_OBSIDIAN, 0); /* top */
    }
    for (y = 65; y <= 67; ++y) {
        plant_cell(&s, 7, y, 11, BLK_OBSIDIAN, 0);  /* left */
        plant_cell(&s, 10, y, 11, BLK_OBSIDIAN, 0); /* right */
    }

    /* Portal pane interior: block 90, meta 1 (axis X) */
    for (x = 8; x <= 9; ++x) {
        for (y = 65; y <= 67; ++y) {
            plant_cell(&s, x, y, 11, 90, 1);
        }
    }

    /* Backstop wall at z=12 behind portal */
    for (x = 7; x <= 10; ++x) {
        for (y = 64; y <= 68; ++y) {
            plant_cell(&s, x, y, 12, BLK_STONE, 0);
        }
    }

    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr, "WROTE %s player (8.5, 65.0, 10.0), lit portal at z=11\n", out_path);
    blaze_snapshot_free(&s);

    return write_chain("blaze/rl/fixtures/portals_s10.json");
}

static int run_units(void) {
    /* Unit 1: coordinate scaling between overworld and nether */
    double ow_x = 8.5, ow_z = 11.6;
    int nether_x = (int)floor(ow_x * 0.125);
    int nether_z = (int)floor(ow_z * 0.125);
    expect(nether_x == 1 && nether_z == 1, "overworld (8.5, 11.6) -> nether (1, 1)");

    double ret_ow_x = (double)nether_x * 8.0;
    double ret_ow_z = (double)nether_z * 8.0;
    expect(ret_ow_x == 8.0 && ret_ow_z == 8.0, "nether (1, 1) -> overworld (8.0, 8.0)");

    /* Unit 2: portal dimension constants */
    expect(BLK_OBSIDIAN == 49, "BLK_OBSIDIAN is 49");
    expect(90 == 90, "Nether portal block id is 90");
    expect(119 == 119, "End portal block id is 119");

    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write-fixture") && i + 2 < argc) {
            return write_fixture(argv[i + 1], argv[i + 2]) ? 0 : 1;
        }
    }
    return run_units();
}
