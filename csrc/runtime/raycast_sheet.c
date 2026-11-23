/* A contact sheet of frames (lane/raycast): PNGs in a grid, each scaled up
 * by an integer factor, a 4-pixel dark gap between them, row by row. "-" in
 * the list leaves a cell empty. For looking at the ray cast beside the
 * renderer's frame of the same tick (pool_gate --raycast-dump, play --shots).
 *
 *   raycast_sheet OUT.png COLS SCALE A.png B.png ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/pngread.h"
#include "../engine/raster.h"

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        fprintf(stderr, "usage: raycast_sheet OUT.png COLS SCALE A.png B.png ...\n");
        return 2;
    }
    int cols = atoi(argv[2]), scale = atoi(argv[3]), n = argc - 4;
    if (cols < 1 || scale < 1) return 2;
    int cw = 0, ch = 0;
    for (int i = 0; i < n; ++i)
    {
        if (!strcmp(argv[4 + i], "-")) continue;
        int w, h;
        unsigned char *p = png_read_rgba(argv[4 + i], &w, &h);
        if (p == NULL) { fprintf(stderr, "raycast_sheet: cannot read %s\n", argv[4 + i]); return 1; }
        free(p);
        if (w * scale > cw) cw = w * scale;
        if (h * scale > ch) ch = h * scale;
    }
    int rows = (n + cols - 1) / cols, gap = 4;
    int W = cols * cw + (cols + 1) * gap, H = rows * ch + (rows + 1) * gap;
    unsigned char *out = malloc((size_t)W * (size_t)H * 3);
    if (out == NULL) return 1;
    memset(out, 24, (size_t)W * (size_t)H * 3);
    for (int i = 0; i < n; ++i)
    {
        if (!strcmp(argv[4 + i], "-")) continue;
        int w, h;
        unsigned char *p = png_read_rgba(argv[4 + i], &w, &h);
        int x0 = gap + (i % cols) * (cw + gap), y0 = gap + (i / cols) * (ch + gap);
        for (int y = 0; y < h * scale; ++y)
            for (int x = 0; x < w * scale; ++x)
                memcpy(out + ((size_t)(y0 + y) * (size_t)W + (size_t)(x0 + x)) * 3,
                       p + ((size_t)(y / scale) * (size_t)w + (size_t)(x / scale)) * 4, 3);
        free(p);
    }
    int rc = raster_png(argv[1], W, H, out);
    free(out);
    return rc != 0;
}
