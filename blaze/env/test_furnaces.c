/* Furnace units + fixture baker.
 *
 * Units: Java-derived recipe/fuel/XP table (verify/furnace_registry.py),
 * lava-bucket container, fillBucket, hotbar loop, food stats.
 * --write-fixture FROM OUT copies a magma region, plants a furnace on the
 * look ray, and seeds the hotbar with coal + raw beef. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "furnace_full_tick.h"
#include "inventory_stack_rules.h"
#include "items_core.h"
#include "mc_blocks.h"
#include "smelting_recipes.h"
#include "../../out/verify/furnace_registry_expect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLK_FURNACE 61

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
    /* Same look-ray as chests: yaw 180 / pitch 0 from standing eye y=66.62
     * hits (8,66,6). */
    for (y = 62; y <= 70; ++y)
        plant_cell(&s, 8, y, 6, 0, 0);
    plant_cell(&s, 8, 65, 6, BLK_DIRT, 0);
    plant_cell(&s, 8, 66, 6, BLK_FURNACE, 2);
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
    s.head.inv[0][0] = 263; s.head.inv[0][1] = 1; /* coal Item.java:1504 */
    s.head.inv[1][0] = 363; s.head.inv[1][1] = 1; /* beef Item.java:1605 */
    s.head.hotbar_sel = 0;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s furnace (8,66,6)=%d dirt(8,65,6)=%d "
            "py=%g on_ground=%d yaw=%g inv0=%d x%d inv1=%d x%d\n",
            out_path, cell_id(&s, 8, 66, 6), cell_id(&s, 8, 65, 6),
            s.head.py, s.head.on_ground, (double)s.head.yaw,
            s.head.inv[0][0], s.head.inv[0][1],
            s.head.inv[1][0], s.head.inv[1][1]);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    SRRecipe R[SR_NRECIPES];
    int n, i;

    n = sr_build(R);
    expect(n == FRE_NRECIPES, "sr_build count matches Java");
    for (i = 0; i < n && i < FRE_NRECIPES; ++i) {
        const FreRecipe *e = &FRE_RECIPES[i];
        expect(R[i].input.item == e->in_item && R[i].input.meta == e->in_meta &&
                   R[i].output.item == e->out_item &&
                   R[i].output.count == e->out_count &&
                   R[i].output.meta == e->out_meta && R[i].xp == e->xp,
               "recipe row matches Java");
        if (fails) {
            fprintf(stderr, "  at %d C=%d:%d->%d Java=%d:%d->%d xp C=%g J=%g\n",
                    i, R[i].input.item, R[i].input.meta, R[i].output.item,
                    e->in_item, e->in_meta, e->out_item,
                    (double)R[i].xp, (double)e->xp);
            break;
        }
    }
    for (i = 0; i < FRE_NFUELS; ++i)
        expect(sr_getItemBurnTime(sr_mk(FRE_FUELS[i].id, 1, 0)) ==
                   FRE_FUELS[i].burn,
               "fuel burn matches Java");
    expect(isr_max_stack_size(325, 0) == 16, "empty bucket stacks to 16");
    expect(isr_max_stack_size(327, 0) == 1, "lava bucket stacks to 1");
    {
        FftFurnace f;
        int t;
        memset(&f, 0, sizeof f);
        f.nrecipes = sr_build(f.recipes);
        f.slot0 = sr_mk(363, 1, 0);
        f.slot1 = sr_mk(263, 1, 0);
        f.slot2 = sr_empty();
        f.total_cook = TE_COOK_TICKS;
        for (t = 0; t < TE_COOK_TICKS; ++t) fft_tick(&f);
        expect(f.slot2.item == 364 && f.slot2.count == 1,
               "coal + beef smelts cooked beef in 200 ticks");
        expect(f.slot0.count == 0, "input consumed");
    }
    {
        FftFurnace f;
        memset(&f, 0, sizeof f);
        f.nrecipes = sr_build(f.recipes);
        f.slot0 = sr_mk(15, 1, 0);
        f.slot1 = sr_mk(327, 1, 0);
        f.total_cook = TE_COOK_TICKS;
        fft_tick(&f);
        expect(f.slot1.item == 325 && f.slot1.count == 1,
               "lava bucket leaves empty bucket");
    }
    expect(ic_food_info(365, 0).potion_prob == 0.3f, "chicken potion 0.3");
    expect(ic_food_info(364, 0).hunger == 8, "cooked beef hunger 8");
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
