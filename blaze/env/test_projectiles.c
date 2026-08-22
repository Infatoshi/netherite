/* Arrow live-tick unit tests + fixture baker.
 *
 * Units: ItemBow charge curve, yaw=0 spawn heading, gravity/drag bits,
 * block despawn, 1200-tick life. --write-fixture FROM OUT copies a magma
 * region, grounds the s10 player, plants a +Z wall and a +X zombie, and
 * puts a bow+arrows in hotbar 0/1. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "entity_spine.h"
#include "mc_blocks.h"

#include <math.h>
#include <stdint.h>
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

static int bits_eq_d(double a, double b) {
    uint64_t ua, ub;
    memcpy(&ua, &a, 8);
    memcpy(&ub, &b, 8);
    return ua == ub;
}

static int bits_eq_f(float a, float b) {
    uint32_t ua, ub;
    memcpy(&ua, &a, 4);
    memcpy(&ub, &b, 4);
    return ua == ub;
}

typedef struct {
    unsigned char id[32][32][32];
    unsigned hits;
} TwWorld;

static int tw_block(TwWorld *w, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= 32 || y >= 32 || z >= 32) return 0;
    return (int)w->id[x][y][z];
}

#define PL_W TwWorld
#define PL_BLOCK(w, x, y, z) tw_block((w), (x), (y), (z))
#define PL_NOTE_HIT(w) do { (w)->hits++; } while (0)
#include "projectile_live.h"

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
        unsigned char sky = (id == 0) ? 15 : 0;
        s->light[idx] = (unsigned char)(sky << 4);
    }
}

static void plant_zombie(RlSnapMob *o, int slot, int id,
                         double x, double y, double z) {
    float hf = 0.6f / 2.0f;
    float h = 1.95f;
    memset(o, 0, sizeof *o);
    o->slot = slot;
    o->id = id;
    o->type = EW_TYPE_ZOMBIE;
    o->alive = 1;
    o->persist = 1;
    o->x = x;
    o->y = y;
    o->z = z;
    o->health = 20.0f;
    o->on_ground = 1;
    o->box_on = 1;
    o->box_minx = x - (double)hf;
    o->box_miny = y;
    o->box_minz = z - (double)hf;
    o->box_maxx = x + (double)hf;
    o->box_maxy = y + (double)h;
    o->box_maxz = z + (double)hf;
}

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    int x, y, z, top, i;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    /* Ground the s10 player (spawn y=66) so the look ray is stable.
     * yaw 0 looks +Z; after one dyaw=-90 the second shot looks +X. */
    s.head.py = 65.0;
    s.head.box[1] = 65.0;
    s.head.box[4] = 65.0 + 1.8;
    s.head.on_ground = 1;
    s.head.mx = s.head.my = s.head.mz = 0.0;
    s.head.yaw = 0.0f;
    s.head.pitch = 0.0f;
    s.head.hotbar_sel = 0;
    s.head.inv[0][0] = 261;
    s.head.inv[0][1] = 1;
    s.head.inv[0][2] = 0;
    s.head.inv[1][0] = 262;
    s.head.inv[1][1] = 16;
    s.head.inv[1][2] = 0;
    for (i = 2; i < 37; ++i)
        s.head.inv[i][0] = s.head.inv[i][1] = s.head.inv[i][2] = 0;

    /* Clear a +Z flight corridor at eye height, then a stone wall. */
    for (z = 9; z <= 13; ++z)
        for (x = 7; x <= 9; ++x)
            for (y = 65; y <= 68; ++y)
                plant_cell(&s, x, y, z, 0, 0);
    for (x = 7; x <= 9; ++x)
        for (y = 65; y <= 68; ++y)
            plant_cell(&s, x, y, 14, BLK_STONE, 0);

    /* Clear a +X corridor and plant a zombie on a stone floor at y=64 so
     * the yaw=-90 shot at eye y=66.62 intersects (feetY+0.9). */
    for (x = 8; x <= 16; ++x)
        for (z = 7; z <= 9; ++z)
            for (y = 65; y <= 75; ++y)
                plant_cell(&s, x, y, z, 0, 0);
    plant_cell(&s, 14, 64, 8, BLK_STONE, 0);
    top = 64;
    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 1;
    plant_zombie(&s.mobs[0], 1, 1, 14.5, (double)(top + 1), 8.5);
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s bow+arrows inv0/1 yaw=0 py=65 wall z=14 "
            "zombie (14.5,%d,8.5) n_mobs=%u\n",
            out_path, top + 1, s.n_mobs);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    TwWorld w;
    PlProj slots[4];
    PlProj *a;
    float f20, f10, f2;
    double expect_vy, expect_vz;

    f20 = pl_bow_charge(20);
    f10 = pl_bow_charge(10);
    f2 = pl_bow_charge(2);
    expect(bits_eq_f(f20, 1.0f), "getArrowVelocity(20) is 1.0F");
    expect(bits_eq_f(f10, (0.5f * 0.5f + 0.5f * 2.0f) / 3.0f),
           "getArrowVelocity(10) is (0.25+1)/3");
    expect(f2 < 0.1f, "getArrowVelocity(2) is below the 0.1 fire floor");
    expect(pl_bow_curve(20) == f20, "draw=20 curve equals clamped charge");

    memset(slots, 0, sizeof slots);
    expect(pl_spawn_arrow(slots, 4, 8.5, 65.0, 8.5, 0.0f, 0.0f, 1.0f),
           "yaw=0 pitch=0 charge=1 spawns");
    a = &slots[0];
    expect(a->type == 1 && a->age == 0, "spawned type 1 age 0");
    expect(bits_eq_d(a->x, 8.5) && a->vx == 0.0,
           "yaw=0 spawn x is feetX and vx is 0");
    expect(bits_eq_d(a->z, 8.5 + 0.2) && bits_eq_d(a->vz, 3.0),
           "yaw=0 spawn z is feetZ+0.2 and vz is 3");
    expect(bits_eq_d(a->y, 65.0 + 1.62), "spawn y is feetY + PSV_EYE_HEIGHT");

    memset(&w, 0, sizeof w);
    pl_tick_arrow(a, &w);
    expect_vy = -0.05 * 0.99;
    expect_vz = 3.0 * 0.99;
    expect(bits_eq_d(a->y, 65.0 + 1.62), "open-air tick 1 keeps y");
    expect(bits_eq_d(a->z, 8.5 + 0.2 + 3.0), "open-air tick 1 adds vz");
    expect(bits_eq_d(a->vy, expect_vy), "tick 1 gravity then 0.99 drag on vy");
    expect(bits_eq_d(a->vz, expect_vz), "tick 1 0.99 drag on vz");
    expect(a->age == 1 && a->active, "age is 1 and still flying");

    memset(slots, 0, sizeof slots);
    memset(&w, 0, sizeof w);
    w.id[10][5][8] = (unsigned char)BLK_STONE;
    expect(pl_spawn_arrow(slots, 4, 8.5, 5.0 - 1.62, 8.0, 0.0f, 0.0f, 1.0f),
           "spawn into wall scene");
    a = &slots[0];
    a->x = 9.5;
    a->y = 5.5;
    a->z = 8.5;
    a->vx = 3.0;
    a->vy = 0.0;
    a->vz = 0.0;
    pl_tick_arrow(a, &w);
    expect(!a->active, "non-air cell despawns the arrow");
    expect(w.hits == 1, "block despawn notes a hit");

    memset(slots, 0, sizeof slots);
    memset(&w, 0, sizeof w);
    expect(pl_spawn_arrow(slots, 4, 8.5, 20.0, 8.5, 0.0f, 0.0f, 1.0f),
           "spawn for 1200-tick despawn");
    a = &slots[0];
    a->age = 1200;
    pl_tick_arrow(a, &w);
    expect(!a->active, "age>1200 despawns");
    expect(w.hits == 0, "lifetime despawn is not a collision hit");
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
