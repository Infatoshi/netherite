/* EntityXPOrb lava / water / pushOut units. Dry motion stays bit-exact. */
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
#include "entity_xp_orb.h"
#include "xp_live.h"
#include "xp_world_tick.h"
#include "mc_blocks.h"

#include <stdio.h>
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
    o->delayBeforeCanPickup = 0;
}

int main(void) {
    McOrb dry, lava, stillw, flow, onblk;
    XlMock air, lavaw, pool, stream, floorw;
    McAABB stone;
    const double far = 100.0;
    const float eye = 1.62f;
    const double g = 0.029999999329447746;
    const double drag = 0.9800000190734863;
    const double lava_y = 0.20000000298023224;
    double dry_my;

    memset(&air, 0, sizeof air);
    air.ox = 0;
    air.oy = 60;
    air.oz = 0;

    fill_orb(&dry, 8.5, 66.5, 8.5);
    xl_tick_orb(&air, &dry, far, far, far, eye, 0);
    dry_my = -g * drag;
    expect(d_eq(dry.motionY, dry_my),
           "dry arena motionY is gravity then 0.98 drag");
    expect(d_eq(dry.motionX, 0.0) && d_eq(dry.motionZ, 0.0),
           "dry arena xz stay 0");

    /* Same dry tick via eo_tick: bit-identical to xl_tick_orb on air. */
    fill_orb(&onblk, 8.5, 66.5, 8.5);
    eo_tick(&onblk, far, far, far, eye, 0, NULL, 0, 0, 0, 0);
    expect(d_eq(onblk.motionX, dry.motionX) &&
           d_eq(onblk.motionY, dry.motionY) &&
           d_eq(onblk.motionZ, dry.motionZ),
           "eo_tick dry matches xl_tick_orb air");

    /* Still lava: motionY Java 0.2F then drag. xz CLASS C skipped. */
    memset(&lavaw, 0, sizeof lavaw);
    lavaw.ox = 0;
    lavaw.oy = 60;
    lavaw.oz = 0;
    mock_set(&lavaw, 8, 66, 8, BLK_LAVA, 0);
    fill_orb(&lava, 8.5, 66.5, 8.5);
    lava.motionX = 0.05;
    lava.motionZ = -0.03;
    xl_tick_orb(&lavaw, &lava, far, far, far, eye, 0);
    expect(d_eq(lava.motionY, lava_y * drag),
           "still lava motionY is 0.20000000298023224 * 0.98");
    expect(d_eq(lava.motionX, 0.05 * drag) && d_eq(lava.motionZ, -0.03 * drag),
           "lava xz CLASS C skipped, existing mx/mz only drag");

    /* Still source water: getFlow is ZERO, motion matches dry. */
    memset(&pool, 0, sizeof pool);
    pool.ox = 0;
    pool.oy = 60;
    pool.oz = 0;
    mock_set(&pool, 8, 66, 8, BLK_WATER, 0);
    fill_orb(&stillw, 8.5, 66.5, 8.5);
    xl_tick_orb(&pool, &stillw, far, far, far, eye, 0);
    expect(d_eq(stillw.motionX, dry.motionX) &&
           d_eq(stillw.motionY, dry.motionY) &&
           d_eq(stillw.motionZ, dry.motionZ),
           "still water handleMaterialAcceleration flow 0 == dry");

    /* Flowing: source at (8,66,8) depth 0, east neighbor depth 1.
     * getFlow of the overlapping source cell is +X unit, * 0.014 before gravity. */
    memset(&stream, 0, sizeof stream);
    stream.ox = 0;
    stream.oy = 60;
    stream.oz = 0;
    mock_set(&stream, 8, 66, 8, BLK_WATER, 0);
    mock_set(&stream, 9, 66, 8, BLK_WATER, 1);
    fill_orb(&flow, 8.5, 66.5, 8.5);
    {
        double mx0 = flow.motionX;
        xl_handle_water(&stream, &flow);
        expect(d_eq(flow.motionX, mx0 + 0.014),
               "handleWaterMovement 0.014 * unit +X World.java:2391-2394");
        expect(d_eq(flow.motionY, 0.0) && d_eq(flow.motionZ, 0.0),
               "still-current YZ unchanged before gravity");
    }

    /* pushOutOfBlocks: standing ON a cube is not collidesWithAnyBlock. */
    memset(&floorw, 0, sizeof floorw);
    floorw.ox = 0;
    floorw.oy = 60;
    floorw.oz = 0;
    mock_set(&floorw, 8, 65, 8, BLK_STONE, 0);
    fill_orb(&onblk, 8.5, 66.0, 8.5);
    stone = mc_aabb_make(8, 65, 8, 9, 66, 9);
    expect(eo_collides_with_any(&onblk, &stone, 1) == 0,
           "on-block feet minY==cube.maxY is not collidesWithAnyBlock");
    {
        McOrb inside;
        fill_orb(&inside, 8.5, 65.25, 8.5);
        expect(eo_collides_with_any(&inside, &stone, 1) == 1,
               "strict intersect inside stone is collidesWithAnyBlock");
    }

    expect(xl_lava_id(BLK_LAVA) && xl_lava_id(BLK_FLOWING_LAVA),
           "Material.LAVA is still 11 and flowing 10");
    expect(xl_liquid_height_percent(0) == (1.0f / 9.0f),
           "getLiquidHeightPercent(0)=1/9 BlockLiquid.java:60-68");

    if (fails) {
        fprintf(stderr, "test_xp_orbs: FAILED\n");
        return 1;
    }
    fprintf(stderr, "test_xp_orbs: PASS\n");
    return 0;
}
