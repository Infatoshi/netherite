/* A PNG reader for the build and the gates: 8-bit, non-interlaced RGB, RGBA
 * or palette (with its tRNS alpha), every filter type. That covers the oracle's goldens and the client jar's GUI
 * sheets; anything else is refused rather than guessed. */
#include "pngread.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

unsigned char *png_read_rgba(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *file = n > 0 ? malloc((size_t)n) : NULL;
    if (!file || fread(file, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(file); return NULL; }
    fclose(f);

    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (n < 8 || memcmp(file, sig, 8) != 0) { free(file); return NULL; }
    unsigned char *idat = NULL;
    size_t nidat = 0;
    int depth = 0, ctype = -1, interlace = 0;
    unsigned char pal[256][4];
    memset(pal, 0, sizeof pal);
    for (int i = 0; i < 256; ++i) pal[i][3] = 255;
    *w = *h = 0;
    for (long p = 8; p + 12 <= n;)
    {
        uint32_t len = be32(file + p);
        const unsigned char *type = file + p + 4, *data = file + p + 8;
        if (p + 12 + (long)len > n) break;
        if (!memcmp(type, "IHDR", 4) && len >= 13)
        {
            *w = (int)be32(data); *h = (int)be32(data + 4);
            depth = data[8]; ctype = data[9]; interlace = data[12];
        }
        else if (!memcmp(type, "PLTE", 4))
        {
            for (uint32_t i = 0; i < len / 3 && i < 256; ++i) memcpy(pal[i], data + i * 3, 3);
        }
        else if (!memcmp(type, "tRNS", 4))
        {
            for (uint32_t i = 0; i < len && i < 256; ++i) pal[i][3] = data[i];
        }
        else if (!memcmp(type, "IDAT", 4))
        {
            unsigned char *grown = realloc(idat, nidat + len);
            if (!grown) { free(idat); free(file); return NULL; }
            idat = grown;
            memcpy(idat + nidat, data, len);
            nidat += len;
        }
        else if (!memcmp(type, "IEND", 4)) break;
        p += 12 + (long)len;
    }
    free(file);
    int bpp = ctype == 2 ? 3 : ctype == 6 ? 4 : ctype == 3 ? 1 : 0;
    if (depth != 8 || !bpp || interlace || *w <= 0 || *h <= 0 || !idat) { free(idat); return NULL; }

    size_t stride = (size_t)*w * bpp, raw_n = (stride + 1) * (size_t)*h;
    unsigned char *raw = malloc(raw_n);
    uLongf got = (uLongf)raw_n;
    if (!raw || uncompress(raw, &got, idat, (uLong)nidat) != Z_OK || got != raw_n)
    { free(idat); free(raw); return NULL; }
    free(idat);

    unsigned char *px = malloc(stride * (size_t)*h), *rgba = malloc((size_t)*w * *h * 4);
    if (!px || !rgba) { free(raw); free(px); free(rgba); return NULL; }
    for (int y = 0; y < *h; ++y)
    {
        const unsigned char *in = raw + (stride + 1) * (size_t)y + 1;
        unsigned char *out = px + stride * (size_t)y;
        const unsigned char *up = y ? out - stride : NULL;
        int filter = in[-1];
        for (size_t i = 0; i < stride; ++i)
        {
            int a = i >= (size_t)bpp ? out[i - bpp] : 0, b = up ? up[i] : 0;
            int c = up && i >= (size_t)bpp ? up[i - bpp] : 0, v = in[i];
            switch (filter)
            {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: free(raw); free(px); free(rgba); return NULL;
            }
            out[i] = (unsigned char)v;
        }
    }
    free(raw);
    for (size_t i = 0; i < (size_t)*w * *h; ++i)
    {
        if (bpp == 1)
        {
            memcpy(rgba + i * 4, pal[px[i]], 4);
            continue;
        }
        memcpy(rgba + i * 4, px + i * bpp, 3);
        rgba[i * 4 + 3] = bpp == 4 ? px[i * 4 + 3] : 255;
    }
    free(px);
    return rgba;
}
