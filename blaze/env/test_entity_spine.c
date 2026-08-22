/* Entity.move / travel spine unit tests + fixture baker.
 *
 * Default: gravity 0.08D then * (double)0.98F, fall onto a stone floor,
 * ground X damping. --write-fixture FROM OUT copies a magma region and
 * plants two spawned-style zombies (ground slide + air fall). */
#define _POSIX_C_SOURCE 200809L
#include "entity_spine.h"
#include "blaze_snapshot.h"

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

static void plant_zombie(RlSnapMob *o, int slot, int id,
                         double x, double y, double z,
                         double mx, double my, double mz, int on_ground) {
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
    o->mx = mx;
    o->my = my;
    o->mz = mz;
    o->on_ground = on_ground ? 1 : 0;
    o->health = 20.0f;
    o->box_on = 1;
    o->box_minx = x - (double)hf;
    o->box_miny = y;
    o->box_minz = z - (double)hf;
    o->box_maxx = x + (double)hf;
    o->box_maxy = y + (double)h;
    o->box_maxz = z + (double)hf;
}

static int cell_id(const CuSnapshot *s, int wx, int wy, int wz) {
    int lx = wx - s->head.rx0;
    int ly = wy - s->head.ry0;
    int lz = wz - s->head.rz0;
    long idx;
    if (lx < 0 || ly < 0 || lz < 0 ||
        lx >= s->head.rnx || ly >= s->head.rny || lz >= s->head.rnz)
        return 0;
    idx = ((long)lx * s->head.rny + ly) * s->head.rnz + lz;
    return (int)(s->cells[idx] >> 4);
}

static int column_top(const CuSnapshot *s, int wx, int wz) {
    int y;
    for (y = s->head.ry0 + s->head.rny - 1; y >= s->head.ry0; --y)
        if (ess_solid_id(cell_id(s, wx, y, wz)))
            return y;
    return -1;
}

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    int top0, top1, id0, id1;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    top0 = column_top(&s, 4, 4);
    top1 = column_top(&s, 8, 12);
    if (top0 < 0 || top1 < 0) {
        fprintf(stderr, "no solid column at (4,4) or (8,12)\n");
        blaze_snapshot_free(&s);
        return 0;
    }
    id0 = cell_id(&s, 4, top0, 4);
    id1 = cell_id(&s, 8, top1, 12);
    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 2;
    /* Ground slide: feet on the solid top. EntityLivingBase.java:2017. */
    plant_zombie(&s.mobs[0], 1, 1, 4.5, (double)(top0 + 1), 4.5,
                 0.2, 0.0, 0.0, 1);
    /* Air fall: eight blocks above the other column. */
    plant_zombie(&s.mobs[1], 2, 2, 8.5, (double)(top1 + 8), 12.5,
                 0.0, 0.0, 0.0, 0);
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s n_mobs=%u digest=0x%016llx "
            "ground id=%d top=%d fall id=%d top=%d\n",
            out_path, s.n_mobs,
            (unsigned long long)blaze_snap_mobs_digest(s.mobs, s.n_mobs),
            id0, top0, id1, top1);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    McSinTable st;
    EbLiving liv;
    PcfBlock stone[8];
    int i, nstone = 0;
    double expect_my;
    float f6, slip;

    mc_sin_table_init(&st);

    /* Stone floor occupying y=0 at x/z = 0. Entity.java move collides
     * against full-cube BF_SOLID. */
    for (i = -1; i <= 1; ++i) {
        stone[nstone].block_id = 1;
        stone[nstone].ox = (double)i;
        stone[nstone].oy = 0.0;
        stone[nstone].oz = 0.0;
        stone[nstone].ladder_facing = 0;
        ++nstone;
    }

    ess_load_pose(&liv, EW_TYPE_ZOMBIE, 0.5, 3.0, 0.5,
                  0.0, 0.0, 0.0, 0, 0.0f, 0,
                  0, 0, 0, 0, 0, 0);
    ess_tick_living(&liv, 0.6f, stone, nstone, &st);
    /* First tick: move(0,0,0) then motionY -= 0.08D; *= 0.9800000190734863D.
     * EntityLivingBase.java:2086 / :2099. */
    expect_my = -0.08 * 0.9800000190734863;
    expect(bits_eq_d(liv.base.phys.posY, 3.0), "air tick 1 keeps posY");
    expect(bits_eq_d(liv.base.phys.motionY, expect_my),
           "air tick 1 gravity*drag bits");
    expect(liv.base.phys.onGround == 0, "air tick 1 not onGround");

    for (i = 0; i < 80 && !liv.base.phys.onGround; ++i)
        ess_tick_living(&liv, 0.6f, stone, nstone, &st);
    expect(liv.base.phys.onGround == 1, "fall lands onGround");
    expect(liv.base.phys.posY > 0.99 && liv.base.phys.posY < 1.01,
           "landed feet near stone top y=1");

    ess_load_pose(&liv, EW_TYPE_ZOMBIE, 0.5, 1.0, 0.5,
                  0.4, 0.0, 0.0, 1, 0.0f, 0,
                  0, 0, 0, 0, 0, 0);
    slip = 0.6f;
    f6 = slip * 0.91f;
    ess_tick_living(&liv, slip, stone, nstone, &st);
    /* Ground: move then motionX *= (double)(slip*0.91F). Zero intents, so
     * no moveRelative. EntityLivingBase.java:2017 / :2100.
     * onGround is written after move as collidedVertically && d3<0
     * (Entity.java:970). dy==0 this tick so onGround clears; gravity then
     * queues the next tick's land. */
    expect(bits_eq_d(liv.base.phys.motionX, 0.4 * (double)f6),
           "ground tick damps motionX by slip*0.91F");
    expect(liv.base.phys.onGround == 0, "ground tick dy=0 clears onGround");
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
