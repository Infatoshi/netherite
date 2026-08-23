/* XP live-tick unit tests + fixture baker.
 *
 * Units: getXPSplit, xpBarCap, addExperience level-up, 6000 despawn,
 * onCollideWithPlayer xpCooldown 2.
 * --write-fixture FROM OUT plants a roofed stone pad and one orb at
 * (8.5, 66.5, 11.5) value 17 delay 10. */
#define _POSIX_C_SOURCE 200809L
#define XL_NO_ORBS
#include "blaze_snapshot.h"
#include "xp_live.h"
#include "mc_blocks.h"

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
    RlSnapOrb *o;
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
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 67; ++y)
                plant_cell(&s, x, y, z, 0, 0);
            plant_cell(&s, x, 68, z, BLK_STONE, 0);
        }

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 0;
    s.n_orbs = 1;
    o = &s.orbs[0];
    memset(o, 0, sizeof *o);
    o->x = 8.5;
    o->y = 66.5;
    o->z = 11.5;
    o->my = 0.2;
    o->xpValue = 17;
    o->delayBeforeCanPickup = 10;
    o->eid = 1000;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr, "WROTE %s orb (8.5,66.5,11.5) value=17 delay=10 n_orbs=%u\n",
            out_path, s.n_orbs);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    XlPlayer p;

    expect(xl_xp_split(17) == 17, "getXPSplit(17)=17");
    expect(xl_xp_split(18) == 17, "getXPSplit(18)=17");
    expect(xl_xp_split(2477) == 2477, "getXPSplit(2477)=2477");
    expect(xl_xp_bar_cap(0) == 7, "xpBarCap(0)=7");
    expect(xl_xp_bar_cap(15) == 37, "xpBarCap(15)=37");
    expect(xl_xp_bar_cap(30) == 112, "xpBarCap(30)=112");
    expect(xl_mob_xp(2) == 5, "EntityMob experienceValue is 5");

    memset(&p, 0, sizeof p);
    xl_add_experience(&p, 17);
    expect(p.experienceTotal == 17, "addExperience 17 totals 17");
    expect(p.experienceLevel == 2, "17 XP from 0 reaches level 2");
    expect(p.experience > 0.0f && p.experience < 1.0f,
           "fractional bar after two level-ups");
    expect(XL_DESPAWN == 6000, "xpOrbAge despawn is 6000");
    expect(XL_COOLDOWN == 2, "onCollideWithPlayer xpCooldown is 2");
    p.xpCooldown = 2;
    xl_player_tick(&p);
    expect(p.xpCooldown == 1, "xpCooldown decrements");
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
