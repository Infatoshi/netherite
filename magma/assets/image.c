#include "image.h"
#include "jar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static unsigned int be32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p - a;
    int pb = p - b;
    int pc = p - c;
    if (pa < 0)
        pa = -pa;
    if (pb < 0)
        pb = -pb;
    if (pc < 0)
        pc = -pc;
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

static int inflate_zlib(const unsigned char *src, size_t src_len,
                        unsigned char **out, size_t *out_len, size_t expect) {
    z_stream strm;
    size_t cap;
    unsigned char *buf;
    int rc;

    memset(&strm, 0, sizeof(strm));
    if (inflateInit(&strm) != Z_OK)
        return -1;
    cap = expect ? expect : (src_len * 4 + 64);
    buf = (unsigned char *)malloc(cap);
    if (!buf) {
        inflateEnd(&strm);
        return -1;
    }
    strm.next_in = (Bytef *)src;
    strm.avail_in = (uInt)src_len;
    strm.next_out = buf;
    strm.avail_out = (uInt)cap;
    for (;;) {
        rc = inflate(&strm, Z_NO_FLUSH);
        if (rc == Z_STREAM_END)
            break;
        if (rc != Z_OK) {
            free(buf);
            inflateEnd(&strm);
            return -1;
        }
        if (strm.avail_out == 0) {
            size_t used = (size_t)(strm.next_out - buf);
            size_t ncap = cap * 2;
            unsigned char *nbuf = (unsigned char *)realloc(buf, ncap);
            if (!nbuf) {
                free(buf);
                inflateEnd(&strm);
                return -1;
            }
            buf = nbuf;
            cap = ncap;
            strm.next_out = buf + used;
            strm.avail_out = (uInt)(cap - used);
        }
    }
    *out_len = (size_t)strm.total_out;
    if (expect && *out_len != expect) {
        free(buf);
        inflateEnd(&strm);
        return -1;
    }
    inflateEnd(&strm);
    *out = buf;
    return 0;
}

int asset_image_new(AssetImage *image, int w, int h) {
    size_t n;
    if (!image || w <= 0 || h <= 0)
        return -1;
    n = (size_t)w * (size_t)h * 4u;
    image->rgba = (unsigned char *)calloc(1, n);
    if (!image->rgba)
        return -1;
    image->w = w;
    image->h = h;
    return 0;
}

void asset_image_free(AssetImage *image) {
    if (!image)
        return;
    free(image->rgba);
    image->rgba = NULL;
    image->w = image->h = 0;
}

int asset_image_crop(const AssetImage *src, int x, int y, int w, int h, AssetImage *dst) {
    int row;
    if (!src || !dst || !src->rgba || w <= 0 || h <= 0)
        return -1;
    if (x < 0 || y < 0 || x + w > src->w || y + h > src->h)
        return -1;
    if (asset_image_new(dst, w, h) != 0)
        return -1;
    for (row = 0; row < h; row++) {
        memcpy(dst->rgba + (size_t)row * (size_t)w * 4u,
               src->rgba + ((size_t)(y + row) * (size_t)src->w + (size_t)x) * 4u,
               (size_t)w * 4u);
    }
    return 0;
}

int asset_image_resize_nearest(const AssetImage *src, int w, int h, AssetImage *dst) {
    int y, x;
    if (!src || !dst || !src->rgba || w <= 0 || h <= 0 || src->w <= 0 || src->h <= 0)
        return -1;
    if (asset_image_new(dst, w, h) != 0)
        return -1;
    /* Center-sample nearest (Pillow Image.NEAREST): sx = ((2x+1)*sw)/(2*dw). */
    for (y = 0; y < h; y++) {
        int sy = ((2 * y + 1) * src->h) / (2 * h);
        if (sy >= src->h)
            sy = src->h - 1;
        for (x = 0; x < w; x++) {
            int sx = ((2 * x + 1) * src->w) / (2 * w);
            unsigned char *d;
            const unsigned char *s;
            if (sx >= src->w)
                sx = src->w - 1;
            d = dst->rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            s = src->rgba + ((size_t)sy * (size_t)src->w + (size_t)sx) * 4u;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }
    return 0;
}

int asset_image_paste(AssetImage *dst, const AssetImage *src, int x, int y) {
    int row, col;
    if (!dst || !src || !dst->rgba || !src->rgba)
        return -1;
    if (x < 0 || y < 0 || x + src->w > dst->w || y + src->h > dst->h)
        return -1;
    for (row = 0; row < src->h; row++) {
        for (col = 0; col < src->w; col++) {
            unsigned char *d =
                dst->rgba +
                ((size_t)(y + row) * (size_t)dst->w + (size_t)(x + col)) * 4u;
            const unsigned char *s =
                src->rgba + ((size_t)row * (size_t)src->w + (size_t)col) * 4u;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }
    return 0;
}

int asset_image_load_png(const unsigned char *png, size_t png_size, AssetImage *image) {
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    size_t off;
    unsigned int width = 0, height = 0;
    unsigned char bit_depth = 0, color_type = 0, interlace = 0;
    unsigned char *idat = NULL;
    size_t idat_len = 0, idat_cap = 0;
    unsigned char plte[256 * 3];
    unsigned char trns[256];
    int has_plte = 0, has_trns = 0;
    int trns_gray = -1, trns_r = -1, trns_g = -1, trns_b = -1;
    int trns_n = 0;
    unsigned char *raw = NULL;
    size_t raw_len = 0;
    int bpp, stride, y, x;
    unsigned char *rgba = NULL;
    size_t i;

    if (!png || !image || png_size < 8)
        return -1;
    memset(image, 0, sizeof(*image));
    if (memcmp(png, sig, 8) != 0)
        return -1;
    memset(trns, 255, sizeof(trns));
    off = 8;
    while (off + 12 <= png_size) {
        unsigned int len = be32(png + off);
        const unsigned char *type = png + off + 4;
        const unsigned char *data = png + off + 8;
        unsigned int file_crc, calc_crc;
        unsigned long c;

        if (off + 12u + len > png_size) {
            free(idat);
            return -1;
        }
        file_crc = be32(png + off + 8 + len);
        c = crc32(0L, Z_NULL, 0);
        c = crc32(c, type, 4);
        if (len)
            c = crc32(c, data, len);
        calc_crc = (unsigned int)c;
        if (calc_crc != file_crc) {
            free(idat);
            return -1;
        }

        if (memcmp(type, "IHDR", 4) == 0) {
            if (len != 13 || width != 0) {
                free(idat);
                return -1;
            }
            width = be32(data);
            height = be32(data + 4);
            bit_depth = data[8];
            color_type = data[9];
            if (data[10] != 0 || data[11] != 0) {
                free(idat);
                return -1;
            }
            interlace = data[12];
            if (interlace != 0 || bit_depth != 8) {
                free(idat);
                return -1;
            }
            if (color_type != 2 && color_type != 3 && color_type != 6) {
                free(idat);
                return -1;
            }
            if (width == 0 || height == 0 || width > 4096 || height > 16384) {
                free(idat);
                return -1;
            }
        } else if (memcmp(type, "PLTE", 4) == 0) {
            if (len == 0 || len % 3 != 0 || len > 768) {
                free(idat);
                return -1;
            }
            memcpy(plte, data, len);
            has_plte = (int)(len / 3);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (color_type == 3) {
                if (len > 256) {
                    free(idat);
                    return -1;
                }
                memcpy(trns, data, len);
                trns_n = (int)len;
                has_trns = 1;
            } else if (color_type == 0) {
                if (len != 2) {
                    free(idat);
                    return -1;
                }
                trns_gray = (data[0] << 8) | data[1];
                has_trns = 1;
            } else if (color_type == 2) {
                if (len != 6) {
                    free(idat);
                    return -1;
                }
                trns_r = (data[0] << 8) | data[1];
                trns_g = (data[2] << 8) | data[3];
                trns_b = (data[4] << 8) | data[5];
                has_trns = 1;
            } else {
                free(idat);
                return -1;
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (idat_len + len < idat_len) {
                free(idat);
                return -1;
            }
            if (idat_len + len > idat_cap) {
                size_t ncap = idat_cap ? idat_cap * 2 : 4096;
                unsigned char *nbuf;
                while (ncap < idat_len + len)
                    ncap *= 2;
                nbuf = (unsigned char *)realloc(idat, ncap);
                if (!nbuf) {
                    free(idat);
                    return -1;
                }
                idat = nbuf;
                idat_cap = ncap;
            }
            if (len)
                memcpy(idat + idat_len, data, len);
            idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            off += 12u + len;
            break;
        } else {
            /* Ancillary: require safe-to-copy or known; reject critical unknown. */
            if (!(type[0] & 0x20)) {
                free(idat);
                return -1;
            }
        }
        off += 12u + len;
    }

    if (width == 0 || idat_len == 0) {
        free(idat);
        return -1;
    }
    if (color_type == 3 && !has_plte) {
        free(idat);
        return -1;
    }

    if (color_type == 2)
        bpp = 3;
    else if (color_type == 6)
        bpp = 4;
    else
        bpp = 1;
    stride = 1 + (int)width * bpp;
    {
        size_t expect = (size_t)stride * (size_t)height;
        if (inflate_zlib(idat, idat_len, &raw, &raw_len, expect) != 0) {
            free(idat);
            return -1;
        }
    }
    free(idat);

    /* Unfilter in place. */
    for (y = 0; y < (int)height; y++) {
        unsigned char *row = raw + (size_t)y * (size_t)stride;
        unsigned char filter = row[0];
        unsigned char *prev = y ? raw + (size_t)(y - 1) * (size_t)stride : NULL;
        unsigned char *cur = row + 1;
        int nbytes = (int)width * bpp;

        if (filter > 4) {
            free(raw);
            return -1;
        }
        for (x = 0; x < nbytes; x++) {
            unsigned char a = (x >= bpp) ? cur[x - bpp] : 0;
            unsigned char b = prev ? prev[1 + x] : 0;
            unsigned char c = (prev && x >= bpp) ? prev[1 + x - bpp] : 0;
            unsigned char v = cur[x];
            switch (filter) {
            case 0:
                break;
            case 1:
                v = (unsigned char)(v + a);
                break;
            case 2:
                v = (unsigned char)(v + b);
                break;
            case 3:
                v = (unsigned char)(v + ((a + b) / 2));
                break;
            case 4:
                v = (unsigned char)(v + paeth(a, b, c));
                break;
            }
            cur[x] = v;
        }
    }

    rgba = (unsigned char *)malloc((size_t)width * (size_t)height * 4u);
    if (!rgba) {
        free(raw);
        return -1;
    }
    for (y = 0; y < (int)height; y++) {
        const unsigned char *src = raw + (size_t)y * (size_t)stride + 1u;
        unsigned char *dst = rgba + (size_t)y * (size_t)width * 4u;
        for (x = 0; x < (int)width; x++) {
            unsigned char r, g, b, a;
            if (color_type == 2) {
                r = src[x * 3 + 0];
                g = src[x * 3 + 1];
                b = src[x * 3 + 2];
                a = 255;
                if (has_trns && trns_r == r && trns_g == g && trns_b == b)
                    a = 0;
            } else if (color_type == 6) {
                r = src[x * 4 + 0];
                g = src[x * 4 + 1];
                b = src[x * 4 + 2];
                a = src[x * 4 + 3];
            } else {
                unsigned char idx = src[x];
                if (idx >= has_plte) {
                    free(raw);
                    free(rgba);
                    return -1;
                }
                r = plte[idx * 3 + 0];
                g = plte[idx * 3 + 1];
                b = plte[idx * 3 + 2];
                a = 255;
                if (has_trns && idx < trns_n)
                    a = trns[idx];
            }
            dst[x * 4 + 0] = r;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = b;
            dst[x * 4 + 3] = a;
            (void)i;
            (void)trns_gray;
        }
    }
    free(raw);
    image->w = (int)width;
    image->h = (int)height;
    image->rgba = rgba;
    return 0;
}

int asset_image_load(AssetJar *jar, const char *member, AssetImage *image) {
    unsigned char *data = NULL;
    size_t size = 0;
    int rc;

    if (!jar || !member || !image)
        return -1;
    if (asset_jar_read(jar, member, &data, &size) != 0)
        return -1;
    rc = asset_image_load_png(data, size, image);
    free(data);
    return rc;
}
