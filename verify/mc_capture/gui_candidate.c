/* gui_candidate - render ONE container screen (player inventory / crafting
 * table / furnace / single chest) through the real gm_screen_draw path onto a
 * bare frame and dump a PPM, to pixel-diff the panel region against a live
 * Minecraft capture of the SAME screen (capture_gui.sh -> mc_gui_*.png).
 *
 * The runtime is a zeroed GmRuntime with only `container` set: an empty
 * inventory, empty grid, no furnace/chest bound (idle furnace draws the vanilla
 * 1px arrow slice), mouse parked at (5,5) so no slot is hovered and the
 * cursor pointer stays outside the panel crop. The 3D scene behind the
 * gradient dim is NOT compared (the diff crops to the panel inset).
 *
 *   gui_candidate --container 0|1|2|3 [--w 854 --h 480] [--mx N --my N] [--ppm PATH]
 *
 * Default mouse (5,5) parks the cursor outside the panel (no hover). Pass
 * framebuffer mouse coords for a second inventory look-at pose (e.g. slot A
 * at 282,258). Prints "PANEL x y w h scale" for the diff script.
 */
#include "game/screen.h"
#include "game/runtime.h"
#include "game/hud.h"
#include "core/config.h"   /* --set key=value -> cr_cfg_set (preview_* knobs) */

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
    int mx = 5, my = 5; /* parked: no slot hover, look-at near corner */
    const char *out = "/tmp/gui_candidate.ppm";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--container") && i + 1 < argc) container = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--w") && i + 1 < argc) W = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h") && i + 1 < argc) H = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mx") && i + 1 < argc) mx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--my") && i + 1 < argc) my = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) {
            /* Same key=value grammar as magma_game --set (registry only). */
            const char *kv = argv[++i];
            const char *eq = strchr(kv, '=');
            if (!eq || eq == kv) {
                fprintf(stderr, "bad --set %s (want key=value)\n", kv);
                return 2;
            }
            char key[64];
            size_t klen = (size_t)(eq - kv);
            if (klen >= sizeof key) {
                fprintf(stderr, "bad --set %s: key too long\n", kv);
                return 2;
            }
            memcpy(key, kv, klen);
            key[klen] = '\0';
            int rc = cr_cfg_set(key, eq + 1);
            if (rc != 0) {
                fprintf(stderr, "error: --set %s: %s\n", kv,
                        rc == -1 ? "unknown key" : "bad value for this key");
                return 2;
            }
        }
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    if (container < 0 || container > 3 || W < 320 || H < 240) {
        fprintf(stderr, "bad args\n"); return 2;
    }
    if (gm_hud_init()) { fprintf(stderr, "hud init failed\n"); return 1; }

    static GmRuntime r; /* zeroed: empty inventory/grid/cursor */
    r.container = container;
    r.active_furnace = -1;
    r.active_chest = -1;

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

    gm_screen_draw(&fb, &r, mx, my);

    if (write_ppm(out, fb.color, W, H)) { fprintf(stderr, "write failed\n"); return 1; }
    /* vanilla GuiContainer origin: floor((scaledW - 176) / 2) in gui units */
    int s = H / 240 > 1 ? H / 240 : 1;
    /* GuiChest centers with ySize=168; drawn texture is 167 tall. */
    int ph = container == 3 ? 168 : 166;
    int gw = (W + s - 1) / s, gh = (H + s - 1) / s;
    printf("PANEL %d %d %d %d %d\n", (gw - 176) / 2 * s, (gh - ph) / 2 * s,
           176 * s, ph * s, s);
    fprintf(stderr, "wrote %s (container %d, %dx%d)\n", out, container, W, H);
    return 0;
}
