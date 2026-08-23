/* Boat live-tick unit tests + fixture baker.
 *
 * Units: status water/land/air, IN_WATER momentum 0.9, controlBoat forward.
 * --write-fixture FROM OUT plants a still-water pool and a boat at
 * (12.5, 65.2, 12.5), player at (8.5, 65, 8.5). */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "boat_live.h"
#include "entity_hostile_spine.h"
#include "mc_blocks.h"

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

static void plant_boat(RlSnapMob *o, int slot, int id,
                       double x, double y, double z) {
    float w, h;
    ehs_size(EW_TYPE_BOAT, &w, &h);
    memset(o, 0, sizeof *o);
    o->slot = slot;
    o->id = id;
    o->type = EW_TYPE_BOAT;
    o->alive = 1;
    o->persist = 1;
    o->x = x;
    o->y = y;
    o->z = z;
    o->health = 40.0f;
    o->on_ground = 0;
    o->box_on = 1;
    o->box_minx = x - (double)(w * 0.5f);
    o->box_miny = y;
    o->box_minz = z - (double)(w * 0.5f);
    o->box_maxx = x + (double)(w * 0.5f);
    o->box_maxy = y + (double)h;
    o->box_maxz = z + (double)(w * 0.5f);
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
    s.head.yaw = 0.0f;
    s.head.pitch = 0.0f;
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
            plant_cell(&s, x, 63, z, BLK_STONE, 0);
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 68; ++y)
                plant_cell(&s, x, y, z, 0, 0);
        }
    /* Still water 8, meta 0. Pool at +X+Z so the boat is IN_WATER. */
    for (x = 10; x <= 14; ++x)
        for (z = 10; z <= 14; ++z)
            plant_cell(&s, x, 65, z, 8, 0);

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 1;
    s.n_orbs = 0;
    plant_boat(&s.mobs[0], 1, 1, 12.5, 65.2, 12.5);
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr, "WROTE %s boat (12.5,65.2,12.5) water pool n_mobs=%u\n",
            out_path, s.n_mobs);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    BlBoat b;
    expect(bl_status(8, 1, 0) == BL_STATUS_IN_WATER, "feet water is IN_WATER");
    expect(bl_status(0, 1, 0) == BL_STATUS_ON_LAND, "solid below air is ON_LAND");
    expect(bl_status(0, 0, 0) == BL_STATUS_IN_AIR, "air below is IN_AIR");

    memset(&b, 0, sizeof b);
    b.y = 65.2;
    b.vx = 1.0;
    bl_tick(&b, BL_STATUS_IN_WATER, 0, 0.0f, 0.0f);
    expect(b.vx > 0.89 && b.vx < 0.91, "IN_WATER momentum 0.9");

    memset(&b, 0, sizeof b);
    b.y = 65.0;
    bl_tick(&b, BL_STATUS_ON_LAND, 1, 1.0f, 0.0f);
    expect(b.vx == 0.0, "ridden forward at yaw 0 has no X thrust");
    expect(b.vz > 0.03 && b.vz < 0.05, "ridden forward at yaw 0 thrusts +Z 0.04");
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
