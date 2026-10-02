#include "region.h"
#include "envstack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* A whole file, malloc'd; NULL when it cannot be read. */
static unsigned char *read_whole(const char *path, size_t *out_n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);

    unsigned char *buf = malloc((size_t)len ? (size_t)len : 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *out_n = (size_t)len;
    return buf;
}

/* InflaterInputStream over a zlib stream that has no size hint. */
static unsigned char *inflate_zlib(const unsigned char *in, size_t n, size_t *out_n)
{
    size_t cap = n < 65536 ? 65536 : n * 4;
    for (;;)
    {
        unsigned char *out = malloc(cap);
        if (!out) return NULL;
        z_stream zs;
        memset(&zs, 0, sizeof zs);
        if (inflateInit(&zs) != Z_OK) { free(out); return NULL; }
        zs.next_in = (Bytef *)in;
        zs.avail_in = (uInt)n;
        zs.next_out = out;
        zs.avail_out = (uInt)cap;
        int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);

        if (rc == Z_OK || rc == Z_BUF_ERROR)
        {
            free(out);
            cap *= 2;
            continue;
        }
        if (rc != Z_STREAM_END) { free(out); return NULL; }

        *out_n = cap - zs.avail_out;
        return out;
    }
}

/* Binary NBT: CompressedStreamTools.read's payload after the stream's own
 * root tag. Only the tag kinds a chunk's Level compound carries are read; a
 * kind outside the set (a mod tag, a future save) fails the load. */
static const unsigned char *nbt_name(const unsigned char *b, size_t n, size_t *i, char *name, size_t name_cap)
{
    if (*i + 2 > n) return NULL;
    unsigned len = (unsigned)(b[*i] << 8 | b[*i + 1]);
    *i += 2;
    if (*i + len > n || len >= name_cap) return NULL;
    memcpy(name, b + *i, len);
    name[len] = 0;
    *i += len;
    return b;
}

static nbt *nbt_bin_value(const unsigned char *b, size_t n, size_t *i, unsigned char type, int depth);

static nbt *nbt_bin_compound(const unsigned char *b, size_t n, size_t *i, int depth)
{
    nbt *comp = nbt_new_compound();
    if (!comp) return NULL;

    for (;;)
    {
        if (*i >= n) { nbt_free(comp); return NULL; }
        unsigned char t = b[(*i)++];
        if (t == 0) return comp;
        char name[128];
        if (!nbt_name(b, n, i, name, sizeof name)) { nbt_free(comp); return NULL; }
        nbt *v = nbt_bin_value(b, n, i, t, depth);
        if (!v) { nbt_free(comp); return NULL; }
        nbt_put(comp, name, v);
    }
}

static nbt *nbt_bin_value(const unsigned char *b, size_t n, size_t *i, unsigned char type, int depth)
{
    if (depth > 64) return NULL;
    long long lv;
    int iv;
    float fv;
    double dv;

    switch (type)
    {
    case 1:
        if (*i + 1 > n) return NULL;
        return nbt_new_byte((signed char)b[(*i)++]);
    case 2:
    {
        if (*i + 2 > n) return NULL;
        short v = (short)((unsigned)b[*i] << 8 | (unsigned)b[*i + 1]);
        *i += 2;
        return nbt_new_short(v);
    }
    case 3:
        if (*i + 4 > n) return NULL;
        iv = (int)((unsigned)b[*i] << 24 | (unsigned)b[*i + 1] << 16 | (unsigned)b[*i + 2] << 8 | (unsigned)b[*i + 3]);
        *i += 4;
        return nbt_new_int(iv);
    case 4:
        if (*i + 8 > n) return NULL;
        lv = 0;
        for (int k = 0; k < 8; ++k) lv = (lv << 8) | b[*i + k];
        *i += 8;
        return nbt_new_long(lv);
    case 5:
    {
        if (*i + 4 > n) return NULL;
        unsigned bits = (unsigned)b[*i] << 24 | (unsigned)b[*i + 1] << 16 | (unsigned)b[*i + 2] << 8 | (unsigned)b[*i + 3];
        memcpy(&fv, &bits, 4);
        *i += 4;
        return nbt_new_float(fv);
    }
    case 6:
    {
        if (*i + 8 > n) return NULL;
        unsigned long long bits = 0;
        for (int k = 0; k < 8; ++k) bits = (bits << 8) | b[*i + k];
        memcpy(&dv, &bits, 8);
        *i += 8;
        return nbt_new_double(dv);
    }
    case 7:
    {
        if (*i + 4 > n) return NULL;
        int cnt = (int)((unsigned)b[*i] << 24 | (unsigned)b[*i + 1] << 16 | (unsigned)b[*i + 2] << 8 | (unsigned)b[*i + 3]);
        *i += 4;
        if (cnt < 0 || *i + (size_t)cnt > n) return NULL;
        nbt *a = nbt_new_byte_array((const signed char *)(b + *i), cnt);
        *i += (size_t)cnt;
        return a;
    }
    case 8:
    {
        char *name ENV_LOCAL = envstack_take(65536);
        if (!nbt_name(b, n, i, name, 65536)) return NULL;
        return nbt_new_string(name);
    }
    case 9:
    {
        if (*i + 5 > n) return NULL;
        unsigned char it = b[(*i)++];
        int cnt = (int)((unsigned)b[*i] << 24 | (unsigned)b[*i + 1] << 16 | (unsigned)b[*i + 2] << 8 | (unsigned)b[*i + 3]);
        *i += 4;
        if (cnt < 0) return NULL;
        nbt *list = nbt_new_list();
        if (!list) return NULL;
        for (int k = 0; k < cnt; ++k)
        {
            nbt *v = nbt_bin_value(b, n, i, it, depth + 1);
            if (!v) { nbt_free(list); return NULL; }
            nbt_list_add(list, v);
        }
        return list;
    }
    case 10:
        return nbt_bin_compound(b, n, i, depth + 1);
    case 11:
    {
        if (*i + 4 > n) return NULL;
        int cnt = (int)((unsigned)b[*i] << 24 | (unsigned)b[*i + 1] << 16 | (unsigned)b[*i + 2] << 8 | (unsigned)b[*i + 3]);
        *i += 4;
        if (cnt < 0 || *i + (size_t)cnt * 4 > n) return NULL;
        int *vals = malloc(cnt > 0 ? (size_t)cnt * sizeof *vals : 4);
        if (!vals) return NULL;
        for (int k = 0; k < cnt; ++k)
        {
            vals[k] = (int)((unsigned)b[*i] << 24 | (unsigned)b[*i + 1] << 16 | (unsigned)b[*i + 2] << 8 | (unsigned)b[*i + 3]);
            *i += 4;
        }
        nbt *a = nbt_new_int_array(vals, cnt);
        free(vals);
        return a;
    }
    case 12:
    {
        if (*i + 4 > n) return NULL;
        int cnt = (int)((unsigned)b[*i] << 24 | (unsigned)b[*i + 1] << 16 | (unsigned)b[*i + 2] << 8 | (unsigned)b[*i + 3]);
        *i += 4;
        if (cnt < 0 || *i + (size_t)cnt * 8 > n) return NULL;
        nbt *a = nbt_new_list();
        if (!a) return NULL;
        for (int k = 0; k < cnt; ++k)
        {
            long long v = 0;
            for (int j = 0; j < 8; ++j) v = (v << 8) | b[*i + j];
            *i += 8;
            nbt_list_add(a, nbt_new_long(v));
        }
        return a;
    }
    default:
        return NULL;
    }
}

nbt *region_load_chunk(const char *save_dir, int cx, int cz)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/region/r.%d.%d.mca", save_dir, cx >> 5, cz >> 5);
    /* the chunk's header entry and then its record alone, not the whole
     * file (a region of 1,024 chunks is megabytes, and each load read it) */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    long flen = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    size_t n = flen > 0 ? (size_t)flen : 0;
    int lx = cx & 31, lz = cz & 31;
    unsigned char h[5];
    if (n < 8192 || fseek(f, (long)(lz * 32 + lx) * 4, SEEK_SET) != 0 || fread(h, 1, 4, f) != 4) { fclose(f); return NULL; }
    unsigned off = (unsigned)h[0] << 16 | (unsigned)h[1] << 8 | (unsigned)h[2];
    unsigned cnt = h[3];

    if (off == 0) { fclose(f); return NULL; }

    size_t sec = (size_t)off * 4096;
    if (sec + 5 > n || fseek(f, (long)sec, SEEK_SET) != 0 || fread(h, 1, 5, f) != 5) { fclose(f); return NULL; }
    unsigned len = (unsigned)h[0] << 24 | (unsigned)h[1] << 16 | (unsigned)h[2] << 8 | (unsigned)h[3];
    unsigned char comp = h[4];

    if (len < 2 || sec + 4 + (size_t)len > n || (cnt && len > cnt * 4096u)) { fclose(f); return NULL; }
    unsigned char *file = malloc(len - 1);
    if (!file || fread(file, 1, len - 1, f) != len - 1) { free(file); fclose(f); return NULL; }
    fclose(f);
    sec = 0;

    unsigned char *raw = NULL;
    size_t raw_n = 0;

    if (comp == 2)
    {
        raw = inflate_zlib(file, len - 1, &raw_n);
    }
    else if (comp == 1)
    {
        /* gzip: a 10-byte header, the deflate body, an 8-byte trailer */
        z_stream zs;
        memset(&zs, 0, sizeof zs);
        if (inflateInit2(&zs, 15 + 16) != Z_OK) { free(file); return NULL; }
        size_t cap = (size_t)(len - 1) * 8 + 65536;
        for (;;)
        {
            raw = malloc(cap);
            if (!raw) { inflateEnd(&zs); free(file); return NULL; }
            zs.next_in = (Bytef *)(file);
            zs.avail_in = (uInt)(len - 1);
            zs.next_out = raw;
            zs.avail_out = (uInt)cap;
            int rc = inflate(&zs, Z_FINISH);
            inflateEnd(&zs);

            if (rc == Z_OK || rc == Z_BUF_ERROR)
            {
                free(raw);
                cap *= 2;
                memset(&zs, 0, sizeof zs);
                if (inflateInit2(&zs, 15 + 16) != Z_OK) { free(file); return NULL; }
                continue;
            }
            if (rc != Z_STREAM_END) { free(raw); free(file); return NULL; }
            raw_n = cap - zs.avail_out;
            break;
        }
    }
    else
    {
        free(file);
        return NULL;
    }

    free(file);

    /* CompressedStreamTools.read: the root compound tag with an empty name.
     * The payload begins at the first member's tag byte, which
     * nbt_bin_compound itself reads. */
    if (raw_n < 3 || raw[0] != 10) { free(raw); return NULL; }
    size_t i = 3;

    nbt *root = nbt_bin_compound(raw, raw_n, &i, 1);
    free(raw);
    if (!root) { return NULL; }

    nbt *level = nbt_take(root, "Level");
    nbt_free(root);
    if (!level) return NULL;

    /* nbt trees are opaque: each field as the canonical text form's round
     * trip gives it back (nbt_copy_canonical, the text itself only where that
     * copy declines), the round trip the snapshot reader relies on; the
     * sections, byte arrays and bytes under keys only ever looked up, move
     * over as they are */
    nbt *copy = nbt_new_compound();
    if (!copy) { nbt_free(level); return NULL; }
    while (nbt_field_count(level) > 0)
    {
        const char *k = nbt_field_key(level, 0);
        nbt *v = nbt_take(level, k);
        if (!strcmp(k, "Sections"))
        {
            nbt_put(copy, k, v);
            continue;
        }
        nbt *parsed = nbt_copy_canonical(v);
        if (!parsed)
        {
            char *text = nbt_render(v);
            if (text) parsed = nbt_parse(text);
            free(text);
        }
        nbt_free(v);
        if (!parsed) { nbt_free(copy); nbt_free(level); return NULL; }
        nbt_put(copy, k, parsed);
    }
    nbt_free(level);
    return copy;
}

int region_level_terrain_populated(const nbt *level)
{
    const nbt *v = nbt_get(level, "TerrainPopulated");
    return v && nbt_int_value(v) != 0;
}

int region_enumerate(const char *save_dir, int **out, int *out_n)
{
    char path[1200];
    int cap = 0;
    *out = NULL;
    *out_n = 0;
    for (int rx = -32; rx < 32; ++rx)
        for (int rz = -32; rz < 32; ++rz)
        {
            snprintf(path, sizeof path, "%s/region/r.%d.%d.mca", save_dir, rx, rz);
            size_t n = 0;
            unsigned char *file = read_whole(path, &n);
            if (!file || n < 8192) { free(file); continue; }
            for (int slot = 0; slot < 1024; ++slot)
            {
                size_t e = (size_t)slot * 4;
                unsigned off = (unsigned)file[e] << 16 | (unsigned)file[e + 1] << 8 | (unsigned)file[e + 2];
                if (!off) continue;
                if (*out_n == cap)
                {
                    cap = cap ? cap * 2 : 256;
                    int *next = realloc(*out, (size_t)cap * 2 * sizeof *next);
                    if (!next) { free(file); free(*out); *out = NULL; *out_n = 0; return -1; }
                    *out = next;
                }
                (*out)[(*out_n) * 2] = rx * 32 + (slot & 31);
                (*out)[(*out_n) * 2 + 1] = rz * 32 + (slot >> 5);
                ++*out_n;
            }
            free(file);
        }
    return 0;
}
