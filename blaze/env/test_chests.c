/* Chest unit tests + fixture baker.
 *
 * Units: TileEntityChest 27-slot insert/open/close/tick
 * (tile_entity_chest.h). --write-fixture FROM OUT copies a magma region,
 * grounds the s10 player, plants a chest on the look ray, and seeds the
 * hotbar with known stacks. Worldgen loot tables are a named gap: the
 * TE is created empty on first interact. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "mc_blocks.h"
#include "tile_entity_chest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLK_CHEST 54

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
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
    int i, y;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    /* Same look-ray as falling_blocks: yaw 180 / pitch 0 from standing
     * eye y=66.62 hits (8,66,6). Air the column so the chest is the
     * top-of-column scan hit (rl_do_interact always-list may still miss
     * 54; the top block is always cached). */
    for (y = 62; y <= 70; ++y)
        plant_cell(&s, 8, y, 6, 0, 0);
    plant_cell(&s, 8, 65, 6, BLK_DIRT, 0);
    plant_cell(&s, 8, 66, 6, BLK_CHEST, 2);
    s.head.py = 65.0;
    s.head.box[1] = 65.0;
    s.head.box[4] = 65.0 + 1.8;
    s.head.on_ground = 1;
    s.head.mx = s.head.my = s.head.mz = 0.0;
    s.head.yaw = 180.0f;
    s.head.pitch = 0.0f;
    s.head.container = 0;
    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 0;
    for (i = 0; i < 37; ++i) {
        s.head.inv[i][0] = 0;
        s.head.inv[i][1] = 0;
        s.head.inv[i][2] = 0;
    }
    /* Known loot starts in the player inventory. First interact creates
     * an empty TE; the action chain moves stacks both ways. */
    s.head.inv[0][0] = 4;   s.head.inv[0][1] = 16; /* cobble */
    s.head.inv[1][0] = 260; s.head.inv[1][1] = 32; /* apple */
    s.head.inv[2][0] = 297; s.head.inv[2][1] = 10; /* bread */
    s.head.hotbar_sel = 0;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s chest (8,66,6)=%d dirt(8,65,6)=%d "
            "py=%g on_ground=%d yaw=%g inv0=%d x%d inv1=%d x%d inv2=%d x%d\n",
            out_path, cell_id(&s, 8, 66, 6), cell_id(&s, 8, 65, 6),
            s.head.py, s.head.on_ground, (double)s.head.yaw,
            s.head.inv[0][0], s.head.inv[0][1],
            s.head.inv[1][0], s.head.inv[1][1],
            s.head.inv[2][0], s.head.inv[2][1]);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    TeChest c;
    TecStack leftover;

    tec_init(&c);
    expect(tec_total_items(&c) == 0 && c.num_players_using == 0,
           "fresh chest is empty");
    leftover = tec_add_item(&c, tec_mk(TEC_APPLE, 20, 0));
    expect(tec_is_empty(&leftover) && c.slots[0].item == TEC_APPLE &&
               c.slots[0].count == 20,
           "addItem occupies slot 0");
    leftover = tec_add_item(&c, tec_mk(TEC_APPLE, 50, 0));
    expect(tec_is_empty(&leftover) && c.slots[0].count == 64 &&
               c.slots[1].count == 6,
           "addItem merges slot 0 to 64 then overflow occupies slot 1");
    tec_open(&c);
    expect(c.num_players_using == 1, "openInventory increments numPlayersUsing");
    tec_tick(&c);
    expect(c.lid_angle > 0.0f, "lid opens while players using");
    tec_close(&c);
    expect(c.num_players_using == 0, "closeInventory decrements");
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
