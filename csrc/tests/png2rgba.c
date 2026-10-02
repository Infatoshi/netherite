/* png2rgba [--rgb] IN.png OUT: the raw RGBA (or RGB) bytes of a PNG, for the
 * build's textures (the client jar's: the GUI sheets, out/assets).
 * png2rgba --cut SRC.rgba SRCW X Y W H OUT: the WxH block at X,Y of a raw
 * RGBA image SRCW pixels wide (a sprite of a recorded atlas). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/pngread.h"

static int put(const char *path, const unsigned char *b, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(b, 1, n, f) == n;
    if (f) ok &= fclose(f) == 0;
    if (!ok) fprintf(stderr, "png2rgba: cannot write %s\n", path);
    return ok ? 0 : 1;
}

static int cut(char **a)
{
    int sw = atoi(a[1]), x = atoi(a[2]), y = atoi(a[3]), w = atoi(a[4]), h = atoi(a[5]);
    FILE *f = fopen(a[0], "rb");
    if (!f) { fprintf(stderr, "png2rgba: cannot read %s\n", a[0]); return 1; }
    unsigned char *px = malloc((size_t)w * h * 4);
    int ok = px != NULL;
    for (int r = 0; ok && r < h; ++r)
        ok = fseek(f, (((long)(y + r) * sw) + x) * 4, SEEK_SET) == 0 &&
             fread(px + (size_t)r * w * 4, 4, (size_t)w, f) == (size_t)w;
    fclose(f);
    int rc = ok ? put(a[6], px, (size_t)w * h * 4) : (fprintf(stderr, "png2rgba: %s: short read\n", a[0]), 1);
    free(px);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc == 9 && !strcmp(argv[1], "--cut")) return cut(argv + 2);
    int rgb = argc == 4 && !strcmp(argv[1], "--rgb");
    if (argc != 3 && !rgb) {
        fprintf(stderr, "usage: png2rgba [--rgb] IN.png OUT | png2rgba --cut SRC.rgba SRCW X Y W H OUT\n");
        return 2;
    }
    const char *in = argv[argc - 2], *out = argv[argc - 1];
    int w, h;
    unsigned char *px = png_read_rgba(in, &w, &h);
    if (!px) { fprintf(stderr, "png2rgba: cannot read %s\n", in); return 1; }
    size_t n = (size_t)w * h;
    if (rgb)
        for (size_t i = 0; i < n; ++i) memmove(px + i * 3, px + i * 4, 3);
    int rc = put(out, px, n * (rgb ? 3 : 4));
    free(px);
    return rc;
}
