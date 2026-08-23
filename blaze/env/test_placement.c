/* Placement + TNT flint units and fixture baker.
 *
 * Units: TNT ignite, torch on air/wall, mayPlace player AABB, boat recipe,
 * container items. --write-fixture FROM OUT copies a magma region, plants a
 * ceiling/floor/TNT pad, seeds torch + flint. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "player_survival.h"
#include "block_may_place.h"
#include "crafting_recipes_full.h"
#include "crafting_remaining.h"
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
    if (s->light) {
        unsigned char sky = (id == 0) ? 15 : 0;
        s->light[idx] = (unsigned char)(sky << 4);
    }
}

static void fill_flat(Chunk *win) {
    int ci, lx, lz, y;
    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
    for (ci = 0; ci < PSV_NCHUNKS; ++ci) {
        win[ci].cx = (ci % PSV_DIM) - PSV_R;
        win[ci].cz = (ci / PSV_DIM) - PSV_R;
        for (lx = 0; lx < 16; ++lx)
            for (lz = 0; lz < 16; ++lz)
                for (y = 0; y <= 64; ++y)
                    mc_set(&win[ci], lx, y, lz, mc_state(BLK_STONE, 0));
    }
}

static int write_chain(const char *path) {
    FILE *f = fopen(path, "w");
    int t;
    if (!f) {
        fprintf(stderr, "open %s\n", path);
        return 0;
    }
    fputc('[', f);
    for (t = 0; t < 96; ++t) {
        if (t) fputc(',', f);
        if (t == 0)
            fputs("{\"dpitch\":-89,\"use\":1,\"hotbar\":0}", f);
        else if (t == 1)
            fputs("{\"dpitch\":178}", f);
        else if (t == 5)
            fputs("{\"use\":1,\"hotbar\":0}", f);
        else if (t == 6)
            fputs("{\"dpitch\":-89}", f);
        else if (t == 10)
            fputs("{\"use\":1,\"hotbar\":1}", f);
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
    s.head.yaw = 0.0f;
    s.head.pitch = 0.0f;
    s.head.px = 8.5;
    s.head.pz = 8.5;
    s.head.box[0] = 8.5 - 0.3;
    s.head.box[2] = 8.5 - 0.3;
    s.head.box[3] = 8.5 + 0.3;
    s.head.box[5] = 8.5 + 0.3;
    s.head.hotbar_sel = 0;
    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 0;
    for (i = 0; i < 37; ++i)
        s.head.inv[i][0] = s.head.inv[i][1] = s.head.inv[i][2] = 0;
    s.head.inv[0][0] = 50;
    s.head.inv[0][1] = 64; /* torch */
    s.head.inv[1][0] = 259;
    s.head.inv[1][1] = 1; /* flint and steel */

    for (x = 6; x <= 10; ++x)
        for (z = 6; z <= 14; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 70; ++y)
                plant_cell(&s, x, y, z, 0, 0);
        }
    plant_cell(&s, 8, 68, 8, BLK_STONE, 0); /* ceiling: torch-on-air */
    plant_cell(&s, 8, 65, 11, BLK_STONE, 0);
    plant_cell(&s, 8, 66, 11, BLK_TNT, 0);

    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s torch+flint player (8.5,65,8.5) TNT (8,66,11) "
            "ceiling (8,68,8)\n",
            out_path);
    blaze_snapshot_free(&s);
    return write_chain("blaze/rl/fixtures/placement_s10.json");
}

static int run_units(void) {
    Chunk win[PSV_NCHUNKS];
    McAABB bb;
    CRRecipe R[CRF_NRECIPES];
    CRStack grid[9];
    int n, meta, i;
    double sx, sy, sz;

    fill_flat(win);
    expect(ibp_tnt_flint_activate(BLK_TNT, IBP_ITEM_FLINT_AND_STEEL),
           "flint on TNT activates");
    expect(!ibp_tnt_flint_activate(1, IBP_ITEM_FLINT_AND_STEEL),
           "flint on stone does not");
    expect(!ibp_flint_broke(0, &meta) && meta == 1, "flint damage 0 -> 1");
    expect(ibp_flint_broke(64, &meta), "flint 64 -> broke");
    ibp_tnt_primed_pos(7, 65, 12, &sx, &sy, &sz);
    expect(sx == (double)((float)7 + 0.5f) && sy == 65.0,
           "TNT spawn x+0.5F");

    expect(!ibp_may_place(win, IBP_BLK_TORCH, 8, 66, 8, IBP_UP, NULL),
           "torch on air rejected");
    psv_set_block(win, 9, 65, 8, BLK_STONE);
    expect(ibp_may_place(win, IBP_BLK_TORCH, 8, 65, 8, IBP_WEST, NULL),
           "torch on wall accepted");
    bb = psv_player_box(8.5, 65.0, 8.5);
    expect(!ibp_may_place(win, BLK_STONE, 8, 65, 8, IBP_UP, &bb),
           "mayPlace stone vs player AABB rejected");
    expect(ibp_may_place(win, IBP_BLK_TORCH, 8, 65, 8, IBP_UP, &bb),
           "mayPlace torch NULL_AABB vs player accepted");
    psv_set_block(win, 10, 64, 10, BLK_DIRT);
    expect(ibp_may_place(win, IBP_BLK_SAPLING, 10, 65, 10, IBP_UP, NULL),
           "sapling on dirt accepted");
    expect(!ibp_may_place(win, IBP_BLK_SAPLING, 8, 65, 8, IBP_UP, NULL),
           "sapling on stone rejected");

    n = crf_build(R);
    expect(n == CRF_NRECIPES, "crf_build count");
    for (i = 0; i < 9; ++i) grid[i] = crf_empty();
    grid[0] = crf_mk(5, 1, 0);
    grid[2] = crf_mk(5, 1, 0);
    grid[3] = crf_mk(5, 1, 0);
    grid[4] = crf_mk(5, 1, 0);
    grid[5] = crf_mk(5, 1, 0);
    {
        CRStack boat = crf_findMatching(R, n, grid);
        expect(boat.item == 333 && boat.count == 1, "oak boat recipe");
    }
    expect(crf_container_item(335) == 325, "milk -> bucket");
    expect(crf_container_item(437) == 374, "dragon breath -> bottle");
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
