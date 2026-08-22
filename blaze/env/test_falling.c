/* Falling-block unit tests + fixture baker.
 *
 * Units: gravity bits, spawn y offset, canFallThrough, land on stone,
 * 600-tick drop. --write-fixture FROM OUT copies a magma region and plants
 * a dirt-supported sand column north of the s10 spawn look ray. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
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

typedef struct {
    unsigned char id[32][32][32];
    unsigned char meta[32][32][32];
} FlWorld;

typedef struct {
    int active, type;
    double x, y, z, mx, my, mz;
    int on_ground, age, item, count, meta, pickup_delay, lifespan;
} FlEnt;

typedef struct {
    int active, x, y, z, block_id;
    long long due_tick;
} FlUpd;

typedef struct {
    int active, x, y, z, block_id, block_meta;
    long long due_tick;
} FlLand;

typedef struct {
    FlEnt ents[48];
    FlUpd fall_updates[128];
    FlLand fall_landings[48];
    int n_active;
    int ticks;
} FlStore;

static int fw_id(FlWorld *w, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= 32 || y >= 32 || z >= 32) return 0;
    return (int)w->id[x][y][z];
}

static int fw_meta(FlWorld *w, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= 32 || y >= 32 || z >= 32) return 0;
    return (int)w->meta[x][y][z];
}

static void fw_set(FlWorld *w, int x, int y, int z, int id, int meta) {
    if (x < 0 || y < 0 || z < 0 || x >= 32 || y >= 32 || z >= 32) return;
    w->id[x][y][z] = (unsigned char)id;
    w->meta[x][y][z] = (unsigned char)(meta & 15);
}

#define FL_W FlWorld
#define fl_id(w, x, y, z) fw_id((w), (x), (y), (z))
#define fl_meta(w, x, y, z) fw_meta((w), (x), (y), (z))
#define fl_set(w, x, y, z, id, meta) fw_set((w), (x), (y), (z), (id), (meta))
#define FL_STORE FlStore
#include "falling_live.h"

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

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    int y;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    /* s10 spawn (8.5, 66, 8.5) yaw 180 looks -Z. Dirt at y=68, sand 69-71,
     * air shaft below so the column falls ~6 blocks after the dirt breaks. */
    for (y = 62; y <= 71; ++y)
        plant_cell(&s, 8, y, 6, 0, 0);
    plant_cell(&s, 8, 62, 6, BLK_STONE, 0);
    plant_cell(&s, 8, 68, 6, BLK_DIRT, 0);
    plant_cell(&s, 8, 69, 6, BLK_SAND, 0);
    plant_cell(&s, 8, 70, 6, BLK_SAND, 0);
    plant_cell(&s, 8, 71, 6, BLK_SAND, 0);
    s.head.yaw = 180.0f;
    s.head.pitch = -24.0f;
    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 0;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s sand column (8,69-71,6) on dirt (8,68,6) "
            "floor stone (8,62,6) below=%d dirt=%d sand=%d/%d/%d "
            "yaw=%g pitch=%g\n",
            out_path, cell_id(&s, 8, 67, 6), cell_id(&s, 8, 68, 6),
            cell_id(&s, 8, 69, 6), cell_id(&s, 8, 70, 6), cell_id(&s, 8, 71, 6),
            (double)s.head.yaw, (double)s.head.pitch);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    FlWorld w;
    FlStore st;
    double expect_my;
    int t, landed;

    memset(&w, 0, sizeof w);
    memset(&st, 0, sizeof st);

    expect(fl_is_gravity(BLK_SAND) && fl_is_gravity(BLK_GRAVEL),
           "sand and gravel are gravity blocks");
    expect(!fl_is_gravity(BLK_DIRT), "dirt is not a gravity block");
    expect(fl_can_fall_through(BLK_AIR) && fl_can_fall_through(51) &&
               fl_can_fall_through(BLK_WATER) &&
               fl_can_fall_through(BLK_LAVA),
           "canFallThrough air/fire/water/lava");
    expect(!fl_can_fall_through(BLK_DIRT) && !fl_can_fall_through(31),
           "canFallThrough rejects dirt and plants");

    fw_set(&w, 4, 0, 4, BLK_STONE, 0);
    fw_set(&w, 4, 8, 4, BLK_SAND, 0);
    expect(fl_spawn(&st, 4, 8, 4, BLK_SAND, 0), "spawn falling sand");
    expect(bits_eq_d(st.ents[0].y, 8.0 + (double)((1.0f - 0.98f) / 2.0f)),
           "spawn y is blockY + (1.0F-0.98F)/2");
    expect(bits_eq_d(st.ents[0].x, 4.5) && bits_eq_d(st.ents[0].z, 4.5),
           "spawn xz is block center");
    expect(st.ents[0].lifespan == 600, "fallTime cap field is 600");

    fl_tick_entity(&st, &w, 0);
    expect_my = -0.03999999910593033 * 0.9800000190734863;
    expect(bits_eq_d(st.ents[0].my, expect_my),
           "air tick 1 gravity*drag bits");
    expect(st.ents[0].age == 1, "fallTime/age is 1 after first onUpdate");
    expect(fw_id(&w, 4, 8, 4) == 0, "source cell is air after first tick");

    landed = 0;
    for (t = 0; t < 80 && st.ents[0].active; ++t)
        fl_tick_entity(&st, &w, 0);
    if (!st.ents[0].active) {
        int i;
        for (i = 0; i < 48; ++i)
            if (st.fall_landings[i].active) {
                landed = 1;
                expect(st.fall_landings[i].y == 1, "landing y is stone top");
                expect(st.fall_landings[i].block_id == BLK_SAND,
                       "landing block is sand");
                expect(st.fall_landings[i].due_tick == 1,
                       "landing packet is ticks+1");
                st.ticks = 1;
                fl_pre_player_tick(&st, &w);
                expect(fw_id(&w, 4, 1, 4) == BLK_SAND,
                       "pre_player_tick places the landing block");
            }
    }
    expect(landed, "fall lands and queues placement");

    memset(&st, 0, sizeof st);
    memset(&w, 0, sizeof w);
    expect(fl_spawn(&st, 4, 10, 4, BLK_SAND, 0), "spawn for 600-tick drop");
    st.ents[0].age = 600;
    fl_tick_entity(&st, &w, 0);
    expect(!st.ents[0].active, "age>600 despawns in the void");

    memset(&st, 0, sizeof st);
    memset(&w, 0, sizeof w);
    fw_set(&w, 4, 5, 4, BLK_SAND, 0);
    fw_set(&w, 4, 4, 4, 0, 0);
    fl_block_changed(&st, &w, 4, 5, 4);
    expect(st.fall_updates[0].active && st.fall_updates[0].due_tick == 2,
           "block_changed schedules tickRate=2");
    st.ticks = 2;
    fl_tick_scheduled(&st, &w);
    expect(st.ents[0].active && st.ents[0].type == 2,
           "scheduled update spawns falling entity");
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
