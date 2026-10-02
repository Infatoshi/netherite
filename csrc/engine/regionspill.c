/* The region stores' parked chunks on disk (regionspill.h). */
#define _GNU_SOURCE   /* sync_file_range */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include "regionspill.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "arena.h"
#include "env.h"
#include "lzpack.h"
#include "world.h"

#define SP (nw_env->rspill)

/* a record: its head, the chunk struct and the caller's bytes compressed
 * together (lzpack.h), the packed blob as chunk_pack made it */
struct rec_head {
    uint32_t magic;
    int32_t cx, cz;
    uint32_t zlen, blob, extra;
};
#define REC_MAGIC 0x6c697073u   /* "spil" */

int rspill_flag(const char *v)
{
    if (v == NULL) return 0;
    if (strcmp(v, "on") == 0) SP.mode = 0;
    else if (strcmp(v, "off") == 0) SP.mode = 1;
    else if (v[0] == '/' && strlen(v) < 900)
    {
        SP.mode = 0;
        SP.dir = v;
    }
    else return 0;
    return 1;
}

static uint32_t rec_hash(int cx, int cz)
{
    uint32_t h = (uint32_t)cx * 0x9e3779b1u ^ (uint32_t)cz * 0x85ebca77u;
    return h ^ h >> 15;
}

static struct rspill_rec *map_find(const struct rspill_map *m, int cx, int cz)
{
    if (m->cap == 0) return NULL;
    for (uint32_t i = rec_hash(cx, cz) & (m->cap - 1);; i = (i + 1) & (m->cap - 1))
    {
        struct rspill_rec *r = &m->v[i];
        if (r->state == 0) return NULL;
        if (r->state == 1 && r->cx == cx && r->cz == cz) return r;
    }
}

/* room for one more record, the tombstones dropped when it grows (the park:
 * outside the tick) */
static void map_room(struct rspill_map *m)
{
    if (m->cap && 4 * (m->n + m->dead + 1) <= 3 * m->cap) return;
    uint32_t cap = 64;
    while (cap < 2 * (m->n + 1)) cap *= 2;
    struct rspill_rec *v = calloc(cap, sizeof *v);
    if (v == NULL) abort();
    for (uint32_t k = 0; k < m->cap; ++k)
    {
        if (m->v[k].state != 1) continue;
        uint32_t i = rec_hash(m->v[k].cx, m->v[k].cz) & (cap - 1);
        while (v[i].state) i = (i + 1) & (cap - 1);
        v[i] = m->v[k];
    }
    free(m->v);
    m->v = v;
    m->cap = cap;
    m->dead = 0;
}

static void room(uint8_t **p, uint32_t *have, uint32_t n)
{
    if (n <= *have) return;
    uint32_t cap = *have ? *have : 16384;
    while (cap < n) cap *= 2;
    free(*p);
    *p = malloc(cap);
    if (*p == NULL) abort();
    *have = cap;
}

static void buf_room(uint32_t n)
{
    room(&SP.buf, &SP.bufcap, n);
}

static int write_all(int fd, const void *p, size_t n, uint64_t off)
{
    const unsigned char *b = p;
    while (n > 0)
    {
        ssize_t w = pwrite(fd, b, n, (off_t)off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return 0;
        b += w;
        n -= (size_t)w;
        off += (uint64_t)w;
    }
    return 1;
}

static int read_all(int fd, void *p, size_t n, uint64_t off)
{
    unsigned char *b = p;
    while (n > 0)
    {
        ssize_t r = pread(fd, b, n, (off_t)off);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return 0;
        b += r;
        n -= (size_t)r;
        off += (uint64_t)r;
    }
    return 1;
}

/* The file's page cache kept small without waiting on the disk (Linux;
 * elsewhere the cache is the kernel's). Once RSPILL_BEHIND_MB past *started,
 * the bytes from *started to end are handed to writeback without a wait,
 * and the stretch handed over the time before (*behind to *started) is
 * waited for, which has finished by then unless the disk is behind by a
 * whole stretch, and dropped from the cache. At most two stretches stay
 * cached. The writeback of each stretch was waited for as it was written
 * until lane/spillfast: 0.3 to 0.6 s a call, up to 1.3 s, on the GPU host's /home
 * while another project read from it at 385 MB/s (an exploring N 32 run
 * spent 20 to 36 s of its workers' time there). */
static void drop_behind(int fd, uint64_t *behind, uint64_t *started, uint64_t end)
{
#if defined(__linux__)
    if (end - *started < (uint64_t)RSPILL_BEHIND_MB << 20) return;
    sync_file_range(fd, (off_t)*started, (off_t)(end - *started), SYNC_FILE_RANGE_WRITE);
    if (*started > *behind)
    {
        sync_file_range(fd, (off_t)*behind, (off_t)(*started - *behind),
                        SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER);
        posix_fadvise(fd, (off_t)*behind, (off_t)(*started - *behind), POSIX_FADV_DONTNEED);
        *behind = *started;
    }
    *started = end;
#else
    (void)fd;
    (void)behind;
    (void)started;
    (void)end;
#endif
}

/* a whole file just written (a compaction's, an adopted store's) written
 * back and dropped, waiting: they are rare, and a whole file at once */
static void drop_all(int fd, uint64_t *behind, uint64_t *started, uint64_t end)
{
#if defined(__linux__)
    sync_file_range(fd, 0, (off_t)end, SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER);
    posix_fadvise(fd, 0, (off_t)end, POSIX_FADV_DONTNEED);
#else
    (void)fd;
#endif
    *behind = *started = end;
}

/* a new file in the directory, unlinked: its descriptor, or -1 */
static int file_new(void)
{
    char path[1024];
    const char *dir = SP.dir;
    if (dir == NULL)
    {
        const char *home = getenv("HOME");
        if (home == NULL) return -1;
        snprintf(path, sizeof path, "%s/.cache", home);
        mkdir(path, 0755);
        snprintf(path, sizeof path, "%s/.cache/netherite-spill", home);
    }
    else snprintf(path, sizeof path, "%s", dir);
    mkdir(path, 0755);
    size_t at = strlen(path);
    snprintf(path + at, sizeof path - at, "/spill-XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0)
    {
        fprintf(stderr, "regionspill: %s does not open (%s): parked chunks stay in memory\n", path, strerror(errno));
        return -1;
    }
    unlink(path);
    return fd;
}

/* every live record of the file from into a new file, the offsets moved;
 * the old descriptor closed when close_old */
static int move_records(int from, int close_old)
{
    int fd = file_new();
    if (fd < 0) return 0;
    uint64_t end = 0;
    for (int d = 0; d < 3; ++d)
    {
        struct rspill_map *m = &SP.dims[d];
        for (uint32_t k = 0; k < m->cap; ++k)
        {
            struct rspill_rec *r = &m->v[k];
            if (r->state != 1) continue;
            buf_room(r->len);
            if (!read_all(from, SP.buf, r->len, r->off) || !write_all(fd, SP.buf, r->len, end))
            {
                fprintf(stderr, "regionspill: a record does not move to the new file\n");
                abort();
            }
            r->off = end;
            end += r->len;
        }
    }
    if (close_old) close(from);
    SP.fd1 = fd + 1;
    SP.end = SP.live = end;
    drop_all(fd, &SP.behind1, &SP.started1, end);
    return 1;
}

int rspill_has(int dim, int cx, int cz)
{
    return map_find(&SP.dims[dim], cx, cz) != NULL;
}

int rspill_ok(const struct chunk *c)
{
    if (SP.mode != 0 || SP.fd1 < 0) return 0;
    for (int s = 0; s < 16; ++s)
        if (c->band[s]) return 0;
    return 1;
}

int rspill_put(int dim, struct chunk *c, const void *extra, uint32_t extra_n)
{
    if (!rspill_ok(c)) return 0;
    if (SP.fd1 == 0)
    {
        int fd = file_new();
        if (fd < 0) { SP.fd1 = -1; return 0; }
        SP.fd1 = fd + 1;
    }
    else if (SP.end - SP.live >= (uint64_t)RSPILL_COMPACT_MB << 20 && SP.end - SP.live > SP.live)
    {
        if (move_records(SP.fd1 - 1, 1)) ++SP.compactions;
    }

    /* the struct as parked, less what names memory (no bands), and the
     * caller's bytes; the blob after them. The take decompresses into xbuf. */
    uint32_t raw = (uint32_t)sizeof(struct chunk) + extra_n;
    room(&SP.xbuf, &SP.xbufcap, raw + LZP_SLACK);
    if (SP.ztab == NULL && (SP.ztab = malloc(LZP_TABLE_BYTES)) == NULL) abort();
    memcpy(SP.xbuf, c, sizeof *c);
    ((struct chunk *)(void *)SP.xbuf)->packed = NULL;
    ((struct chunk *)(void *)SP.xbuf)->tes.v = NULL;
    if (extra_n) memcpy(SP.xbuf + sizeof *c, extra, extra_n);
    buf_room((uint32_t)(sizeof(struct rec_head) + LZP_BOUND(raw) + c->packed_n));
    uint32_t zlen = lzp_compress(SP.xbuf, raw, SP.buf + sizeof(struct rec_head), SP.ztab);
    struct rec_head rh = {REC_MAGIC, c->cx, c->cz, zlen, c->packed_n, extra_n};
    memcpy(SP.buf, &rh, sizeof rh);
    if (c->packed_n) memcpy(SP.buf + sizeof rh + zlen, c->packed, c->packed_n);
    uint32_t len = (uint32_t)(sizeof rh + zlen + c->packed_n);
    if (!write_all(SP.fd1 - 1, SP.buf, len, SP.end))
    {
        fprintf(stderr, "regionspill: a write failed (%s): parked chunks stay in memory from here\n", strerror(errno));
        return 0;
    }

    struct rspill_map *m = &SP.dims[dim];
    map_room(m);
    uint32_t i = rec_hash(c->cx, c->cz) & (m->cap - 1);
    while (m->v[i].state == 1) i = (i + 1) & (m->cap - 1);
    if (m->v[i].state == 2) --m->dead;
    m->v[i] = (struct rspill_rec){c->cx, c->cz, SP.end, len, 1};
    ++m->n;
    SP.end += len;
    SP.live += len;
    drop_behind(SP.fd1 - 1, &SP.behind1, &SP.started1, SP.end);
    ++SP.puts;
    chunk_free(c);
    return 1;
}

struct chunk *rspill_take(int dim, int cx, int cz, const uint8_t **extra, uint32_t *extra_n)
{
    struct rspill_rec *r = map_find(&SP.dims[dim], cx, cz);
    if (r == NULL) return NULL;
    struct rec_head rh;
    if (r->len > SP.bufcap || !read_all(SP.fd1 - 1, SP.buf, r->len, r->off) || (memcpy(&rh, SP.buf, sizeof rh), 0) ||
        rh.magic != REC_MAGIC || rh.cx != cx || rh.cz != cz || sizeof rh + rh.zlen + rh.blob != r->len)
    {
        fprintf(stderr, "regionspill: the record of chunk (%d,%d) does not read back\n", cx, cz);
        abort();
    }

    uint32_t raw = (uint32_t)sizeof(struct chunk) + rh.extra;
    if (raw + LZP_SLACK > SP.xbufcap)
    {
        fprintf(stderr, "regionspill: the record of chunk (%d,%d) is past the buffer\n", cx, cz);
        abort();
    }
    if (lzp_decompress(SP.buf + sizeof rh, rh.zlen, SP.xbuf, raw) != (int64_t)raw)
    {
        fprintf(stderr, "regionspill: the struct of chunk (%d,%d) does not decompress\n", cx, cz);
        abort();
    }
    struct chunk *c = chunk_new();
    memcpy(c, SP.xbuf, sizeof *c);
    *extra = SP.xbuf + sizeof *c;
    *extra_n = rh.extra;
    c->packed = NULL;
    c->packed_n = 0;
    if (rh.blob) chunk_unpack_blob(c, SP.buf + sizeof rh + rh.zlen, rh.blob);

    r->state = 2;
    --SP.dims[dim].n;
    ++SP.dims[dim].dead;
    SP.live -= r->len;
    ++SP.takes;
    return c;
}

/* a blob: its raw length, its compressed length, the compressed bytes */
struct blob_head {
    uint32_t magic, raw, zlen;
};
#define BLOB_MAGIC 0x626f6c62u   /* "blob" */

uint64_t rspill_blob_put(const void *p, uint32_t n)
{
    if (SP.mode != 0 || SP.fd2 < 0) return 0;
    if (SP.fd2 == 0)
    {
        int fd = file_new();
        if (fd < 0) { SP.fd2 = -1; return 0; }
        SP.fd2 = fd + 1;
    }
    room(&SP.xbuf, &SP.xbufcap, n + LZP_SLACK);
    if (SP.ztab == NULL && (SP.ztab = malloc(LZP_TABLE_BYTES)) == NULL) abort();
    buf_room((uint32_t)(sizeof(struct blob_head) + LZP_BOUND(n)));
    uint32_t zlen = lzp_compress(p, n, SP.buf + sizeof(struct blob_head), SP.ztab);
    struct blob_head bh = {BLOB_MAGIC, n, zlen};
    memcpy(SP.buf, &bh, sizeof bh);
    uint32_t len = (uint32_t)(sizeof bh + zlen);
    if (!write_all(SP.fd2 - 1, SP.buf, len, SP.end2)) return 0;
    uint64_t at = SP.end2 + 1;
    SP.end2 += len;
    SP.live2 += len;
    drop_behind(SP.fd2 - 1, &SP.behind2, &SP.started2, SP.end2);
    return at;
}

const uint8_t *rspill_blob_get(uint64_t at, uint32_t *n)
{
    struct blob_head bh;
    if (at == 0 || !read_all(SP.fd2 - 1, &bh, sizeof bh, at - 1) || bh.magic != BLOB_MAGIC || bh.raw + LZP_SLACK > SP.xbufcap ||
        sizeof bh + bh.zlen > SP.bufcap || !read_all(SP.fd2 - 1, SP.buf, bh.zlen, at - 1 + sizeof bh))
    {
        fprintf(stderr, "regionspill: a blob does not read back\n");
        abort();
    }
    if (lzp_decompress(SP.buf, bh.zlen, SP.xbuf, bh.raw) != (int64_t)bh.raw)
    {
        fprintf(stderr, "regionspill: a blob does not decompress\n");
        abort();
    }
    SP.live2 -= sizeof bh + bh.zlen;
    *n = bh.raw;
    return SP.xbuf;
}

void rspill_adopt(void)
{
    /* the start's files stay the start's: the records and blobs they hold
     * come to files of this environment's own, the blobs at their offsets */
    if (SP.fd1 > 0 && !move_records(SP.fd1 - 1, 0))
    {
        fprintf(stderr, "regionspill: an adopted store has no file of its own\n");
        abort();
    }
    if (SP.fd2 > 0)
    {
        int from = SP.fd2 - 1, fd = file_new();
        if (fd < 0) { fprintf(stderr, "regionspill: an adopted store has no blob file of its own\n"); abort(); }
        buf_room(1 << 20);
        for (uint64_t at = 0; at < SP.end2;)
        {
            uint32_t k = SP.end2 - at < (1u << 20) ? (uint32_t)(SP.end2 - at) : 1u << 20;
            if (!read_all(from, SP.buf, k, at) || !write_all(fd, SP.buf, k, at)) { fprintf(stderr, "regionspill: the blob file does not copy\n"); abort(); }
            at += k;
        }
        SP.fd2 = fd + 1;
        drop_all(fd, &SP.behind2, &SP.started2, SP.end2);
    }
}

void rspill_close(struct region_spill *s)
{
    if (s->fd1 > 0) close(s->fd1 - 1);
    if (s->fd2 > 0) close(s->fd2 - 1);
    for (int d = 0; d < 3; ++d) free(s->dims[d].v);
    free(s->buf);
    free(s->xbuf);
    free(s->sbuf);
    free(s->ztab);
    int mode = s->mode;
    const char *dir = s->dir;
    memset(s, 0, sizeof *s);
    s->mode = mode;
    s->dir = dir;
}

size_t rspill_index_bytes(const struct region_spill *s)
{
    size_t n = (size_t)s->bufcap + s->xbufcap + s->sbufcap + (s->ztab != NULL ? LZP_TABLE_BYTES : 0);
    for (int d = 0; d < 3; ++d) n += (size_t)s->dims[d].cap * sizeof(struct rspill_rec);
    return n;
}

uint64_t rspill_records(const struct region_spill *s)
{
    return (uint64_t)s->dims[0].n + s->dims[1].n + s->dims[2].n;
}

/* ------------------------------------------------ a byte stream and NBT */

void rspill_out_put(struct rspill_out *o, const void *p, size_t n)
{
    /* the take's string buffer, made where writes happen (outside the tick) */
    if (SP.sbufcap == 0) room(&SP.sbuf, &SP.sbufcap, 65536);
    if (o->n + n > o->cap)
    {
        size_t cap = o->cap ? o->cap : 4096;
        while (cap < o->n + n) cap *= 2;
        uint8_t *q = realloc(o->p, cap);
        if (q == NULL) abort();
        o->p = q;
        o->cap = cap;
    }
    memcpy(o->p + o->n, p, n);
    o->n += n;
}

static void out_u32(struct rspill_out *o, uint32_t v)
{
    rspill_out_put(o, &v, sizeof v);
}

static void out_str(struct rspill_out *o, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    out_u32(o, n);
    rspill_out_put(o, s, n);
}

/* a node's type and payload (a container's count; its children follow) */
static void nbt_node_out(struct rspill_out *o, const nbt *v)
{
    uint8_t t = (uint8_t)nbt_kind(v);
    rspill_out_put(o, &t, 1);
    switch (t)
    {
    case NBT_BYTE: case NBT_SHORT: case NBT_INT: case NBT_LONG:
    {
        int64_t x = nbt_int_value(v);
        rspill_out_put(o, &x, sizeof x);
        break;
    }
    case NBT_FLOAT: out_u32(o, nbt_float_bits(v)); break;
    case NBT_DOUBLE:
    {
        uint64_t x = nbt_double_bits(v);
        rspill_out_put(o, &x, sizeof x);
        break;
    }
    case NBT_STRING: out_str(o, nbt_string_value(v)); break;
    case NBT_BYTE_ARRAY:
    {
        int n = 0;
        const signed char *a = nbt_byte_array(v, &n);
        out_u32(o, (uint32_t)n);
        rspill_out_put(o, a, (size_t)n);
        break;
    }
    case NBT_INT_ARRAY:
    {
        int n = 0;
        const int *a = nbt_int_array(v, &n);
        out_u32(o, (uint32_t)n);
        rspill_out_put(o, a, (size_t)n * sizeof *a);
        break;
    }
    case NBT_LIST: out_u32(o, (uint32_t)nbt_list_size(v)); break;
    case NBT_COMPOUND: out_u32(o, (uint32_t)nbt_field_count(v)); break;
    }
}

#define NBT_DEPTH 64
struct nbt_wframe { const nbt *v; int next, n; };
struct nbt_rframe { nbt *v; int left, comp; };

void rspill_nbt_out(struct rspill_out *o, const nbt *root)
{
    /* pre-order with a stack of (container, next child): no recursion */
    struct nbt_wframe st[NBT_DEPTH];
    int top = 0;
    nbt_node_out(o, root);
    if (nbt_kind(root) == NBT_LIST || nbt_kind(root) == NBT_COMPOUND)
        st[top++] = (struct nbt_wframe){root, 0, nbt_kind(root) == NBT_LIST ? nbt_list_size(root) : nbt_field_count(root)};
    while (top > 0)
    {
        struct nbt_wframe *f = &st[top - 1];
        if (f->next == f->n) { --top; continue; }
        const nbt *c;
        if (nbt_kind(f->v) == NBT_COMPOUND)
        {
            out_str(o, nbt_field_key(f->v, f->next));
            c = nbt_field_value(f->v, f->next);
        }
        else c = nbt_list_get(f->v, f->next);
        ++f->next;
        nbt_node_out(o, c);
        if (nbt_kind(c) == NBT_LIST || nbt_kind(c) == NBT_COMPOUND)
        {
            if (top == NBT_DEPTH) { fprintf(stderr, "regionspill: an NBT tree deeper than %d\n", NBT_DEPTH); abort(); }
            st[top++] = (struct nbt_wframe){c, 0, nbt_kind(c) == NBT_LIST ? nbt_list_size(c) : nbt_field_count(c)};
        }
    }
}

static void in_bytes(const uint8_t **p, const uint8_t *end, void *out, size_t n)
{
    if ((size_t)(end - *p) < n) { fprintf(stderr, "regionspill: a record ends early\n"); abort(); }
    memcpy(out, *p, n);
    *p += n;
}

uint32_t rspill_in_u32(const uint8_t **p, const uint8_t *end)
{
    uint32_t v;
    in_bytes(p, end, &v, sizeof v);
    return v;
}

void rspill_in(const uint8_t **p, const uint8_t *end, void *out, size_t n)
{
    in_bytes(p, end, out, n);
}

/* a string of the stream, NUL-terminated in place of nothing: copied into
 * key (at most keycap bytes with its NUL) */
static void in_str(const uint8_t **p, const uint8_t *end, char *key, size_t keycap)
{
    uint32_t n = rspill_in_u32(p, end);
    if (n >= keycap) { fprintf(stderr, "regionspill: an NBT string of %u bytes\n", n); abort(); }
    in_bytes(p, end, key, n);
    key[n] = 0;
}

/* one node (a container empty, its count into *n) */
static nbt *nbt_node_in(const uint8_t **p, const uint8_t *end, int *n, char *str, size_t strcap)
{
    uint8_t t;
    in_bytes(p, end, &t, 1);
    *n = 0;
    switch (t)
    {
    case NBT_BYTE: case NBT_SHORT: case NBT_INT: case NBT_LONG:
    {
        int64_t x;
        in_bytes(p, end, &x, sizeof x);
        return t == NBT_BYTE ? nbt_new_byte((int)x) : t == NBT_SHORT ? nbt_new_short((int)x)
             : t == NBT_INT ? nbt_new_int((int)x) : nbt_new_long(x);
    }
    case NBT_FLOAT:
    {
        uint32_t b = rspill_in_u32(p, end);
        float f;
        memcpy(&f, &b, sizeof f);
        return nbt_new_float(f);
    }
    case NBT_DOUBLE:
    {
        double d;
        in_bytes(p, end, &d, sizeof d);
        return nbt_new_double(d);
    }
    case NBT_STRING:
        in_str(p, end, str, strcap);
        return nbt_new_string(str);
    case NBT_BYTE_ARRAY:
    {
        uint32_t k = rspill_in_u32(p, end);
        if ((size_t)(end - *p) < k) break;
        nbt *v = nbt_new_byte_array((const signed char *)*p, (int)k);
        *p += k;
        return v;
    }
    case NBT_INT_ARRAY:
    {
        uint32_t k = rspill_in_u32(p, end);
        if ((size_t)(end - *p) / sizeof(int) < k) break;
        /* the bytes may sit unaligned: through the string buffer */
        if (k * sizeof(int) > strcap) break;
        in_bytes(p, end, str, k * sizeof(int));
        return nbt_new_int_array((const int *)(void *)str, (int)k);
    }
    case NBT_LIST:
        *n = (int)rspill_in_u32(p, end);
        return nbt_new_list();
    case NBT_COMPOUND:
        *n = (int)rspill_in_u32(p, end);
        return nbt_new_compound();
    }
    fprintf(stderr, "regionspill: an NBT node does not read back\n");
    abort();
}

nbt *rspill_nbt_in(const uint8_t **p, const uint8_t *end)
{
    char *str = (char *)SP.sbuf;
    size_t strcap = SP.sbufcap;
    struct nbt_rframe st[NBT_DEPTH];
    int top = 0, n;
    nbt *root = nbt_node_in(p, end, &n, str, strcap);
    int k = nbt_kind(root);
    if (k == NBT_LIST || k == NBT_COMPOUND) st[top++] = (struct nbt_rframe){root, n, k == NBT_COMPOUND};
    /* a compound's key waits in its own buffer while the value reads into str */
    char key[256];
    while (top > 0)
    {
        struct nbt_rframe *f = &st[top - 1];
        if (f->left == 0) { --top; continue; }
        --f->left;
        if (f->comp) in_str(p, end, key, sizeof key);
        nbt *c = nbt_node_in(p, end, &n, str, strcap);
        if (f->comp) nbt_put(f->v, key, c);
        else nbt_list_add(f->v, c);
        k = nbt_kind(c);
        if (k == NBT_LIST || k == NBT_COMPOUND)
        {
            if (top == NBT_DEPTH) { fprintf(stderr, "regionspill: an NBT tree deeper than %d\n", NBT_DEPTH); abort(); }
            st[top++] = (struct nbt_rframe){c, n, k == NBT_COMPOUND};
        }
    }
    return root;
}
