/* One NBT writer for two outputs: the entity writers (living.c, entity_nbt.c,
 * fallhang.c, endfight.c and the kinds') write through it once, and it either
 * builds the tree (nbtw_tree: the snapshot comparisons, saves, --watch) or
 * writes the binary NBT the entity digest hashes (nbtw_bin_begin: Rows.nbtBytes'
 * bytes, nbtbin.h), straight into a buffer with no tree and no allocation once
 * the buffers have grown.
 *
 * A writer opens a compound or list, writes its fields or elements, and
 * closes it with nbtw_end; a key is given for a compound's field and NULL
 * for a list's element (the nbtw_add_* forms). The binary output writes each
 * field's bytes as they come and, when the compound closes, moves them into
 * the JDK 8 HashMap's iteration order the tree writer (nbtbin_write_root)
 * computes from the insertion order: both outputs of one writer give the same
 * bytes. The tree output puts a container into its parent when it opens it,
 * which is the insertion order of a writer that fills a child and then puts
 * it, as long as nothing else is put into the parent in between (nesting).
 */
#ifndef NETHERITE_NBTW_H
#define NETHERITE_NBTW_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nbtbin.h"
#include "nbtjson.h"
#include "nbtjson_tree.h"

struct nbt_key;

#define NBTW_DEPTH 32

struct nbtw_frame {
    nbt *node;       /* tree: the container */
    size_t start;    /* bin: a list's header byte */
    int fbase;       /* bin: a compound's first field record */
    int list;        /* a list, else a compound */
    int count, elem; /* bin: a list's elements so far and their tag id */
};

struct nbtw {
    int bin;
    nbtbin *b;
    int depth, nf;   /* bin: field records in use */
    struct nbtw_field *wf;   /* bin: nw_env->nbtbin.wf, wfcap of them */
    int wfcap;
    int nc, nd;              /* bin: closed field records and compounds */
    struct nbtw_frame st[NBTW_DEPTH];
};

/* Write into root, a compound (its fields append to what it holds). */
void nbtw_tree(struct nbtw *w, nbt *root);
/* Write a whole entity tag into b from its start: 0x0A, writeUTF(""), then
 * the root compound's payload, which nbtw_bin_end closes. The fields stay in
 * the order they were written until nbtw_bin_order puts every compound's
 * into the HashMap order (children first: a compound's fields move inside
 * its own bytes, so its parent's field bounds hold). */
void nbtw_bin_begin(struct nbtw *w, nbtbin *b);
void nbtw_bin_end(struct nbtw *w);
void nbtw_bin_order(struct nbtw *w);
static inline int nbtw_is_bin(const struct nbtw *w) { return w->bin; }

/* The interned key of a literal (cached at the call site) or of any string. */
const struct nbt_key *nbt_site_key(struct nbt_put_site *site, const char *key);
const struct nbt_key *nbt_key_of(const char *key);
/* the site's cached key, or nbt_site_key's first look up */
static inline const struct nbt_key *nbt_site_key_fast(struct nbt_put_site *site, const char *key)
{
    const void *k = __atomic_load_n(&site->k, __ATOMIC_ACQUIRE);
    if (k != NULL && __atomic_load_n(&site->p, __ATOMIC_RELAXED) == key) return (const struct nbt_key *)k;
    return nbt_site_key(site, key);
}
#if defined(__GNUC__)
#define NBTW_K(key) \
    (__builtin_constant_p(key) && __builtin_constant_p((key)[0]) \
         ? ({ static struct nbt_put_site nbtw_site_; nbt_site_key_fast(&nbtw_site_, (key)); }) \
         : nbt_key_of(key))
#else
#define NBTW_K(key) nbt_key_of(key)
#endif

/* k: the field's key in a compound, NULL for a list's element. The scalars
 * are inline: the binary output's common case (room in the buffers, an ASCII
 * key) is written here, anything else by the _x functions (nbtbin.c), which
 * do the same for both outputs. */
void nbtw_byte_x(struct nbtw *w, const struct nbt_key *k, int v);
void nbtw_short_x(struct nbtw *w, const struct nbt_key *k, int v);
void nbtw_int_x(struct nbtw *w, const struct nbt_key *k, int v);
void nbtw_long_x(struct nbtw *w, const struct nbt_key *k, int64_t v);
void nbtw_float_x(struct nbtw *w, const struct nbt_key *k, float v);
void nbtw_double_x(struct nbtw *w, const struct nbt_key *k, double v);

/* n bytes from s to d in whole 16-byte pieces: up to 15 more are read past
 * s + n and written past d + n (NBTBIN_SLACK, the keys' zero padding). The
 * empty asm keeps the compiler from turning the loop back into a call to
 * memcpy, whose call and length dispatch cost more than these short copies. */
static inline void nbtw_copy16(uint8_t *d, const uint8_t *s, size_t n)
{
    memcpy(d, s, 16);
    for (size_t i = 16; i < n; i += 16)
    {
        __asm__ volatile("" ::: "memory");
        memcpy(d + i, s + i, 16);
    }
}

/* The binary output's bytes for one value (after its field header, which it
 * writes), or NULL when the out-of-line path must take it: bin_value in
 * nbtbin.c, for the case that needs no growth. */
static inline uint8_t *nbtw_fast(struct nbtw *w, const struct nbt_key *k, int tag, size_t payload)
{
    struct nbtw_frame *f = &w->st[w->depth - 1];
    nbtbin *b = w->b;

    if (f->list)
    {
        if (b->n + payload + NBTBIN_SLACK > b->cap) return NULL;
        if (f->count++ == 0) f->elem = tag;
        uint8_t *p = b->p + b->n;
        b->n += payload;
        return p;
    }

    size_t len = k->len;
    if (w->nf == w->wfcap || !k->ascii || b->n + 3 + len + payload + NBTBIN_SLACK > b->cap) return NULL;
    w->wf[w->nf].off = b->n;
    w->wf[w->nf].key = k;
    ++w->nf;

    uint8_t *p = b->p + b->n;
    b->n += 3 + len + payload;
    p[0] = (uint8_t)tag;
    p[1] = (uint8_t)(len >> 8);
    p[2] = (uint8_t)len;
    /* whole 16-byte pieces: the key's text is zero padded (nbtjson.c), the
     * buffer has NBTBIN_SLACK */
    nbtw_copy16(p + 3, (const uint8_t *)k->s, len);
    return p + 3 + len;
}

static inline void nbtw_byte_k(struct nbtw *w, const struct nbt_key *k, int v)
{
    uint8_t *p;
    if (w->bin && (p = nbtw_fast(w, k, 1, 1)) != NULL) *p = (uint8_t)v;
    else nbtw_byte_x(w, k, v);
}

static inline void nbtw_short_k(struct nbtw *w, const struct nbt_key *k, int v)
{
    uint8_t *p;
    if (w->bin && (p = nbtw_fast(w, k, 2, 2)) != NULL)
    {
        p[0] = (uint8_t)((unsigned)v >> 8);
        p[1] = (uint8_t)v;
    }
    else nbtw_short_x(w, k, v);
}

static inline void nbtw_int_k(struct nbtw *w, const struct nbt_key *k, int v)
{
    uint8_t *p;
    if (w->bin && (p = nbtw_fast(w, k, 3, 4)) != NULL)
    {
        uint32_t x = __builtin_bswap32((uint32_t)v);
        memcpy(p, &x, 4);
    }
    else nbtw_int_x(w, k, v);
}

static inline void nbtw_long_k(struct nbtw *w, const struct nbt_key *k, int64_t v)
{
    uint8_t *p;
    if (w->bin && (p = nbtw_fast(w, k, 4, 8)) != NULL)
    {
        uint64_t x = __builtin_bswap64((uint64_t)v);
        memcpy(p, &x, 8);
    }
    else nbtw_long_x(w, k, v);
}

static inline void nbtw_float_k(struct nbtw *w, const struct nbt_key *k, float v)
{
    uint8_t *p;
    if (w->bin && (p = nbtw_fast(w, k, 5, 4)) != NULL)
    {
        uint32_t x;
        memcpy(&x, &v, 4);
        x = __builtin_bswap32(x);
        memcpy(p, &x, 4);
    }
    else nbtw_float_x(w, k, v);
}

static inline void nbtw_double_k(struct nbtw *w, const struct nbt_key *k, double v)
{
    uint8_t *p;
    if (w->bin && (p = nbtw_fast(w, k, 6, 8)) != NULL)
    {
        uint64_t x;
        memcpy(&x, &v, 8);
        x = __builtin_bswap64(x);
        memcpy(p, &x, 8);
    }
    else nbtw_double_x(w, k, v);
}
void nbtw_string_k(struct nbtw *w, const struct nbt_key *k, const char *s);
/* A string the caller holds interned (nbt_key_of, a site's key): its bytes
 * need no scan. */
void nbtw_string_key_k(struct nbtw *w, const struct nbt_key *k, const struct nbt_key *v);
void nbtw_comp_k(struct nbtw *w, const struct nbt_key *k);
void nbtw_list_k(struct nbtw *w, const struct nbt_key *k);
/* An existing tree as the field: the tree output puts copy(src) (the owned
 * copy the caller's old writer put), the binary one writes src itself, which
 * must be what copy returns (the same fields in the same insertion order). */
void nbtw_tree_k(struct nbtw *w, const struct nbt_key *k, const nbt *src, nbt *(*copy)(const nbt *));
/* Close the open compound or list. */
void nbtw_end(struct nbtw *w);

#define nbtw_byte(w, key, v) nbtw_byte_k((w), NBTW_K(key), (v))
#define nbtw_short(w, key, v) nbtw_short_k((w), NBTW_K(key), (v))
#define nbtw_int(w, key, v) nbtw_int_k((w), NBTW_K(key), (v))
#define nbtw_long(w, key, v) nbtw_long_k((w), NBTW_K(key), (v))
#define nbtw_float(w, key, v) nbtw_float_k((w), NBTW_K(key), (v))
#define nbtw_double(w, key, v) nbtw_double_k((w), NBTW_K(key), (v))
#define nbtw_string(w, key, s) nbtw_string_k((w), NBTW_K(key), (s))
#define nbtw_string_key(w, key, v) nbtw_string_key_k((w), NBTW_K(key), (v))
#define nbtw_comp(w, key) nbtw_comp_k((w), NBTW_K(key))
#define nbtw_list(w, key) nbtw_list_k((w), NBTW_K(key))
#define nbtw_put_tree(w, key, src, copy) nbtw_tree_k((w), NBTW_K(key), (src), (copy))

#define nbtw_add_byte(w, v) nbtw_byte_k((w), NULL, (v))
#define nbtw_add_float(w, v) nbtw_float_k((w), NULL, (v))
#define nbtw_add_double(w, v) nbtw_double_k((w), NULL, (v))
#define nbtw_add_comp(w) nbtw_comp_k((w), NULL)
#define nbtw_add_list(w) nbtw_list_k((w), NULL)

/* A list of three doubles (Pos, Motion) and of two floats (Rotation). */
void nbtw_double3(struct nbtw *w, const struct nbt_key *k, double a, double b, double c);
void nbtw_float2(struct nbtw *w, const struct nbt_key *k, float a, float b);

/* Rows.nbtLong of the entity tag w just wrote (nbtw_bin_begin on
 * nw_env->nbtbin.memo_scratch, nbtw_bin_end), through the memo slot of key
 * (the same value and slots as nbtbin_long_memo): the bytes as written equal
 * to the slot's last are the same tag (the order is a function of them) and
 * return its value without ordering or hashing; else they are ordered and
 * hashed as nbtbin_long_memo's bytes are. */
uint64_t nbtw_bin_long_memo(struct nbtw *w, int key);

#endif
