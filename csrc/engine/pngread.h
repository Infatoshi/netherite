#ifndef NETHERITE_PNGREAD_H
#define NETHERITE_PNGREAD_H

/* PATH as packed RGBA (alpha 255 for an RGB file), or NULL for a missing file
 * or anything but an 8-bit, non-interlaced RGB or RGBA PNG. Free the result. */
unsigned char *png_read_rgba(const char *path, int *w, int *h);

#endif
