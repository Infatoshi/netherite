#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "mca.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define CHUNK_CELLS MCA_CHUNK_CELLS
#define MAX_INFLATE (16u * 1024u * 1024u)
#define TAG_END 0
#define TAG_BYTE 1
#define TAG_SHORT 2
#define TAG_INT 3
#define TAG_LONG 4
#define TAG_FLOAT 5
#define TAG_DOUBLE 6
#define TAG_BYTE_ARRAY 7
#define TAG_STRING 8
#define TAG_LIST 9
#define TAG_COMPOUND 10
#define TAG_INT_ARRAY 11
#define TAG_LONG_ARRAY 12

typedef struct {
    int t;
    int ax, ay, az;
} Pose;

typedef struct {
    const unsigned char *p;
    const unsigned char *end;
} Nbt;

typedef struct {
    int cx0, cz0, ncx, ncz;
    uint16_t **grid;
    int loaded;
    uint64_t file_bytes;
} Store;

static uint16_t pack_state(unsigned id, unsigned meta)
{
    id &= 0xfffu;
    meta &= 15u;
    if (id == 175u && (meta & 8u))
        meta = 8u | 2u;
    return (uint16_t)((id << 4) | meta);
}

static int nbt_left(const Nbt *n)
{
    return n->p < n->end;
}

static int nbt_u8(Nbt *n, unsigned *o)
{
    if (!nbt_left(n))
        return 0;
    *o = *n->p++;
    return 1;
}

static int nbt_be16(Nbt *n, unsigned *o)
{
    if (n->end - n->p < 2)
        return 0;
    *o = ((unsigned)n->p[0] << 8) | (unsigned)n->p[1];
    n->p += 2;
    return 1;
}

static int nbt_be32(Nbt *n, int32_t *o)
{
    uint32_t u;
    if (n->end - n->p < 4)
        return 0;
    u = ((uint32_t)n->p[0] << 24) | ((uint32_t)n->p[1] << 16) |
        ((uint32_t)n->p[2] << 8) | (uint32_t)n->p[3];
    n->p += 4;
    *o = (int32_t)u;
    return 1;
}

static int nbt_skip_bytes(Nbt *n, size_t k)
{
    if ((size_t)(n->end - n->p) < k)
        return 0;
    n->p += k;
    return 1;
}

static int nbt_name(Nbt *n, char *buf, size_t cap)
{
    unsigned len;
    if (!nbt_be16(n, &len))
        return 0;
    if ((size_t)(n->end - n->p) < len)
        return 0;
    if (len >= cap)
        return 0;
    memcpy(buf, n->p, len);
    buf[len] = 0;
    n->p += len;
    return 1;
}

static int nbt_skip(Nbt *n, unsigned type)
{
    int32_t len;
    unsigned et;
    int32_t i, count;
    switch (type) {
    case TAG_BYTE:
        return nbt_skip_bytes(n, 1);
    case TAG_SHORT:
        return nbt_skip_bytes(n, 2);
    case TAG_INT:
    case TAG_FLOAT:
        return nbt_skip_bytes(n, 4);
    case TAG_LONG:
    case TAG_DOUBLE:
        return nbt_skip_bytes(n, 8);
    case TAG_BYTE_ARRAY:
        if (!nbt_be32(n, &len) || len < 0)
            return 0;
        return nbt_skip_bytes(n, (size_t)len);
    case TAG_STRING:
        {
            unsigned sl;
            if (!nbt_be16(n, &sl))
                return 0;
            return nbt_skip_bytes(n, sl);
        }
    case TAG_LIST:
        if (!nbt_u8(n, &et) || !nbt_be32(n, &count) || count < 0)
            return 0;
        for (i = 0; i < count; i++) {
            if (!nbt_skip(n, et))
                return 0;
        }
        return 1;
    case TAG_COMPOUND:
        for (;;) {
            unsigned t;
            unsigned nl;
            if (!nbt_u8(n, &t))
                return 0;
            if (t == TAG_END)
                return 1;
            if (!nbt_be16(n, &nl) || !nbt_skip_bytes(n, nl))
                return 0;
            if (!nbt_skip(n, t))
                return 0;
        }
    case TAG_INT_ARRAY:
        if (!nbt_be32(n, &len) || len < 0)
            return 0;
        return nbt_skip_bytes(n, (size_t)len * 4u);
    case TAG_LONG_ARRAY:
        if (!nbt_be32(n, &len) || len < 0)
            return 0;
        return nbt_skip_bytes(n, (size_t)len * 8u);
    default:
        return 0;
    }
}

static int nbt_byte_array(Nbt *n, const unsigned char **p, int32_t *len)
{
    if (!nbt_be32(n, len) || *len < 0)
        return 0;
    if ((size_t)(n->end - n->p) < (size_t)*len)
        return 0;
    *p = n->p;
    n->p += *len;
    return 1;
}

static unsigned nibble(const unsigned char *data, int i)
{
    unsigned b = data[i >> 1];
    return (i & 1) ? (b >> 4) : (b & 0x0fu);
}

static int apply_section(uint16_t *cells, int sy, const unsigned char *blocks,
                         const unsigned char *data, const unsigned char *add)
{
    int y, z, x;
    if (sy < 0 || sy > 15)
        return 0;
    for (y = 0; y < 16; y++) {
        int wy = sy * 16 + y;
        for (z = 0; z < 16; z++) {
            for (x = 0; x < 16; x++) {
                int i = y * 256 + z * 16 + x;
                unsigned id = blocks[i];
                unsigned meta = nibble(data, i);
                size_t idx;
                if (add)
                    id |= nibble(add, i) << 8;
                idx = (size_t)x * 256u * 16u + (size_t)wy * 16u + (size_t)z;
                cells[idx] = pack_state(id, meta);
            }
        }
    }
    return 1;
}

static int parse_section_compound(Nbt *n, uint16_t *cells)
{
    int y = INT_MIN;
    const unsigned char *blocks = NULL;
    const unsigned char *data = NULL;
    const unsigned char *add = NULL;
    int32_t blocks_n = 0, data_n = 0, add_n = 0;
    for (;;) {
        unsigned type;
        char name[64];
        if (!nbt_u8(n, &type))
            return 0;
        if (type == TAG_END)
            break;
        if (!nbt_name(n, name, sizeof name))
            return 0;
        if (type == TAG_BYTE && strcmp(name, "Y") == 0) {
            unsigned b;
            if (!nbt_u8(n, &b))
                return 0;
            y = (int)(int8_t)b;
        } else if (type == TAG_BYTE_ARRAY && strcmp(name, "Blocks") == 0) {
            if (!nbt_byte_array(n, &blocks, &blocks_n))
                return 0;
        } else if (type == TAG_BYTE_ARRAY && strcmp(name, "Data") == 0) {
            if (!nbt_byte_array(n, &data, &data_n))
                return 0;
        } else if (type == TAG_BYTE_ARRAY && strcmp(name, "Add") == 0) {
            if (!nbt_byte_array(n, &add, &add_n))
                return 0;
        } else if (!nbt_skip(n, type)) {
            return 0;
        }
    }
    if (y == INT_MIN)
        return 0;
    if (y < 0 || y > 15)
        return 1;
    if (!blocks || !data || blocks_n != 4096 || data_n != 2048)
        return 0;
    if (add && add_n != 2048)
        return 0;
    return apply_section(cells, y, blocks, data, add);
}

static int parse_sections_list(Nbt *n, uint16_t *cells)
{
    unsigned et;
    int32_t count, i;
    if (!nbt_u8(n, &et) || !nbt_be32(n, &count) || count < 0)
        return 0;
    if (count == 0)
        return 1;
    if (et != TAG_COMPOUND)
        return 0;
    for (i = 0; i < count; i++) {
        if (!parse_section_compound(n, cells))
            return 0;
    }
    return 1;
}

static int parse_level_compound(Nbt *n, uint16_t *cells)
{
    for (;;) {
        unsigned type;
        char name[64];
        if (!nbt_u8(n, &type))
            return 0;
        if (type == TAG_END)
            return 1;
        if (!nbt_name(n, name, sizeof name))
            return 0;
        if (type == TAG_LIST && strcmp(name, "Sections") == 0) {
            if (!parse_sections_list(n, cells))
                return 0;
        } else if (!nbt_skip(n, type)) {
            return 0;
        }
    }
    return 0;
}

static int parse_root_compound(Nbt *n, uint16_t *cells)
{
    for (;;) {
        unsigned type;
        char name[64];
        if (!nbt_u8(n, &type))
            return 0;
        if (type == TAG_END)
            return 1;
        if (!nbt_name(n, name, sizeof name))
            return 0;
        if (type == TAG_COMPOUND && strcmp(name, "Level") == 0) {
            if (!parse_level_compound(n, cells))
                return 0;
        } else if (!nbt_skip(n, type)) {
            return 0;
        }
    }
    return 0;
}

static int parse_chunk_nbt(const unsigned char *p, size_t n, uint16_t *cells)
{
    Nbt nbt;
    unsigned type;
    unsigned nlen;
    nbt.p = p;
    nbt.end = p + n;
    if (!nbt_u8(&nbt, &type) || type != TAG_COMPOUND)
        return 0;
    if (!nbt_be16(&nbt, &nlen) || !nbt_skip_bytes(&nbt, nlen))
        return 0;
    memset(cells, 0, CHUNK_CELLS * sizeof(uint16_t));
    return parse_root_compound(&nbt, cells);
}

static int inflate_payload(int ctype, const unsigned char *in, size_t nin,
                           unsigned char **out, size_t *nout)
{
    z_stream zs;
    unsigned char *buf;
    size_t cap;
    int wbits, rc;
    if (ctype != 1 && ctype != 2)
        return 0;
    if (nin >= 4 && in[0] == 'P' && in[1] == 'K')
        return 0;
    memset(&zs, 0, sizeof zs);
    wbits = (ctype == 1) ? (15 + 16) : 15;
    if (inflateInit2(&zs, wbits) != Z_OK)
        return 0;
    cap = nin * 8u;
    if (cap < 65536u)
        cap = 65536u;
    if (cap > MAX_INFLATE)
        cap = MAX_INFLATE;
    buf = (unsigned char *)malloc(cap);
    if (!buf) {
        inflateEnd(&zs);
        return 0;
    }
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)nin;
    for (;;) {
        size_t used = (size_t)zs.total_out;
        if (used >= MAX_INFLATE) {
            inflateEnd(&zs);
            free(buf);
            return 0;
        }
        if (used == cap) {
            size_t ncap = cap * 2u;
            unsigned char *nb;
            if (ncap > MAX_INFLATE)
                ncap = MAX_INFLATE;
            if (ncap <= cap) {
                inflateEnd(&zs);
                free(buf);
                return 0;
            }
            nb = (unsigned char *)realloc(buf, ncap);
            if (!nb) {
                inflateEnd(&zs);
                free(buf);
                return 0;
            }
            buf = nb;
            cap = ncap;
        }
        zs.next_out = buf + (size_t)zs.total_out;
        zs.avail_out = (uInt)(cap - (size_t)zs.total_out);
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc == Z_STREAM_END)
            break;
        if (rc != Z_OK) {
            inflateEnd(&zs);
            free(buf);
            return 0;
        }
        if (zs.avail_in == 0 && zs.avail_out > 0) {
            inflateEnd(&zs);
            free(buf);
            return 0;
        }
    }
    inflateEnd(&zs);
    *out = buf;
    *nout = (size_t)zs.total_out;
    return 1;
}

static int read_whole_file(const char *path, unsigned char **out, size_t *nout)
{
    FILE *fp;
    struct stat st;
    unsigned char *buf;
    size_t n, got;
    fp = fopen(path, "rb");
    if (!fp)
        return 0;
    if (fstat(fileno(fp), &st) != 0 || st.st_size < 0) {
        fclose(fp);
        return 0;
    }
    n = (size_t)st.st_size;
    buf = (unsigned char *)malloc(n ? n : 1);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    got = fread(buf, 1, n, fp);
    fclose(fp);
    if (got != n) {
        free(buf);
        return 0;
    }
    *out = buf;
    *nout = n;
    return 1;
}

static int decode_mca_chunk(const unsigned char *file, size_t n, int cx, int cz,
                            uint16_t *cells, int *present)
{
    int local = (cx & 31) + (cz & 31) * 32;
    size_t loff = (size_t)local * 4u;
    uint32_t sector, length;
    unsigned ctype;
    size_t off, payload_n;
    unsigned char *raw = NULL;
    size_t raw_n = 0;
    int ok;
    *present = 0;
    if (n < 8192)
        return 0;
    sector = ((uint32_t)file[loff] << 16) | ((uint32_t)file[loff + 1] << 8) |
             (uint32_t)file[loff + 2];
    if (sector == 0)
        return 1;
    if (file[loff + 3] == 0)
        return 0;
    off = (size_t)sector * 4096u;
    if (off + 5 > n)
        return 0;
    length = ((uint32_t)file[off] << 24) | ((uint32_t)file[off + 1] << 16) |
             ((uint32_t)file[off + 2] << 8) | (uint32_t)file[off + 3];
    if (length == 0xffffffffu)
        return 0;
    if (length < 1u)
        return 0;
    if (off + 4u + (size_t)length > n)
        return 0;
    ctype = file[off + 4];
    payload_n = (size_t)length - 1u;
    if (!inflate_payload((int)ctype, file + off + 5, payload_n, &raw, &raw_n))
        return 0;
    ok = parse_chunk_nbt(raw, raw_n, cells);
    free(raw);
    if (!ok)
        return 0;
    *present = 1;
    return 1;
}

static void store_free(Store *st)
{
    int i, n;
    if (!st->grid)
        return;
    n = st->ncx * st->ncz;
    for (i = 0; i < n; i++)
        free(st->grid[i]);
    free(st->grid);
    st->grid = NULL;
    st->loaded = 0;
}

static int store_index(const Store *st, int cx, int cz)
{
    int ix, iz;
    if (cx < st->cx0 || cz < st->cz0)
        return -1;
    ix = cx - st->cx0;
    iz = cz - st->cz0;
    if (ix >= st->ncx || iz >= st->ncz)
        return -1;
    return ix * st->ncz + iz;
}

static uint16_t *store_cells(const Store *st, int cx, int cz)
{
    int i = store_index(st, cx, cz);
    if (i < 0)
        return NULL;
    return st->grid[i];
}

static uint16_t world_get(const Store *st, int x, int y, int z)
{
    uint16_t *cells;
    size_t idx;
    if (y < 0 || y > 255)
        return 0;
    cells = store_cells(st, x >> 4, z >> 4);
    if (!cells)
        return 0;
    idx = (size_t)(x & 15) * 256u * 16u + (size_t)y * 16u + (size_t)(z & 15);
    return cells[idx];
}

static int load_needed(Store *st, const char *world, const Pose *poses,
                       int npose, int rd, int rmax)
{
    char path[4096];
    int i, cx, cz, min_cx, max_cx, min_cz, max_cz, ngrid;
    typedef struct {
        int rx, rz;
        unsigned char *data;
        size_t n;
        int loaded;
        int missing;
    } Reg;
    Reg regs[16];
    int nreg = 0;

    min_cx = INT_MAX;
    max_cx = INT_MIN;
    min_cz = INT_MAX;
    max_cz = INT_MIN;
    for (i = 0; i < npose; i++) {
        int pcx = poses[i].ax >> 4;
        int pcz = poses[i].az >> 4;
        int c0 = (poses[i].ax - rmax) >> 4;
        int c1 = (poses[i].ax + rmax) >> 4;
        int z0 = (poses[i].az - rmax) >> 4;
        int z1 = (poses[i].az + rmax) >> 4;
        if (pcx - rd < min_cx)
            min_cx = pcx - rd;
        if (pcx + rd > max_cx)
            max_cx = pcx + rd;
        if (pcz - rd < min_cz)
            min_cz = pcz - rd;
        if (pcz + rd > max_cz)
            max_cz = pcz + rd;
        if (c0 < min_cx)
            min_cx = c0;
        if (c1 > max_cx)
            max_cx = c1;
        if (z0 < min_cz)
            min_cz = z0;
        if (z1 > max_cz)
            max_cz = z1;
    }
    if (npose <= 0)
        return 0;
    memset(st, 0, sizeof *st);
    st->cx0 = min_cx;
    st->cz0 = min_cz;
    st->ncx = max_cx - min_cx + 1;
    st->ncz = max_cz - min_cz + 1;
    if (st->ncx <= 0 || st->ncz <= 0)
        return 0;
    ngrid = st->ncx * st->ncz;
    st->grid = (uint16_t **)calloc((size_t)ngrid, sizeof(uint16_t *));
    if (!st->grid)
        return 0;
    memset(regs, 0, sizeof regs);

    for (cx = min_cx; cx <= max_cx; cx++) {
        for (cz = min_cz; cz <= max_cz; cz++) {
            int rx = cx >> 5;
            int rz = cz >> 5;
            int ri, present = 0;
            uint16_t *cells;
            Reg *rg = NULL;
            for (ri = 0; ri < nreg; ri++) {
                if (regs[ri].rx == rx && regs[ri].rz == rz) {
                    rg = &regs[ri];
                    break;
                }
            }
            if (!rg) {
                if (nreg >= 16) {
                    store_free(st);
                    return 0;
                }
                rg = &regs[nreg++];
                rg->rx = rx;
                rg->rz = rz;
                snprintf(path, sizeof path, "%s/region/r.%d.%d.mca", world, rx,
                         rz);
                if (access(path, F_OK) != 0) {
                    rg->missing = 1;
                } else if (!read_whole_file(path, &rg->data, &rg->n)) {
                    int k;
                    for (k = 0; k < nreg; k++)
                        free(regs[k].data);
                    store_free(st);
                    fprintf(stderr, "failed reading %s\n", path);
                    return 0;
                } else {
                    rg->loaded = 1;
                    st->file_bytes += rg->n;
                    if (rg->n < 8192) {
                        int k;
                        for (k = 0; k < nreg; k++)
                            free(regs[k].data);
                        store_free(st);
                        fprintf(stderr, "short region %s\n", path);
                        return 0;
                    }
                }
            }
            if (rg->missing)
                continue;
            cells = (uint16_t *)malloc(CHUNK_CELLS * sizeof(uint16_t));
            if (!cells) {
                int k;
                for (k = 0; k < nreg; k++)
                    free(regs[k].data);
                store_free(st);
                return 0;
            }
            if (!decode_mca_chunk(rg->data, rg->n, cx, cz, cells, &present)) {
                int k;
                free(cells);
                for (k = 0; k < nreg; k++)
                    free(regs[k].data);
                store_free(st);
                fprintf(stderr, "bad chunk %d %d\n", cx, cz);
                return 0;
            }
            if (!present) {
                free(cells);
                continue;
            }
            st->grid[store_index(st, cx, cz)] = cells;
            st->loaded++;
        }
    }
    for (i = 0; i < nreg; i++)
        free(regs[i].data);
    return 1;
}

_Static_assert(sizeof(Store) == sizeof(McaStore), "McaStore layout");
_Static_assert(sizeof(Pose) == sizeof(McaPose), "McaPose layout");

uint16_t mca_pack_state(unsigned id, unsigned meta)
{
    return pack_state(id, meta);
}

void mca_store_free(McaStore *st)
{
    store_free((Store *)st);
}

int mca_load(McaStore *st, const char *world, const McaPose *poses, int npose,
             int rd, int rmax)
{
    return load_needed((Store *)st, world, (const Pose *)poses, npose, rd,
                       rmax);
}

uint16_t mca_world_get(const McaStore *st, int x, int y, int z)
{
    return world_get((const Store *)st, x, y, z);
}

unsigned long long mca_nearby_hash(const McaStore *st, int cx, int cy, int cz)
{
    unsigned long long h = 1469598103934665603ULL;
    int x, y, z;
    for (z = cz - 4; z <= cz + 4; z++) {
        for (y = cy - 4; y <= cy + 4; y++) {
            for (x = cx - 4; x <= cx + 4; x++) {
                h ^= (unsigned long long)mca_world_get(st, x, y, z);
                h *= 1099511628211ULL;
            }
        }
    }
    return h;
}
