/* World.mayPlace / BlockTNT flint / boat recipe / container items. */
#include "player_survival.h"
#include "block_may_place.h"
#include "crafting_recipes_full.h"
#include "crafting_remaining.h"

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

int main(void) {
    Chunk win[PSV_NCHUNKS];
    McAABB bb;
    CRRecipe R[CRF_NRECIPES];
    CRStack grid[9];
    int n, meta, i;
    double sx, sy, sz;

    fill_flat(win);

    expect(ibp_tnt_flint_activate(BLK_TNT, IBP_ITEM_FLINT_AND_STEEL),
           "flint on TNT activates");
    expect(!ibp_tnt_flint_activate(BLK_TNT, 50), "torch on TNT does not");
    expect(!ibp_flint_broke(0, &meta) && meta == 1, "flint damage 0 -> 1");
    expect(!ibp_flint_broke(63, &meta) && meta == 64, "flint 63 -> 64 lives");
    expect(ibp_flint_broke(64, &meta), "flint 64 -> broke");
    ibp_tnt_primed_pos(7, 65, 12, &sx, &sy, &sz);
    expect(sx == (double)((float)7 + 0.5f) && sy == 65.0 &&
               sz == (double)((float)12 + 0.5f),
           "TNT spawn at (float)x+0.5F, y, (float)z+0.5F");

    /* Torch on air (no support) at (8,66,8). */
    expect(!ibp_torch_can_place_block_at(win, 8, 66, 8),
           "torch canPlaceBlockAt air is false");
    expect(!ibp_may_place(win, IBP_BLK_TORCH, 8, 66, 8, IBP_UP, NULL),
           "mayPlace torch on air rejected");
    expect(ibp_torch_placement_meta(win, 8, 66, 8, IBP_UP) < 0,
           "torch placement meta on air is -1");

    /* Torch on a wall: stone at (9,65,8), place cell (8,65,8) facing WEST. */
    psv_set_block(win, 9, 65, 8, BLK_STONE);
    expect(ibp_torch_can_place_at(win, 8, 65, 8, IBP_WEST),
           "torch canPlaceAt west wall (stone at x+1)");
    expect(ibp_may_place(win, IBP_BLK_TORCH, 8, 65, 8, IBP_WEST, NULL),
           "mayPlace torch on wall accepted");
    expect(ibp_torch_placement_meta(win, 8, 65, 8, IBP_WEST) == 2,
           "wall torch meta WEST=2");

    /* Player AABB vs solid place. Standing in (8,65,8). */
    bb = psv_player_box(8.5, 65.0, 8.5);
    expect(!ibp_may_place(win, BLK_STONE, 8, 65, 8, IBP_UP, &bb),
           "mayPlace stone intersecting player AABB rejected");
    expect(ibp_may_place(win, IBP_BLK_TORCH, 8, 65, 8, IBP_UP, &bb),
           "mayPlace torch skips NULL_AABB vs player");

    /* Sapling on dirt vs stone. */
    psv_set_block(win, 10, 64, 10, BLK_DIRT);
    expect(ibp_may_place(win, IBP_BLK_SAPLING, 10, 65, 10, IBP_UP, NULL),
           "sapling on dirt accepted");
    expect(!ibp_may_place(win, IBP_BLK_SAPLING, 8, 65, 8, IBP_UP, NULL),
           "sapling on stone rejected");

    n = crf_build(R);
    expect(n == CRF_NRECIPES, "crf_build fills CRF_NRECIPES");
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
    expect(crf_container_item(335) == 325, "milk bucket -> empty bucket");
    expect(crf_container_item(326) == 325, "water bucket -> empty bucket");
    expect(crf_container_item(437) == 374, "dragon breath -> glass bottle");
    expect(crf_container_item(50) == 0, "torch has no container");

    for (i = 0; i < 9; ++i) grid[i] = crf_empty();
    grid[0] = crf_it(335);
    grid[1] = crf_it(335);
    grid[2] = crf_it(335);
    grid[3] = crf_it(353);
    grid[4] = crf_it(344);
    grid[5] = crf_it(353);
    grid[6] = crf_it(296);
    grid[7] = crf_it(296);
    grid[8] = crf_it(296);
    {
        CRStack cake = crf_findMatching(R, n, grid);
        expect(cake.item == 354 && cake.count == 1, "cake recipe");
    }

    return fails ? 1 : 0;
}
