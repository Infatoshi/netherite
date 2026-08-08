/* Render the four deterministic GuiEditStructure modes for the Java oracle
 * fixture in capture_structure_gui.py. The background color is deliberately
 * arbitrary: compare_structure_gui.py owns only opaque form pixels. */
#include "game/runtime.h"
#include "game/screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 854, HEIGHT = 480 };

static int write_ppm(const char *path, const CrFramebuffer *fb) {
    FILE *stream = fopen(path, "wb");
    if (!stream) return 0;
    fprintf(stream, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (int index = 0; index < fb->w * fb->h; ++index) {
        unsigned char rgb[3] = {
            fb->color[index].r,
            fb->color[index].g,
            fb->color[index].b,
        };
        if (fwrite(rgb, 1, sizeof rgb, stream) != sizeof rgb) {
            fclose(stream);
            return 0;
        }
    }
    return fclose(stream) == 0;
}

static void set_form(GmRuntime *runtime, int mode) {
    GmRuntimeStructureGui *gui;
    memset(runtime, 0, sizeof *runtime);
    runtime->container = 12;
    gui = &runtime->structure_gui;
    gui->active = 1;
    gui->focus = mode == GM_STRUCTURE_MODE_DATA
        ? GM_STRUCTURE_GUI_METADATA : GM_STRUCTURE_GUI_NAME;
    gui->value.active = 1;
    gui->value.mode = mode;
    gui->value.mirror = GM_STRUCTURE_MIRROR_LEFT_RIGHT;
    gui->value.rotation = GM_STRUCTURE_ROTATION_CW90;
    gui->value.ignore_entities = 1;
    gui->value.show_air = 1;
    gui->value.show_bounding_box = 1;
    snprintf(gui->value.name, sizeof gui->value.name, "screen_fixture");
    snprintf(gui->value.metadata, sizeof gui->value.metadata, "custom_data");
    snprintf(gui->pos_x, sizeof gui->pos_x, "1");
    snprintf(gui->pos_y, sizeof gui->pos_y, "2");
    snprintf(gui->pos_z, sizeof gui->pos_z, "3");
    snprintf(gui->size_x, sizeof gui->size_x, "4");
    snprintf(gui->size_y, sizeof gui->size_y, "5");
    snprintf(gui->size_z, sizeof gui->size_z, "6");
    snprintf(gui->integrity, sizeof gui->integrity, "0.75");
    snprintf(gui->seed, sizeof gui->seed, "12345");
}

int main(int argc, char **argv) {
    static const char *names[4] = {"save", "load", "corner", "data"};
    const char *out = argc == 2 ? argv[1] : "../verify/ui_hud/c_frames";
    CrRgba *pixels = (CrRgba *)malloc(
        (size_t)WIDTH * HEIGHT * sizeof *pixels);
    if (!pixels) return 1;
    CrFramebuffer fb = {WIDTH, HEIGHT, pixels, NULL};
    for (int mode = 0; mode < 4; ++mode) {
        GmRuntime runtime;
        char path[512];
        set_form(&runtime, mode);
        for (int index = 0; index < WIDTH * HEIGHT; ++index)
            pixels[index] = (CrRgba){32, 64, 96, 255};
        gm_screen_draw(&fb, &runtime, 0, 0);
        snprintf(path, sizeof path, "%s/gui_structure_%s.ppm",
                 out, names[mode]);
        if (!write_ppm(path, &fb)) {
            fprintf(stderr, "failed to write %s\n", path);
            free(pixels);
            return 1;
        }
    }
    free(pixels);
    puts("structure GUI candidate: rendered 4 modes");
    return 0;
}
