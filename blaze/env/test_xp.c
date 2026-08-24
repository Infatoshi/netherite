/* XP live-tick unit tests + fixture baker.
 *
 * Units: getXPSplit, xpBarCap, addExperience level-up, 6000 despawn,
 * onCollideWithPlayer xpCooldown 2, lava motionY, still/flowing water,
 * dry bit-exact, pushOutOfBlocks on-block gate.
 * --write-fixture FROM OUT plants a roofed stone pad and one orb at
 * (8.5, 66.5, 11.5) value 17 delay 10. */
#define _POSIX_C_SOURCE 200809L
#define XL_MOCK_N 16
typedef struct {
    int id[XL_MOCK_N][XL_MOCK_N][XL_MOCK_N];
    int meta[XL_MOCK_N][XL_MOCK_N][XL_MOCK_N];
    int ox, oy, oz;
} XlMock;
static int xl_mock_id(XlMock *w, int x, int y, int z);
static int xl_mock_meta(XlMock *w, int x, int y, int z);
#define XL_W XlMock
#define xl_id(w, x, y, z) xl_mock_id((w), (x), (y), (z))
#define xl_meta(w, x, y, z) xl_mock_meta((w), (x), (y), (z))
#include "xp_live.h"
#include "xp_world_tick.h"
#include "blaze_snapshot.h"
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

static int d_eq(double a, double b) {
    return memcmp(&a, &b, sizeof a) == 0;
}

static int xl_mock_id(XlMock *w, int x, int y, int z) {
    int ix, iy, iz;
    if (!w) return 0;
    ix = x - w->ox;
    iy = y - w->oy;
    iz = z - w->oz;
    if (ix < 0 || iy < 0 || iz < 0 ||
        ix >= XL_MOCK_N || iy >= XL_MOCK_N || iz >= XL_MOCK_N)
        return 0;
    return w->id[ix][iy][iz];
}

static int xl_mock_meta(XlMock *w, int x, int y, int z) {
    int ix, iy, iz;
    if (!w) return 0;
    ix = x - w->ox;
    iy = y - w->oy;
    iz = z - w->oz;
    if (ix < 0 || iy < 0 || iz < 0 ||
        ix >= XL_MOCK_N || iy >= XL_MOCK_N || iz >= XL_MOCK_N)
        return 0;
    return w->meta[ix][iy][iz];
}

static void mock_set(XlMock *w, int x, int y, int z, int id, int meta) {
    int ix = x - w->ox, iy = y - w->oy, iz = z - w->oz;
    if (ix < 0 || iy < 0 || iz < 0 ||
        ix >= XL_MOCK_N || iy >= XL_MOCK_N || iz >= XL_MOCK_N)
        return;
    w->id[ix][iy][iz] = id;
    w->meta[ix][iy][iz] = meta & 15;
}

static void fill_orb(McOrb *o, double x, double y, double z) {
    memset(o, 0, sizeof *o);
    eo_set_position(o, x, y, z);
    o->xpValue = 17;
    o->eid = 1000;
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

    {
        McOrb dry, lava, stillw, flow, onblk;
        XlMock air, lavaw, pool, stream;
        McAABB stone;
        const double far = 100.0;
        const float eye = 1.62f;
        const double g = 0.029999999329447746;
        const double drag = 0.9800000190734863;
        const double lava_y = 0.20000000298023224;
        double dry_my;

        memset(&air, 0, sizeof air);
        air.oy = 60;
        fill_orb(&dry, 8.5, 66.5, 8.5);
        xl_tick_orb(&air, &dry, far, far, far, eye, 0);
        dry_my = -g * drag;
        expect(d_eq(dry.motionY, dry_my),
               "dry arena motionY is gravity then 0.98 drag");
        expect(d_eq(dry.motionX, 0.0) && d_eq(dry.motionZ, 0.0),
               "dry arena xz stay 0");
        fill_orb(&onblk, 8.5, 66.5, 8.5);
        eo_tick(&onblk, far, far, far, eye, 0, NULL, 0, 0, 0, 0);
        expect(d_eq(onblk.motionY, dry.motionY),
               "eo_tick dry matches xl_tick_orb air");

        memset(&lavaw, 0, sizeof lavaw);
        lavaw.oy = 60;
        mock_set(&lavaw, 8, 66, 8, BLK_LAVA, 0);
        fill_orb(&lava, 8.5, 66.5, 8.5);
        lava.motionX = 0.05;
        lava.motionZ = -0.03;
        xl_tick_orb(&lavaw, &lava, far, far, far, eye, 0);
        expect(d_eq(lava.motionY, lava_y * drag),
               "still lava motionY is 0.20000000298023224 * 0.98");
        expect(d_eq(lava.motionX, 0.05 * drag) &&
               d_eq(lava.motionZ, -0.03 * drag),
               "lava xz CLASS C skipped, existing mx/mz only drag");

        memset(&pool, 0, sizeof pool);
        pool.oy = 60;
        mock_set(&pool, 8, 66, 8, BLK_WATER, 0);
        fill_orb(&stillw, 8.5, 66.5, 8.5);
        xl_tick_orb(&pool, &stillw, far, far, far, eye, 0);
        expect(d_eq(stillw.motionY, dry.motionY) &&
               d_eq(stillw.motionX, dry.motionX),
               "still water handleMaterialAcceleration flow 0 == dry");

        memset(&stream, 0, sizeof stream);
        stream.oy = 60;
        mock_set(&stream, 8, 66, 8, BLK_WATER, 0);
        mock_set(&stream, 9, 66, 8, BLK_WATER, 1);
        fill_orb(&flow, 8.5, 66.5, 8.5);
        xl_handle_water(&stream, &flow);
        expect(d_eq(flow.motionX, 0.014),
               "handleWaterMovement 0.014 * unit +X World.java:2391-2394");

        fill_orb(&onblk, 8.5, 66.0, 8.5);
        stone = mc_aabb_make(8, 65, 8, 9, 66, 9);
        expect(eo_collides_with_any(&onblk, &stone, 1) == 0,
               "on-block feet minY==cube.maxY is not collidesWithAnyBlock");
    }
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
