/* gui_candidate - render ONE container screen (player inventory / crafting
 * table / furnace) through the real gm_screen_draw path onto a bare frame and
 * dump a PPM, to pixel-diff the 176x166 panel region against a live Minecraft
 * capture of the SAME screen (capture_gui.sh -> mc_gui_*.png).
 *
 * The runtime is a zeroed GmRuntime with only `container` set: an empty
 * inventory, empty grid, no furnace bound (idle furnace draws the vanilla
 * 1px arrow slice), mouse parked at (5,5) so no slot is hovered and the
 * cursor pointer stays outside the panel crop. The 3D scene behind the
 * gradient dim is NOT compared (the diff crops to the panel inset).
 *
 *   gui_candidate --container 0|1|2 [--w 854 --h 480] [--ppm PATH]
 *
 * Prints "PANEL x y w h scale" (framebuffer px) for the diff script.
 */
#include "game/screen.h"
#include "game/runtime.h"
#include "game/hud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_ppm(const char *path, const CrRgba *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char rgb[3] = { px[i].r, px[i].g, px[i].b };
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) != 0;
}

int main(int argc, char **argv)
{
    int container = 1, W = 854, H = 480;
    const char *out = "/tmp/gui_candidate.ppm";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--container") && i + 1 < argc) container = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--w") && i + 1 < argc) W = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h") && i + 1 < argc) H = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) out = argv[++i];
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    if (container < 0 || container > 2 || W < 320 || H < 240) {
        fprintf(stderr, "bad args\n"); return 2;
    }
    if (gm_hud_init()) { fprintf(stderr, "hud init failed\n"); return 1; }

    static GmRuntime r; /* zeroed: empty inventory/grid/cursor */
    r.container = container;
    r.active_furnace = -1;

    CrFramebuffer fb;
    fb.w = W; fb.h = H;
    fb.color = calloc((size_t)W * H, sizeof(CrRgba));
    fb.depth = 0;
    if (!fb.color) return 1;
    /* flat mid-gray stand-in for the 3D scene; not part of the panel diff */
    for (int i = 0; i < W * H; i++) {
        CrRgba g = { 120, 120, 120, 255 };
        fb.color[i] = g;
    }

    gm_screen_draw(&fb, &r, 5, 5);

    if (write_ppm(out, fb.color, W, H)) { fprintf(stderr, "write failed\n"); return 1; }
    /* vanilla GuiContainer origin: floor((scaledW - 176) / 2) in gui units */
    int s = H / 240 > 1 ? H / 240 : 1;
    int gw = (W + s - 1) / s, gh = (H + s - 1) / s;
    printf("PANEL %d %d %d %d %d\n", (gw - 176) / 2 * s, (gh - 166) / 2 * s,
           176 * s, 166 * s, s);
    fprintf(stderr, "wrote %s (container %d, %dx%d)\n", out, container, W, H);
    return 0;
}
