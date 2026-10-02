/* See nbtbin.h. */
#include "env.h"
#include "nbtbin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbtjson_tree.h"
#include "nbtw.h"
#include "sha1.h"

/* The tag ids NBTBase.getId returns, which are not the nbtjson enum's order:
 * byte, short, int, long, float, double, byte[], string, list, compound,
 * int[]. */
static int tag_id(nbt_type t)
{
    switch (t)
    {
        case NBT_BYTE: return 1;
        case NBT_SHORT: return 2;
        case NBT_INT: return 3;
        case NBT_LONG: return 4;
        case NBT_FLOAT: return 5;
        case NBT_DOUBLE: return 6;
        case NBT_BYTE_ARRAY: return 7;
        case NBT_STRING: return 8;
        case NBT_LIST: return 9;
        case NBT_COMPOUND: return 10;
        case NBT_INT_ARRAY: return 11;
        default: return 0;
    }
}

static void grow(nbtbin *b, size_t extra)
{
    size_t was = b->cap;
    while (b->n + extra > b->cap) b->cap = b->cap ? b->cap * 2 : 1024;
    b->p = realloc(b->p, b->cap);
    if (b->p == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
    /* the new room holds whatever the recycled memory held (pointers among
     * it pin buried livings in grave.c's scan); only NBT bytes after this */
    memset(b->p + was, 0, b->cap - was);
}

/* Room for extra more bytes; the pointer to write them at, already counted.
 * NBTBIN_SLACK more stay allocated past them (nbtbin.h). */
static inline uint8_t *reserve(nbtbin *b, size_t extra)
{
    if (b->n + extra + NBTBIN_SLACK > b->cap) grow(b, extra + NBTBIN_SLACK);
    uint8_t *p = b->p + b->n;
    b->n += extra;
    return p;
}

void nbtbin_init(nbtbin *b)
{
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
}

void nbtbin_free(nbtbin *b)
{
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

static void put_bytes(nbtbin *b, const void *p, size_t n)
{
    memcpy(reserve(b, n), p, n);
}

static void put_u8(nbtbin *b, unsigned v)
{
    *reserve(b, 1) = (uint8_t)v;
}

static inline void be16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline void be64(uint8_t *p, uint64_t v)
{
    be32(p, (uint32_t)(v >> 32));
    be32(p + 4, (uint32_t)v);
}

static void put_be16(nbtbin *b, unsigned v)
{
    be16(reserve(b, 2), v);
}

static void put_be32(nbtbin *b, uint32_t v)
{
    be32(reserve(b, 4), v);
}

/* DataOutputStream.writeUTF: the modified UTF-8 length in bytes then the bytes
 * of every UTF-16 code unit (one to three bytes, a NUL as 0xC0 0x80). The keys
 * here are ASCII, but a written string (a painting's Motive, a throwable's
 * ownerName) is not guaranteed to be. */
static void put_utf(nbtbin *b, const char *s)
{
    size_t len = 0;

    while (s[len] != 0 && (unsigned char)s[len] < 0x80) ++len;

    if (s[len] == 0 && len <= 0xffff)
    {
        uint8_t *p = reserve(b, 2 + len);
        be16(p, (unsigned)len);
        memcpy(p + 2, s, len);
        return;
    }

    size_t nbytes = 0;

    for (const unsigned char *p = (const unsigned char *)s; *p != 0;)
    {
        unsigned c = *p;

        if (c < 0x80)
        {
            nbytes += c == 0 ? 2 : 1;
            ++p;
        }
        else if ((c & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80)
        {
            nbytes += 2;
            p += 2;
        }
        else if ((c & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80)
        {
            nbytes += 3;
            p += 3;
        }
        else
        {
            nbytes += 3;
            ++p;
        }
    }

    if (nbytes > 0xffff) nbytes = 0xffff;

    put_be16(b, (unsigned)nbytes);

    size_t written = 0;

    for (const unsigned char *p = (const unsigned char *)s; *p != 0 && written < nbytes;)
    {
        unsigned c = *p;

        if (c >= 0x80 && (c & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80)
        {
            uint8_t two[2] = {c, p[1]};
            put_bytes(b, two, 2);
            p += 2;
            written += 2;
        }
        else if (c >= 0x80 && (c & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80)
        {
            uint8_t three[3] = {c, p[1], p[2]};
            put_bytes(b, three, 3);
            p += 3;
            written += 3;
        }
        else if (c != 0)
        {
            put_u8(b, c);
            ++p;
            written += 1;
        }
        else
        {
            put_u8(b, 0xc0);
            put_u8(b, 0x80);
            ++p;
            written += 2;
        }
    }
}

static void write_payload(const nbt *v, nbtbin *b);

/* Where a key falls inside its HashMap bucket when the compound came from
 * sorted text (itemtag.h: a stack's tag is interned by its canonical text,
 * keys sorted) while Java inserted it in another order. The one vanilla
 * compound whose bucket-mates are inserted out of name order is a firework
 * charge's Explosion (RecipeFireworks: Flicker and Trail as the grid scan
 * meets them, then Colors and Type; the fade recipe adds FadeColors last):
 * Trail and Colors share a bucket, and so do Flicker and FadeColors. Every
 * other key keeps rank 1, so its order is the tree's. */
static int bucket_rank(const struct nbt_key *key)
{
    if (key->len == 7 && memcmp(key->s, "Flicker", 7) == 0) return 0;
    if (key->len == 5 && memcmp(key->s, "Trail", 5) == 0) return 0;
    if (key->len == 10 && memcmp(key->s, "FadeColors", 10) == 0) return 2;
    return 1;
}

/* The compound's fields in the JDK 8 HashMap's iteration order: bucket
 * (hash ^ hash >>> 16) & (capacity - 1) ascending, insertion order inside a
 * bucket, the capacity 16 doubled while size > capacity * 0.75. The capacity
 * is a power of two, so a counting sort on the bucket index (then the key's
 * bucket_rank) keeps both. The key hashes were taken as the keys were
 * interned (nbtjson_tree.h). */
static void write_compound(const nbt *v, nbtbin *b)
{
    int n = v->u.comp.n;
    const struct nbt_field *f = v->u.comp.f;
    int cap = 16;

    while (n > cap * 3 / 4) cap *= 2;

    int order_buf[64], count_buf[384];
    int *order = n <= 64 ? order_buf : malloc((size_t)n * sizeof *order);
    int *count = cap * 3 <= 384 ? count_buf : malloc((size_t)cap * 3 * sizeof *count);

    if (order == NULL || count == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }

    memset(count, 0, (size_t)cap * 3 * sizeof *count);

    for (int i = 0; i < n; ++i)
    {
        uint32_t h = f[i].key->hash;
        ++count[(int)((h ^ (h >> 16)) & (uint32_t)(cap - 1)) * 3 + bucket_rank(f[i].key)];
    }

    for (int k = 0, sum = 0; k < cap * 3; ++k)
    {
        int c0 = count[k];
        count[k] = sum;
        sum += c0;
    }

    for (int i = 0; i < n; ++i)
    {
        uint32_t h = f[i].key->hash;
        order[count[(int)((h ^ (h >> 16)) & (uint32_t)(cap - 1)) * 3 + bucket_rank(f[i].key)]++] = i;
    }

    for (int i = 0; i < n; ++i)
    {
        const struct nbt_key *key = f[order[i]].key;
        const nbt *val = f[order[i]].val;

        /* NBTBase.func_150298_a: the tag byte, then the name, then the value */
        if (key->ascii)
        {
            uint8_t *p = reserve(b, 3 + key->len);
            p[0] = (uint8_t)tag_id(val->type);
            be16(p + 1, (unsigned)key->len);
            memcpy(p + 3, key->s, key->len);
        }
        else
        {
            put_u8(b, (unsigned)tag_id(val->type));
            put_utf(b, key->s);
        }

        write_payload(val, b);
    }

    put_u8(b, 0); /* the compound's end tag */

    if (order != order_buf) free(order);
    if (count != count_buf) free(count);
}

static void write_payload(const nbt *v, nbtbin *b)
{
    switch (v->type)
    {
        case NBT_BYTE: put_u8(b, (unsigned)(uint8_t)v->u.i); break;
        case NBT_SHORT: put_be16(b, (unsigned)(uint16_t)v->u.i); break;
        case NBT_INT: put_be32(b, (uint32_t)v->u.i); break;
        case NBT_LONG: be64(reserve(b, 8), (uint64_t)v->u.i); break;

        case NBT_FLOAT:
        {
            uint32_t bits;
            memcpy(&bits, &v->u.f, sizeof bits);
            put_be32(b, bits);
            break;
        }

        case NBT_DOUBLE:
        {
            uint64_t bits;
            memcpy(&bits, &v->u.d, sizeof bits);
            be64(reserve(b, 8), bits);
            break;
        }

        case NBT_STRING: put_utf(b, v->u.s); break;

        case NBT_BYTE_ARRAY:
        {
            int n = v->u.bytes.n;
            put_be32(b, (uint32_t)n);
            if (n > 0) put_bytes(b, v->u.bytes.v, (size_t)n);
            break;
        }

        case NBT_INT_ARRAY:
        {
            int n = v->u.ints.n;
            uint8_t *p = reserve(b, 4 + (size_t)n * 4);
            be32(p, (uint32_t)n);

            for (int i = 0; i < n; ++i) be32(p + 4 + (size_t)i * 4, (uint32_t)v->u.ints.v[i]);

            break;
        }

        case NBT_LIST:
        {
            int n = v->u.list.n;
            uint8_t *p = reserve(b, 5);
            p[0] = n > 0 ? (uint8_t)tag_id(v->u.list.v[0]->type) : 0;
            be32(p + 1, (uint32_t)n);

            for (int i = 0; i < n; ++i) write_payload(v->u.list.v[i], b);

            break;
        }

        case NBT_COMPOUND: write_compound(v, b); break;
        default: break;
    }
}

void nbtbin_write_root(const nbt *root, nbtbin *b)
{
    /* CompressedStreamTools.write's func_150663_a: the root's id (10), then its
     * name -- the empty string -- then its payload */
    put_u8(b, 10);
    put_utf(b, "");
    write_compound(root, b);
}

static uint64_t digest_long(const uint8_t digest[20])
{
    uint64_t v = 0;

    for (int i = 0; i < 8; ++i) v = (v << 8) | digest[i];

    return v;
}

uint64_t nbtbin_long(const nbt *root)
{
    nbtbin *b = &nw_env->nbtbin.long_buf;
    uint8_t digest[20];

    b->n = 0;
    nbtbin_write_root(root, b);
    sha1(b->p, b->n, digest);
    return digest_long(digest);
}

/* One memo slot (struct nbtbin_memo_slot, nbtbin.h), MEMO_SLOTS of them per
 * environment. */
#define memo_slot nbtbin_memo_slot
#define MEMO_SLOTS NBTBIN_MEMO_SLOTS

#define memo (nw_env->nbtbin.memo)
#define memo_scratch (nw_env->nbtbin.memo_scratch)

static uint64_t memo_bytes(struct memo_slot *m);

uint64_t nbtbin_long_memo(int key, const nbt *root)
{
    struct memo_slot *m = &memo[(unsigned)key & (MEMO_SLOTS - 1)];
    size_t rawn = 0;
    const uint8_t *raw = nbt_scratch_bytes(&rawn);

    /* the same scratch bytes from the same root: the same tree (nbtjson.h) */
    if (raw != NULL && m->raw != NULL && m->root == root && m->rawn == rawn && memcmp(m->raw, raw, rawn) == 0)
        return m->v;

    if (raw != NULL)
    {
        if (rawn > m->rawcap)
        {
            m->rawcap = rawn > 2 * m->rawcap ? rawn : 2 * m->rawcap;
            free(m->raw);
            m->raw = calloc(1, m->rawcap);
            if (m->raw == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
        }

        memcpy(m->raw, raw, rawn);
        m->rawn = rawn;
        m->root = root;
    }
    else
        m->root = NULL;

    memo_scratch.n = 0;
    nbtbin_write_root(root, &memo_scratch);
    return memo_bytes(m);
}

/* The memo's root for bytes nbtw wrote (unordered in m->raw): no tree is
 * at this address. */
static const char written_mark;
#define WRITTEN_ROOT ((const struct nbt *)(const void *)&written_mark)

uint64_t nbtw_bin_long_memo(struct nbtw *w, int key)
{
    struct memo_slot *m = &memo[(unsigned)key & (MEMO_SLOTS - 1)];
    nbtbin *b = w->b;

    if (b != &memo_scratch) { fprintf(stderr, "nbtw_bin_long_memo: not the memo's buffer\n"); abort(); }
    if (m->root == WRITTEN_ROOT && m->rawn == b->n && memcmp(m->raw, b->p, b->n) == 0) return m->v;

    if (b->n > m->rawcap)
    {
        m->rawcap = b->n > 2 * m->rawcap ? b->n : 2 * m->rawcap;
        free(m->raw);
        m->raw = calloc(1, m->rawcap);
        if (m->raw == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
    }

    memcpy(m->raw, b->p, b->n);
    m->rawn = b->n;
    m->root = WRITTEN_ROOT;
    nbtw_bin_order(w);
    return memo_bytes(m);
}

/* The memo's value for the bytes in memo_scratch: the slot's when they are
 * its last bytes, else SHA-1 resumed from the slot's state after the longest
 * run of leading 64-byte blocks the two share. */
static uint64_t memo_bytes(struct memo_slot *m)
{
    nbtbin *b = &memo_scratch;
    size_t k = 0;
    size_t n = b->n, full = n / 64;

    if (m->p != NULL)
    {
        if (m->n == n && memcmp(m->p, b->p, n) == 0) return m->v;

        size_t mfull = m->n / 64;

        while (k < full && k < mfull && memcmp(m->p + k * 64, b->p + k * 64, 64) == 0) ++k;
    }

    if (full + 1 > m->stcap)
    {
        m->stcap = full + 1 > 2 * m->stcap ? full + 1 : 2 * m->stcap;
        m->st = realloc(m->st, m->stcap * sizeof *m->st);
        if (m->st == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
    }

    if (k == 0) sha1_init(m->st[0]);

    for (size_t j = k; j < full; ++j)
    {
        memcpy(m->st[j + 1], m->st[j], sizeof m->st[j]);
        sha1_blocks(m->st[j + 1], b->p + j * 64, 1);
    }

    uint32_t h[5];
    uint8_t digest[20];

    memcpy(h, m->st[full], sizeof h);
    sha1_final(h, b->p + full * 64, n - full * 64, n, digest);

    /* the slot keeps these bytes; its old buffer is the next scratch */
    uint8_t *old = m->p;
    size_t oldcap = m->cap;

    m->p = b->p;
    m->cap = b->cap;
    m->n = n;
    m->v = digest_long(digest);
    b->p = old;
    b->cap = oldcap;
    return m->v;
}

/* ------------------------------------------------------------------ nbtw.h */

void nbtw_tree(struct nbtw *w, nbt *root)
{
    w->bin = 0;
    w->b = NULL;
    w->nf = 0;
    w->depth = 1;
    memset(&w->st[0], 0, sizeof w->st[0]);
    w->st[0].node = root;
}

void nbtw_bin_begin(struct nbtw *w, nbtbin *b)
{
    w->bin = 1;
    w->b = b;
    w->nf = 0;
    w->depth = 1;
    w->wf = nw_env->nbtbin.wf;
    w->wfcap = nw_env->nbtbin.wfcap;
    w->nc = w->nd = 0;
    memset(&w->st[0], 0, sizeof w->st[0]);
    b->n = 0;
    uint8_t *p = reserve(b, 3);
    p[0] = 10;
    be16(p + 1, 0);
}

static void w_push(struct nbtw *w, nbt *node, int list, size_t start, int fbase)
{
    if (w->depth >= NBTW_DEPTH) { fprintf(stderr, "nbtw: nested deeper than %d\n", NBTW_DEPTH); exit(1); }
    struct nbtw_frame *f = &w->st[w->depth++];
    f->node = node;
    f->list = list;
    f->start = start;
    f->fbase = fbase;
    f->count = 0;
    f->elem = 0;
}

#define copy16 nbtw_copy16

__attribute__((noinline)) static void wf_grow(struct nbtw *w)
{
    int was = nw_env->nbtbin.wfcap;
    nw_env->nbtbin.wfcap = was ? 2 * was : 256;
    nw_env->nbtbin.wf = realloc(nw_env->nbtbin.wf, (size_t)nw_env->nbtbin.wfcap * sizeof *nw_env->nbtbin.wf);
    if (nw_env->nbtbin.wf == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
    memset(nw_env->nbtbin.wf + was, 0, (size_t)(nw_env->nbtbin.wfcap - was) * sizeof *nw_env->nbtbin.wf);
    w->wf = nw_env->nbtbin.wf;
    w->wfcap = nw_env->nbtbin.wfcap;
}

/* The bytes of one value in the binary output, already counted, after its
 * field header (a compound's: tag, key; a list's element has none). */
static inline __attribute__((always_inline)) uint8_t *bin_value(struct nbtw *w, const struct nbt_key *k, int tag,
                                                                size_t payload)
{
    struct nbtw_frame *f = &w->st[w->depth - 1];
    nbtbin *b = w->b;

    if (f->list)
    {
        if (f->count++ == 0) f->elem = tag;
        return reserve(b, payload);
    }

    if (w->nf == w->wfcap) wf_grow(w);
    w->wf[w->nf].off = b->n;
    w->wf[w->nf].key = k;
    ++w->nf;

    if (k->ascii)
    {
        uint8_t *p = reserve(b, 3 + k->len + payload);
        p[0] = (uint8_t)tag;
        be16(p + 1, (unsigned)k->len);
        copy16(p + 3, (const uint8_t *)k->s, k->len);
        return p + 3 + k->len;
    }

    put_u8(b, (unsigned)tag);
    put_utf(b, k->s);
    return reserve(b, payload);
}

/* The tree output's put of v into the open container. */
static void tree_put(struct nbtw *w, const struct nbt_key *k, nbt *v)
{
    struct nbtw_frame *f = &w->st[w->depth - 1];
    if (f->list) nbt_list_add(f->node, v);
    else nbt_put_key(f->node, k, v);
}

void nbtw_byte_x(struct nbtw *w, const struct nbt_key *k, int v)
{
    if (w->bin) *bin_value(w, k, 1, 1) = (uint8_t)v;
    else tree_put(w, k, nbt_new_byte(v));
}

void nbtw_short_x(struct nbtw *w, const struct nbt_key *k, int v)
{
    if (w->bin) be16(bin_value(w, k, 2, 2), (unsigned)(uint16_t)v);
    else tree_put(w, k, nbt_new_short(v));
}

void nbtw_int_x(struct nbtw *w, const struct nbt_key *k, int v)
{
    if (w->bin) be32(bin_value(w, k, 3, 4), (uint32_t)v);
    else tree_put(w, k, nbt_new_int(v));
}

void nbtw_long_x(struct nbtw *w, const struct nbt_key *k, int64_t v)
{
    if (w->bin) be64(bin_value(w, k, 4, 8), (uint64_t)v);
    else tree_put(w, k, nbt_new_long(v));
}

void nbtw_float_x(struct nbtw *w, const struct nbt_key *k, float v)
{
    if (w->bin)
    {
        uint32_t bits;
        memcpy(&bits, &v, sizeof bits);
        be32(bin_value(w, k, 5, 4), bits);
    }
    else tree_put(w, k, nbt_new_float(v));
}

void nbtw_double_x(struct nbtw *w, const struct nbt_key *k, double v)
{
    if (w->bin)
    {
        uint64_t bits;
        memcpy(&bits, &v, sizeof bits);
        be64(bin_value(w, k, 6, 8), bits);
    }
    else tree_put(w, k, nbt_new_double(v));
}

void nbtw_string_k(struct nbtw *w, const struct nbt_key *k, const char *s)
{
    if (w->bin)
    {
        bin_value(w, k, 8, 0);
        put_utf(w->b, s);
    }
    else tree_put(w, k, nbt_new_string(s));
}

void nbtw_string_key_k(struct nbtw *w, const struct nbt_key *k, const struct nbt_key *v)
{
    if (!w->bin) tree_put(w, k, nbt_new_string(v->s));
    else if (v->ascii)
    {
        uint8_t *p = bin_value(w, k, 8, 2 + v->len);
        be16(p, (unsigned)v->len);
        copy16(p + 2, (const uint8_t *)v->s, v->len);
    }
    else
    {
        bin_value(w, k, 8, 0);
        put_utf(w->b, v->s);
    }
}

void nbtw_comp_k(struct nbtw *w, const struct nbt_key *k)
{
    if (w->bin)
    {
        bin_value(w, k, 10, 0);
        w_push(w, NULL, 0, w->b->n, w->nf);
    }
    else
    {
        nbt *c = nbt_new_compound();
        tree_put(w, k, c);
        w_push(w, c, 0, 0, 0);
    }
}

void nbtw_list_k(struct nbtw *w, const struct nbt_key *k)
{
    if (w->bin)
    {
        bin_value(w, k, 9, 0);
        size_t at = w->b->n;
        reserve(w->b, 5);
        w_push(w, NULL, 1, at, w->nf);
    }
    else
    {
        nbt *l = nbt_new_list();
        tree_put(w, k, l);
        w_push(w, l, 1, 0, 0);
    }
}

void nbtw_tree_k(struct nbtw *w, const struct nbt_key *k, const nbt *src, nbt *(*copy)(const nbt *))
{
    if (w->bin)
    {
        bin_value(w, k, tag_id(src->type), 0);
        write_payload(src, w->b);
    }
    else tree_put(w, k, copy(src));
}

/* The order write_compound writes n fields with these keys in: order[i] is
 * the field that goes i-th; 1 when that is not the insertion order. */
static int hashmap_order(const struct nbtw_field *f, int n, int *order)
{
    int cap = 16;

    while (n > cap * 3 / 4) cap *= 2;

    int count_buf[384];
    int *count = cap * 3 <= 384 ? count_buf : malloc((size_t)cap * 3 * sizeof *count);

    if (count == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }

    memset(count, 0, (size_t)cap * 3 * sizeof *count);

    for (int i = 0; i < n; ++i)
    {
        uint32_t h = f[i].key->hash;
        ++count[(int)((h ^ (h >> 16)) & (uint32_t)(cap - 1)) * 3 + bucket_rank(f[i].key)];
    }

    for (int k = 0, sum = 0; k < cap * 3; ++k)
    {
        int c0 = count[k];
        count[k] = sum;
        sum += c0;
    }

    int moved = 0;

    for (int i = 0; i < n; ++i)
    {
        uint32_t h = f[i].key->hash;
        int at = count[(int)((h ^ (h >> 16)) & (uint32_t)(cap - 1)) * 3 + bucket_rank(f[i].key)]++;
        order[at] = i;
        moved |= at != i;
    }

    if (count != count_buf) free(count);
    return moved;
}

/* A closing compound's fields, bytes [f[0].off, end) of the output, moved
 * into the order write_compound writes a tree's in. The order depends on the
 * keys and their insertion order alone, so a sequence of keys seen before
 * takes its order from the cache (checked key by key). */
static void bin_order(nbtbin *b, const struct nbtw_field *f, int n, size_t e)
{
    int order_buf[NBTW_ORDER_MAX];
    int *order = order_buf, moved;
    struct nbtw_order *c = NULL;

    if (n <= NBTW_ORDER_MAX)
    {
        uint64_t sig = (uint64_t)n;
        for (int i = 0; i < n; ++i) sig = (sig ^ (uint64_t)(uintptr_t)f[i].key) * 0x100000001b3ULL;
        sig ^= sig >> 29;

        if (nw_env->nbtbin.worder == NULL)
        {
            nw_env->nbtbin.worder = calloc(NBTW_ORDER_SLOTS, sizeof *nw_env->nbtbin.worder);
            if (nw_env->nbtbin.worder == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
        }

        c = &nw_env->nbtbin.worder[sig & (NBTW_ORDER_SLOTS - 1)];
        int hit = c->sig == sig && c->n == n;
        for (int i = 0; hit && i < n; ++i) hit = c->keys[i] == f[i].key;

        if (hit)
        {
            if (!c->moved) return;
            for (int i = 0; i < n; ++i) order[i] = c->order[i];
            moved = 1;
        }
        else
        {
            moved = hashmap_order(f, n, order);
            c->sig = sig;
            c->n = n;
            c->moved = moved;
            for (int i = 0; i < n; ++i)
            {
                c->keys[i] = f[i].key;
                c->order[i] = (uint8_t)order[i];
            }
        }
    }
    else
    {
        order = malloc((size_t)n * sizeof *order);
        if (order == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
        moved = hashmap_order(f, n, order);
    }

    if (moved)
    {
        size_t s = f[0].off, len = e - s;

        if (len + 16 > nw_env->nbtbin.wtmpcap)
        {
            size_t cap = nw_env->nbtbin.wtmpcap ? nw_env->nbtbin.wtmpcap : 4096;
            while (cap < len + 16) cap *= 2;
            free(nw_env->nbtbin.wtmp);
            nw_env->nbtbin.wtmp = calloc(1, cap);
            if (nw_env->nbtbin.wtmp == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
            nw_env->nbtbin.wtmpcap = cap;
        }

        /* the fields in order into the side buffer, then back as one block */
        uint8_t *tmp = nw_env->nbtbin.wtmp;
        size_t o = 0;
        for (int i = 0; i < n; ++i)
        {
            int j = order[i];
            size_t a = f[j].off, z = j + 1 < n ? f[j + 1].off : e;
            copy16(tmp + o, b->p + a, z - a);
            o += z - a;
        }
        memcpy(b->p + s, tmp, len);
    }

    if (order != order_buf) free(order);
}

void nbtw_end(struct nbtw *w)
{
    struct nbtw_frame *f = &w->st[--w->depth];

    if (!w->bin) return;

    if (f->list)
    {
        uint8_t *p = w->b->p + f->start;
        p[0] = (uint8_t)(f->count > 0 ? f->elem : 0);
        be32(p + 1, (uint32_t)f->count);
        return;
    }

    int n = w->nf - f->fbase;
    if (n > 1)
    {
        /* its fields' records kept for nbtw_bin_order */
        if (w->nc + n > nw_env->nbtbin.wccap)
        {
            int was = nw_env->nbtbin.wccap, cap = was ? was : 1024;
            while (cap < w->nc + n) cap *= 2;
            nw_env->nbtbin.wc = realloc(nw_env->nbtbin.wc, (size_t)cap * sizeof *nw_env->nbtbin.wc);
            if (nw_env->nbtbin.wc == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
            memset(nw_env->nbtbin.wc + was, 0, (size_t)(cap - was) * sizeof *nw_env->nbtbin.wc);
            nw_env->nbtbin.wccap = cap;
        }
        if (w->nd == nw_env->nbtbin.wdcap)
        {
            int was = nw_env->nbtbin.wdcap;
            nw_env->nbtbin.wdcap = was ? 2 * was : 256;
            nw_env->nbtbin.wd = realloc(nw_env->nbtbin.wd, (size_t)nw_env->nbtbin.wdcap * sizeof *nw_env->nbtbin.wd);
            if (nw_env->nbtbin.wd == NULL) { fprintf(stderr, "nbtbin: out of memory\n"); exit(1); }
        }
        memcpy(nw_env->nbtbin.wc + w->nc, w->wf + f->fbase, (size_t)n * sizeof *w->wf);
        nw_env->nbtbin.wd[w->nd].first = w->nc;
        nw_env->nbtbin.wd[w->nd].n = n;
        nw_env->nbtbin.wd[w->nd].end = w->b->n;
        w->nc += n;
        ++w->nd;
    }
    w->nf = f->fbase;
    put_u8(w->b, 0); /* the compound's end tag */
}

void nbtw_bin_order(struct nbtw *w)
{
    /* closed children before parents */
    for (int d = 0; d < w->nd; ++d)
    {
        const struct nbtw_closed *c = &nw_env->nbtbin.wd[d];
        bin_order(w->b, nw_env->nbtbin.wc + c->first, c->n, c->end);
    }
    w->nd = w->nc = 0;
}

void nbtw_bin_end(struct nbtw *w)
{
    /* the root compound, frame 0: a writer closes all it opens */
    if (w->depth != 1) { fprintf(stderr, "nbtw: %d containers left open\n", w->depth - 1); abort(); }
    nbtw_end(w);
}

void nbtw_double3(struct nbtw *w, const struct nbt_key *k, double a, double b, double c)
{
    nbtw_list_k(w, k);
    nbtw_double_k(w, NULL, a);
    nbtw_double_k(w, NULL, b);
    nbtw_double_k(w, NULL, c);
    nbtw_end(w);
}

void nbtw_float2(struct nbtw *w, const struct nbt_key *k, float a, float b)
{
    nbtw_list_k(w, k);
    nbtw_float_k(w, NULL, a);
    nbtw_float_k(w, NULL, b);
    nbtw_end(w);
}
