/* gui_actions_candidate - replay the deterministic inventory click sequence
 * from capture_gui_actions.sh through magma's authoritative container_live
 * operations and render gm_screen_draw after every visible step.
 *
 * The oracle and magma use the same 854x480 player-inventory slot centers:
 * A=inventory 9, B=10, C=11, H0=hotbar 0, H1=hotbar 1. The initial loadout is
 * two stone in A and five dirt in H1. Output names match the oracle goldens.
 */
#include "game/screen.h"
#include "game/runtime.h"
#include "game/hud.h"
#include "container_click.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 854
#define H 480

static int write_ppm(const char *path, const CrRgba *px)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) {
        unsigned char rgb[3] = {px[i].r, px[i].g, px[i].b};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) != 0;
}

static int render_step(const char *outdir, const char *name,
                       const GmRuntime *r, int mx, int my)
{
    CrFramebuffer fb;
    char path[1024];
    fb.w = W;
    fb.h = H;
    fb.color = calloc((size_t)W * H, sizeof(*fb.color));
    fb.depth = 0;
    if (!fb.color) return 1;
    for (int i = 0; i < W * H; ++i)
        fb.color[i] = (CrRgba){120, 120, 120, 255};
    gm_screen_draw(&fb, r, mx, my);
    snprintf(path, sizeof path, "%s/magma_gui_action_%s.ppm", outdir, name);
    int failed = write_ppm(path, fb.color);
    free(fb.color);
    if (failed) fprintf(stderr, "write failed: %s\n", path);
    return failed;
}

static ICStack slot(const GmRuntime *r, int id)
{
    return isr_get_stack(&r->player.inv, id);
}

static int expect(const GmRuntime *r, int id, int item, int count,
                  int cursor_item, int cursor_count, const char *step)
{
    ICStack s = slot(r, id), c = gm_player_cursor();
    if (s.item == item && s.count == count &&
        c.item == cursor_item && c.count == cursor_count)
        return 0;
    fprintf(stderr,
            "%s state mismatch: slot%d=(%d,%d), cursor=(%d,%d)\n",
            step, id, s.item, s.count, c.item, c.count);
    return 1;
}

int main(int argc, char **argv)
{
    const char *outdir = argc == 2 ? argv[1] : "../verify/mc_capture";
    enum { AX = 282, AY = 258, BX = 318, BY = 258, CX = 354, CY = 258,
           H0X = 282, H0Y = 374 };
    static GmRuntime r;
    int failed = 0;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [OUTDIR]\n", argv[0]);
        return 2;
    }
    if (gm_hud_init()) {
        fprintf(stderr, "hud init failed\n");
        return 1;
    }
    r.container = 0;
    r.active_furnace = -1;
    gm_player_cursor_set(ic_empty());
    gm_runtime_set_inventory(&r, 9, 1, 2, 0);
    gm_runtime_set_inventory(&r, 1, 3, 5, 0);

    failed |= render_step(outdir, "00_initial", &r, AX, AY);

    gm_container_click(&r, 9, 0, CC_CLICK_PICKUP);
    failed |= expect(&r, 9, 0, 0, 1, 2, "01_pickup_a");
    failed |= render_step(outdir, "01_pickup_a", &r, AX, AY);

    gm_container_click(&r, 10, 0, CC_CLICK_PICKUP);
    failed |= expect(&r, 10, 1, 2, 0, 0, "02_place_b");
    failed |= render_step(outdir, "02_place_b", &r, BX, BY);

    gm_container_click(&r, 10, 1, CC_CLICK_PICKUP);
    failed |= expect(&r, 10, 1, 1, 1, 1, "03_split_b");
    failed |= render_step(outdir, "03_split_b", &r, BX, BY);

    gm_container_click(&r, 11, 1, CC_CLICK_PICKUP);
    failed |= expect(&r, 11, 1, 1, 0, 0, "04_deposit_one_c");
    failed |= render_step(outdir, "04_deposit_one_c", &r, CX, CY);

    gm_container_click(&r, 10, 0, CC_CLICK_QUICK_MOVE);
    failed |= expect(&r, 0, 1, 1, 0, 0, "05_shift_b_to_hotbar");
    failed |= render_step(outdir, "05_shift_b_to_hotbar", &r, BX, BY);

    gm_container_click(&r, 0, 0, CC_CLICK_PICKUP);
    gm_container_click(&r, 1, 0, CC_CLICK_PICKUP);
    gm_container_click(&r, 0, 0, CC_CLICK_PICKUP);
    failed |= expect(&r, 0, 3, 5, 0, 0, "06_swap_hotbar_0_1");
    failed |= expect(&r, 1, 1, 1, 0, 0, "06_swap_hotbar_0_1");
    failed |= render_step(outdir, "06_swap_hotbar_0_1", &r, H0X, H0Y);

    gm_container_click(&r, 0, 0, CC_CLICK_THROW);
    failed |= expect(&r, 0, 3, 4, 0, 0, "07_drop_one_hotbar0");
    failed |= render_step(outdir, "07_drop_one_hotbar0", &r, H0X, H0Y);

    gm_container_close(&r);
    if (gm_player_cursor().count != 0) {
        fprintf(stderr, "08_close state mismatch: cursor not empty\n");
        failed = 1;
    }

    if (failed) return 1;
    fprintf(stderr, "gui action candidate: PASS (8 visible states + close)\n");
    return 0;
}
