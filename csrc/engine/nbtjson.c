#include "env.h"
#include "nbtjson.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbtjson_tree.h"
#include "grave.h"

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "nbtjson: out of memory\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "nbtjson: out of memory\n"); exit(1); }
    return q;
}

/* The trees' own memory. The entity digest builds and frees a tree per
 * entity per row, and malloc was most of its cost, so a tree's nodes, keys,
 * strings and arrays come from size classes of 16 << k bytes, each block led
 * by eight bytes holding k; a freed block goes on its class's list for the
 * next tree. Blocks over POOL_MAX bytes are malloc's (k = POOL_BIG). One
 * thread builds trees (the renderer's workers never touch them). */
#define POOL_CLASSES NBT_POOL_CLASSES
#define POOL_MAX ((size_t)16 << (POOL_CLASSES - 1))
#define POOL_BIG 0xffu
#define POOL_SLAB ((size_t)256 << 10)

#define pool_free_list (nw_env->nbtjson.pool_free_list)
#define pool_slab (nw_env->nbtjson.pool_slab)
#define pool_left (nw_env->nbtjson.pool_left)

#define scratch_on (nw_env->nbtjson.scratch_on)
#define scratch_outside (nw_env->nbtjson.scratch_outside)

static void *pool_alloc_normal(size_t n)
{
    size_t need = n + 8;

    /* an ordinary block made during a scratch build: its bytes are not the
     * scratch's, so the build is not a function of the scratch bytes */
    if (scratch_on) scratch_outside = 1;

    if (need > POOL_MAX)
    {
        uint64_t *b = xmalloc(need);
        b[0] = POOL_BIG;
        return b + 1;
    }

    unsigned k = need <= 16 ? 0 : 60u - (unsigned)__builtin_clzll((unsigned long long)(need - 1));
    uint64_t *b = pool_free_list[k];

    if (b != NULL)
        pool_free_list[k] = (uint64_t *)b[1];
    else
    {
        size_t size = (size_t)16 << k;

        if (pool_left < size)
        {
            pool_slab = xmalloc(POOL_SLAB);
            pool_left = POOL_SLAB;
        }

        b = (uint64_t *)pool_slab;
        pool_slab += size;
        pool_left -= size;
    }

    b[0] = k;
    return b + 1;
}

/* The scratch region (nbt_scratch_begin): blocks cut in order from 1 MB
 * slabs, each led by POOL_SCRATCH in the low byte and its size above it;
 * releasing one does nothing and nbt_scratch_end rewinds the lot. A block too
 * big for a slab is malloc'd and listed, and freed at the end. Every build
 * starts at the first slab's first byte on zeroed memory, so one that stays
 * in that slab is a function of the calls that made it (nbt_scratch_bytes). */
#define POOL_SCRATCH 0xfeu
#define SCRATCH_SLAB ((size_t)1 << 20)

/* scratch_on: between begin and end; scratch_outside: this build has a
 * block outside the first slab (declared above pool_alloc_normal) */
#define scratch_slabs (nw_env->nbtjson.scratch_slabs)
#define scratch_nslabs (nw_env->nbtjson.scratch_nslabs)
#define scratch_cur (nw_env->nbtjson.scratch_cur)
#define scratch_used (nw_env->nbtjson.scratch_used)
/* the first slab's bytes not known to be zero */
#define scratch_dirty (nw_env->nbtjson.scratch_dirty)
#define scratch_big (nw_env->nbtjson.scratch_big)
#define scratch_nbig (nw_env->nbtjson.scratch_nbig)
#define scratch_capbig (nw_env->nbtjson.scratch_capbig)

/* The block when the current slab lacks room: the next slab, or a listed
 * malloc for one too big for any. */
__attribute__((noinline)) static void *scratch_alloc_slow(size_t n, size_t need)
{
    uint64_t *b;

    scratch_outside = 1;

    if (need > SCRATCH_SLAB)
    {
        if (scratch_nbig == scratch_capbig)
        {
            scratch_capbig = scratch_capbig ? 2 * scratch_capbig : 16;
            scratch_big = xrealloc(scratch_big, (size_t)scratch_capbig * sizeof *scratch_big);
        }

        b = xmalloc(n + 8);
        scratch_big[scratch_nbig++] = b;
    }
    else
    {
        if (scratch_cur == 0) scratch_dirty = SCRATCH_SLAB;

        if (++scratch_cur == scratch_nslabs)
        {
            scratch_slabs = xrealloc(scratch_slabs, (size_t)(scratch_nslabs + 1) * sizeof *scratch_slabs);
            scratch_slabs[scratch_nslabs++] = xmalloc(SCRATCH_SLAB);
        }

        b = (uint64_t *)scratch_slabs[scratch_cur];
        scratch_used = need;
    }

    b[0] = POOL_SCRATCH | (uint64_t)n << 8;
    return b + 1;
}

static inline void *scratch_alloc(size_t n)
{
    size_t need = (n + 8 + 15) & ~(size_t)15;

    if (scratch_cur < 0 || scratch_used + need > SCRATCH_SLAB) return scratch_alloc_slow(n, need);

    uint64_t *b = (uint64_t *)(scratch_slabs[scratch_cur] + scratch_used);

    scratch_used += need;
    b[0] = POOL_SCRATCH | (uint64_t)n << 8;
    return b + 1;
}

static inline void *pool_alloc(size_t n)
{
    return scratch_on ? scratch_alloc(n) : pool_alloc_normal(n);
}

void nbt_scratch_begin(void)
{
    if (scratch_nslabs == 0)
    {
        scratch_slabs = xmalloc(sizeof *scratch_slabs);
        scratch_slabs[0] = calloc(1, SCRATCH_SLAB);
        if (scratch_slabs[0] == NULL) { fprintf(stderr, "nbtjson: out of memory\n"); exit(1); }
        scratch_nslabs = 1;
    }

    memset(scratch_slabs[0], 0, scratch_dirty);
    scratch_dirty = 0;
    scratch_cur = 0;
    scratch_used = 0;
    scratch_outside = 0;
    scratch_on = 1;
}

const void *nbt_scratch_bytes(size_t *n)
{
    if (!scratch_on || scratch_outside || scratch_cur != 0) return NULL;

    *n = scratch_used;
    return scratch_slabs[0];
}

void nbt_scratch_end(void)
{
    for (int i = 0; i < scratch_nbig; ++i) free(scratch_big[i]);

    if (scratch_cur == 0 && scratch_used > scratch_dirty) scratch_dirty = scratch_used;

    scratch_nbig = 0;
    scratch_cur = -1;
    scratch_used = 0;
    scratch_on = 0;
}

static void pool_release(void *p)
{
    if (p == NULL) return;

    uint64_t *b = (uint64_t *)p - 1;

    if ((b[0] & 0xff) == POOL_SCRATCH) return;
    if (b[0] == POOL_BIG) { free(b); return; }

    b[1] = (uint64_t)pool_free_list[b[0]];
    pool_free_list[b[0]] = b;
}

static void *pool_realloc(void *p, size_t n)
{
    if (p == NULL) return pool_alloc(n);

    uint64_t *b = (uint64_t *)p - 1;

    if ((b[0] & 0xff) == POOL_SCRATCH)
    {
        size_t old = (size_t)(b[0] >> 8);

        if (n <= old) return p;

        void *q = pool_alloc(n);
        memcpy(q, p, old);
        return q;
    }

    /* an ordinary block stays ordinary, scratch or not: a tree that outlives
     * the scratch never moves into it */
    if (b[0] == POOL_BIG)
    {
        if (n + 8 > POOL_MAX)
        {
            b = xrealloc(b, n + 8);
            return b + 1;
        }
    }
    else if (n + 8 <= (size_t)16 << b[0])
        return p;

    size_t old = b[0] == POOL_BIG ? n : ((size_t)16 << b[0]) - 8;
    void *q = pool_alloc_normal(n);

    memcpy(q, p, old < n ? old : n);
    pool_release(p);
    return q;
}

static char *pool_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = pool_alloc(n);
    memcpy(d, s, n);
    return d;
}

static nbt *value_new(nbt_type t)
{
    nbt *v = pool_alloc(sizeof *v);

    /* a scratch block is fresh zeroed memory already */
    if (!scratch_on) memset(v, 0, sizeof *v);
    v->type = t;
    return v;
}

nbt *nbt_new_byte(int v) { nbt *n = value_new(NBT_BYTE); n->u.i = v; return n; }
nbt *nbt_new_short(int v) { nbt *n = value_new(NBT_SHORT); n->u.i = v; return n; }
nbt *nbt_new_int(int v) { nbt *n = value_new(NBT_INT); n->u.i = v; return n; }
nbt *nbt_new_long(int64_t v) { nbt *n = value_new(NBT_LONG); n->u.i = v; return n; }
nbt *nbt_new_float(float v) { nbt *n = value_new(NBT_FLOAT); n->u.f = v; return n; }
nbt *nbt_new_double(double v) { nbt *n = value_new(NBT_DOUBLE); n->u.d = v; return n; }
nbt *nbt_new_list(void) { return value_new(NBT_LIST); }
nbt *nbt_new_compound(void) { return value_new(NBT_COMPOUND); }

nbt *nbt_new_byte_array(const signed char *v, int n)
{
    nbt *b = value_new(NBT_BYTE_ARRAY);
    b->u.bytes.v = pool_alloc((size_t)n);
    memcpy(b->u.bytes.v, v, (size_t)n);
    b->u.bytes.n = n;
    return b;
}

nbt *nbt_new_int_array(const int *v, int n)
{
    nbt *b = value_new(NBT_INT_ARRAY);
    b->u.ints.v = pool_alloc((size_t)n * sizeof *b->u.ints.v);
    memcpy(b->u.ints.v, v, (size_t)n * sizeof *b->u.ints.v);
    b->u.ints.n = n;
    return b;
}

void nbt_list_add(nbt *list, nbt *v)
{
    if (list->u.list.n == list->u.list.cap)
    {
        list->u.list.cap = list->u.list.cap ? 2 * list->u.list.cap : 4;
        list->u.list.v = pool_realloc(list->u.list.v, (size_t)list->u.list.cap * sizeof *list->u.list.v);
    }
    list->u.list.v[list->u.list.n++] = v;
}

/* The interned strings: an open-addressed table by content, and in front of
 * it a cache by the caller's pointer (a key is nearly always the same
 * literal), checked against the content before it is trusted. An entry lives
 * as long as the process. */
static const struct nbt_key **key_table;
static size_t key_cap, key_count;
static struct { const char *p; const struct nbt_key *k; } key_cache[4096];

static size_t key_slot(uint32_t hash, size_t len, size_t cap)
{
    return (size_t)(hash ^ (uint32_t)len * 0x9e3779b9u) & (cap - 1);
}

/* The interned entry for s. A string value is interned the same way while it
 * is short and the table small (the names an entity writes every row: an
 * attribute's Name, a painting's Motive); past that, bounded, it is NULL and
 * the caller copies. */
#define SHARED_MAX_LEN 64
#define SHARED_MAX_COUNT 16384

/* The table is the process's, and environments tick on several threads at
 * once (csrc/runtime's pool): a miss takes the lock. A cache hit reads its
 * entry without it: the key is loaded first and trusted only when its text is
 * s (an entry is never freed, so any key read is a whole one). */
static pthread_mutex_t key_lock = PTHREAD_MUTEX_INITIALIZER;

static const struct nbt_key *intern_locked(const char *s, int value, size_t ci);

static const struct nbt_key *intern(const char *s, int value)
{
    size_t ci = (size_t)(((uint64_t)(uintptr_t)s * 0x9e3779b97f4a7c15ull) >> 52);
    const struct nbt_key *hit = __atomic_load_n(&key_cache[ci].k, __ATOMIC_ACQUIRE);

    if (hit != NULL && __atomic_load_n(&key_cache[ci].p, __ATOMIC_RELAXED) == s && strcmp(hit->s, s) == 0) return hit;
    pthread_mutex_lock(&key_lock);
    const struct nbt_key *k = intern_locked(s, value, ci);
    pthread_mutex_unlock(&key_lock);
    return k;
}

static const struct nbt_key *intern_locked(const char *s, int value, size_t ci)
{
    if (key_cap == 0) grave_ignore(key_cache, sizeof key_cache);

    size_t len = strlen(s);

    if (value && len > SHARED_MAX_LEN) return NULL;

    uint32_t h = 0;
    int ascii = len <= 0xffff;

    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)s[i];
        h = h * 31 + c;
        if (c >= 0x80) ascii = 0;
    }

    if (2 * (key_count + 1) > key_cap)
    {
        size_t cap = key_cap ? 2 * key_cap : 1024;
        /* the keys are the process's, shared by every environment */
        const struct nbt_key **t = proc_calloc(cap, sizeof *t);

        if (t == NULL) { fprintf(stderr, "nbtjson: out of memory\n"); exit(1); }

        for (size_t i = 0; i < key_cap; ++i)
        {
            if (key_table[i] == NULL) continue;
            size_t j = key_slot(key_table[i]->hash, key_table[i]->len, cap);
            while (t[j] != NULL) j = (j + 1) & (cap - 1);
            t[j] = key_table[i];
        }

        proc_free(key_table);
        key_table = t;
        key_cap = cap;
    }

    size_t j = key_slot(h, len, key_cap);
    const struct nbt_key *k;

    while ((k = key_table[j]) != NULL)
    {
        if (k->hash == h && k->len == len && memcmp(k->s, s, len) == 0) break;
        j = (j + 1) & (key_cap - 1);
    }

    if (k == NULL && value && key_count >= SHARED_MAX_COUNT) return NULL;

    if (k == NULL)
    {
        /* the text is followed by zeros to a 16-byte multiple past its end:
         * nbtw's binary output copies a key in whole 16-byte pieces */
        size_t room = (len + 1 + 15) / 16 * 16 + 16;
        struct nbt_key *nk = proc_malloc(sizeof *nk + room);
        if (nk == NULL) { fprintf(stderr, "nbtjson: out of memory\n"); exit(1); }
        char *text = (char *)(nk + 1);

        memset(text, 0, room);
        memcpy(text, s, len + 1);
        nk->s = text;
        nk->len = len;
        nk->hash = h;
        nk->ascii = ascii;
        key_table[j] = nk;
        ++key_count;
        k = nk;
    }

    __atomic_store_n(&key_cache[ci].k, k, __ATOMIC_RELEASE);
    __atomic_store_n(&key_cache[ci].p, s, __ATOMIC_RELEASE);
    return k;
}

nbt *nbt_new_string(const char *s)
{
    nbt *n = value_new(NBT_STRING);
    const struct nbt_key *k = intern(s, 1);

    if (k != NULL)
    {
        n->u.s = (char *)k->s;
        n->shared = 1;
    }
    else
        n->u.s = pool_strdup(s);

    return n;
}

static void put_key(nbt *comp, const struct nbt_key *key, nbt *v)
{
    if (comp->u.comp.n == comp->u.comp.cap)
    {
        comp->u.comp.cap = comp->u.comp.cap ? 2 * comp->u.comp.cap : 8;
        comp->u.comp.f = pool_realloc(comp->u.comp.f, (size_t)comp->u.comp.cap * sizeof *comp->u.comp.f);
    }

    comp->u.comp.f[comp->u.comp.n].key = key;
    comp->u.comp.f[comp->u.comp.n].val = v;
    ++comp->u.comp.n;
}

void (nbt_put)(nbt *comp, const char *key, nbt *v)
{
    put_key(comp, intern(key, 0), v);
}

const struct nbt_key *nbt_site_key(struct nbt_put_site *site, const char *key)
{
    /* a site's key is one literal: any key it holds is that literal's (the
     * pool's threads may fill it at once, with the same key) */
    const void *k = __atomic_load_n(&site->k, __ATOMIC_ACQUIRE);
    if (k == NULL || __atomic_load_n(&site->p, __ATOMIC_RELAXED) != key)
    {
        k = intern(key, 0);
        __atomic_store_n(&site->k, k, __ATOMIC_RELEASE);
        __atomic_store_n(&site->p, key, __ATOMIC_RELEASE);
    }
    return k;
}

const struct nbt_key *nbt_key_of(const char *key)
{
    return intern(key, 0);
}

void nbt_put_key(nbt *comp, const struct nbt_key *key, nbt *v)
{
    put_key(comp, key, v);
}

void nbt_put_site(nbt *comp, struct nbt_put_site *site, const char *key, nbt *v)
{
    put_key(comp, nbt_site_key(site, key), v);
}

void nbt_list_set(nbt *list, int i, nbt *v) { list->u.list.v[i] = v; }
void nbt_comp_set(nbt *comp, const char *key, nbt *v)
{
    for (int i = 0; i < comp->u.comp.n; ++i)
        if (!strcmp(comp->u.comp.f[i].key->s, key))
        {
            nbt_free(comp->u.comp.f[i].val);
            comp->u.comp.f[i].val = v;
            return;
        }
}

int nbt_replace(nbt *comp, const char *key, nbt *v)
{
    if (comp == NULL || comp->type != NBT_COMPOUND) return 0;
    for (int i = 0; i < comp->u.comp.n; ++i)
        if (strcmp(comp->u.comp.f[i].key->s, key) == 0)
        {
            nbt_free(comp->u.comp.f[i].val);
            comp->u.comp.f[i].val = v;
            return 1;
        }
    return 0;
}

nbt_type nbt_kind(const nbt *v) { return v->type; }
int nbt_list_size(const nbt *list) { return list->u.list.n; }
const nbt *nbt_list_get(const nbt *list, int i) { return list->u.list.v[i]; }
long nbt_int_value(const nbt *v)
{
    if (v == NULL) return 0;

    switch (v->type)
    {
        case NBT_BYTE:
        case NBT_SHORT:
        case NBT_INT:
        case NBT_LONG:
            return (long)v->u.i;
        default:
            return 0;
    }
}

const char *nbt_string_value(const nbt *v) { return v->u.s; }

int nbt_field_count(const nbt *comp) { return comp->type == NBT_COMPOUND ? comp->u.comp.n : 0; }
const char *nbt_field_key(const nbt *comp, int i) { return comp->u.comp.f[i].key->s; }
const nbt *nbt_field_value(const nbt *comp, int i) { return comp->u.comp.f[i].val; }

uint32_t nbt_float_bits(const nbt *v)
{
    if (v->type != NBT_FLOAT) return 0;
    uint32_t bits;
    memcpy(&bits, &v->u.f, sizeof bits);
    return bits;
}

uint64_t nbt_double_bits(const nbt *v)
{
    if (v->type != NBT_DOUBLE) return 0;
    uint64_t bits;
    memcpy(&bits, &v->u.d, sizeof bits);
    return bits;
}

const signed char *nbt_byte_array(const nbt *v, int *n)
{
    if (v->type != NBT_BYTE_ARRAY) { if (n) *n = 0; return NULL; }
    if (n) *n = v->u.bytes.n;
    return v->u.bytes.v;
}

const int *nbt_int_array(const nbt *v, int *n)
{
    if (v->type != NBT_INT_ARRAY) { if (n) *n = 0; return NULL; }
    if (n) *n = v->u.ints.n;
    return v->u.ints.v;
}

const nbt *nbt_get(const nbt *comp, const char *key)
{
    if (comp == NULL || comp->type != NBT_COMPOUND) return NULL;
    for (int i = 0; i < comp->u.comp.n; ++i)
        if (!strcmp(comp->u.comp.f[i].key->s, key)) return comp->u.comp.f[i].val;
    return NULL;
}

nbt *nbt_take(nbt *comp, const char *key)
{
    if (comp == NULL || comp->type != NBT_COMPOUND) return NULL;
    for (int i = 0; i < comp->u.comp.n; ++i)
        if (!strcmp(comp->u.comp.f[i].key->s, key))
        {
            nbt *v = comp->u.comp.f[i].val;
            memmove(&comp->u.comp.f[i], &comp->u.comp.f[i + 1], (size_t)(comp->u.comp.n - i - 1) * sizeof *comp->u.comp.f);
            --comp->u.comp.n;
            return v;
        }
    return NULL;
}

/* How deep the tree walks below go (a list or compound per level); vanilla's
 * NBT reader refuses deeper than 512, and no tag this engine builds or reads
 * nests past about ten. */
#define NBT_WALK_DEPTH 64

static void walk_too_deep(const char *what)
{
    fprintf(stderr, "nbtjson: %s nests deeper than %d\n", what, NBT_WALK_DEPTH);
    abort();
}

static int is_container(const nbt *v)
{
    return v->type == NBT_LIST || v->type == NBT_COMPOUND;
}

/* A scalar node's own memory. */
static inline void release_scalar(nbt *v)
{
    switch (v->type)
    {
        case NBT_STRING: if (!v->shared) pool_release(v->u.s); break;
        case NBT_BYTE_ARRAY: pool_release(v->u.bytes.v); break;
        case NBT_INT_ARRAY: pool_release(v->u.ints.v); break;
        default: break;
    }
    pool_release(v);
}

/* Post-order: a container's children in order, then its array, then itself,
 * on an explicit stack (the order the recursive walk released them). */
void nbt_free(nbt *v)
{
    if (!v) return;
    if (!is_container(v)) { release_scalar(v); return; }

    struct { nbt *v; int i; } st[NBT_WALK_DEPTH];
    int sp = 0;

    st[0].v = v;
    st[0].i = 0;

    while (sp >= 0)
    {
        nbt *c = st[sp].v;
        int n = c->type == NBT_LIST ? c->u.list.n : c->u.comp.n;

        if (st[sp].i < n)
        {
            nbt *ch = c->type == NBT_LIST ? c->u.list.v[st[sp].i] : c->u.comp.f[st[sp].i].val;

            ++st[sp].i;

            if (!is_container(ch)) release_scalar(ch);
            else
            {
                if (sp + 1 >= NBT_WALK_DEPTH) walk_too_deep("a freed tag");
                ++sp;
                WL_DEPTH_NOTE(nbt, sp + 1);
                st[sp].v = ch;
                st[sp].i = 0;
            }

            continue;
        }

        if (c->type == NBT_LIST) pool_release(c->u.list.v);
        else pool_release(c->u.comp.f);

        pool_release(c);
        --sp;
    }
}

/* Canonical key order is Java String.compareTo; every key in these trees is
 * ASCII, where that is plain byte order. */
static int *key_order(const nbt *c)
{
    int *idx = xmalloc((size_t)c->u.comp.n * sizeof *idx);   /* caller frees */
    int n = 0;
    for (int i = 0; i < c->u.comp.n; ++i)
    {
        int j = n++;
        while (j > 0 && strcmp(c->u.comp.f[idx[j - 1]].key->s, c->u.comp.f[i].key->s) > 0)
        {
            idx[j] = idx[j - 1];
            --j;
        }
        idx[j] = i;
    }
    return idx;
}

struct sbuf { char *p; size_t n, cap; };

static void sb_need(struct sbuf *b, size_t extra)
{
    if (b->n + extra + 1 > b->cap)
    {
        while (b->n + extra + 1 > b->cap) b->cap = b->cap ? 2 * b->cap : 64;
        b->p = xrealloc(b->p, b->cap);
    }
}

/* The appends leave room for the terminating NUL (sb_need's + 1) and
 * nbt_render writes it once at the end. */
static void sb_ch(struct sbuf *b, char c) { sb_need(b, 1); b->p[b->n++] = c; }

static void sb_str(struct sbuf *b, const char *s)
{
    size_t n = strlen(s);
    sb_need(b, n);
    memcpy(b->p + b->n, s, n);
    b->n += n;
}

/* %lld, written directly (the renderer's hot path: snprintf was most of it) */
static void sb_dec(struct sbuf *b, long long v)
{
    char tmp[24];
    int n = 0;
    unsigned long long u = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;

    do { tmp[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
    sb_need(b, (size_t)n + 1);
    if (v < 0) b->p[b->n++] = '-';
    while (n) b->p[b->n++] = tmp[--n];
}

/* %0<digits>llx, lowercase */
static void sb_hex(struct sbuf *b, unsigned long long v, int digits)
{
    sb_need(b, (size_t)digits);
    for (int i = digits - 1; i >= 0; --i) { b->p[b->n + (size_t)i] = "0123456789abcdef"[v & 15]; v >>= 4; }
    b->n += (size_t)digits;
}

/* "<tag>:<decimal>" */
static void sb_tagged(struct sbuf *b, const char *tag, long long v)
{
    sb_str(b, tag);
    sb_dec(b, v);
    sb_ch(b, '"');
}

static void render_scalar(struct sbuf *b, const nbt *v)
{
    switch (v->type)
    {
        case NBT_BYTE: sb_tagged(b, "\"b:", (long long)v->u.i); break;
        case NBT_SHORT: sb_tagged(b, "\"s:", (long long)v->u.i); break;
        case NBT_INT: sb_tagged(b, "\"i:", (long long)v->u.i); break;
        case NBT_LONG: sb_tagged(b, "\"l:", (long long)v->u.i); break;
        case NBT_FLOAT:
        {
            uint32_t bits;
            float f = v->u.f;
            memcpy(&bits, &f, sizeof bits);
            sb_str(b, "\"f:");
            sb_hex(b, bits, 8);
            sb_ch(b, '"');
            break;
        }
        case NBT_DOUBLE:
        {
            uint64_t bits;
            double d = v->u.d;
            memcpy(&bits, &d, sizeof bits);
            sb_str(b, "\"d:");
            sb_hex(b, bits, 16);
            sb_ch(b, '"');
            break;
        }
        case NBT_STRING:
            sb_str(b, "\"str:");
            sb_str(b, v->u.s);
            sb_ch(b, '"');
            break;
        case NBT_BYTE_ARRAY:
            sb_str(b, "\"ba:");
            for (int i = 0; i < v->u.bytes.n; ++i)
            {
                if (i) sb_ch(b, ',');
                sb_dec(b, v->u.bytes.v[i]);
            }
            sb_ch(b, '"');
            break;
        case NBT_INT_ARRAY:
            sb_str(b, "\"ia:");
            for (int i = 0; i < v->u.ints.n; ++i)
            {
                if (i) sb_ch(b, ',');
                sb_dec(b, v->u.ints.v[i]);
            }
            sb_ch(b, '"');
            break;
        default: break;
    }
}

/* Depth-first, a list's elements in order and a compound's fields in key
 * order, on an explicit stack of the open containers. */
static void render_value(struct sbuf *b, const nbt *v)
{
    struct { const nbt *v; int i; int *o; } st[NBT_WALK_DEPTH];
    int sp = -1;

    for (;;)
    {
        /* open v */
        if (is_container(v))
        {
            if (sp + 1 >= NBT_WALK_DEPTH) walk_too_deep("a rendered tag");
            ++sp;
            WL_DEPTH_NOTE(nbt, sp + 1);
            st[sp].v = v;
            st[sp].i = 0;
            st[sp].o = v->type == NBT_COMPOUND ? key_order(v) : NULL;
            sb_ch(b, v->type == NBT_LIST ? '[' : '{');
        }
        else render_scalar(b, v);

        /* the next value to open, closing the containers that are done */
        v = NULL;

        while (sp >= 0)
        {
            const nbt *c = st[sp].v;
            int i = st[sp].i;

            if (c->type == NBT_LIST ? i < c->u.list.n : i < c->u.comp.n)
            {
                ++st[sp].i;
                if (i) sb_ch(b, ',');

                if (c->type == NBT_LIST) v = c->u.list.v[i];
                else
                {
                    sb_ch(b, '"');
                    sb_str(b, c->u.comp.f[st[sp].o[i]].key->s);
                    sb_str(b, "\":");
                    v = c->u.comp.f[st[sp].o[i]].val;
                }

                break;
            }

            sb_ch(b, c->type == NBT_LIST ? ']' : '}');
            free(st[sp].o);
            --sp;
        }

        if (v == NULL) return;
    }
}

char *nbt_render(const nbt *v)
{
    struct sbuf b = {0};
    sb_need(&b, 1);
    b.p[0] = 0;
    render_value(&b, v);
    b.p[b.n] = 0;
    return b.p;
}

/* nbt_parse(nbt_render(v)) without the text: the same tree (a compound's
 * fields in key order, every scalar and array as it was), walked on an
 * explicit stack as render_value walks it. NULL where the round trip would
 * not give the tree back: a string holding a quote ends early in the text. */
static nbt *copy_scalar(const nbt *v)
{
    switch (v->type)
    {
        case NBT_BYTE: return nbt_new_byte((int)v->u.i);
        case NBT_SHORT: return nbt_new_short((int)v->u.i);
        case NBT_INT: return nbt_new_int((int)v->u.i);
        case NBT_LONG: return nbt_new_long(v->u.i);
        case NBT_FLOAT: return nbt_new_float(v->u.f);
        case NBT_DOUBLE: return nbt_new_double(v->u.d);
        case NBT_STRING: return strchr(v->u.s, '"') ? NULL : nbt_new_string(v->u.s);
        case NBT_BYTE_ARRAY: return nbt_new_byte_array(v->u.bytes.v, v->u.bytes.n);
        case NBT_INT_ARRAY: return nbt_new_int_array(v->u.ints.v, v->u.ints.n);
        default: return NULL;
    }
}

nbt *nbt_copy_canonical(const nbt *v)
{
    struct { const nbt *v; int i; int *o; nbt *d; } st[NBT_WALK_DEPTH];
    int sp = -1;
    nbt *root = NULL;

    if (!is_container(v)) return copy_scalar(v);
    for (;;)
    {
        /* open v, a container: its copy joins its parent's */
        nbt *d = v->type == NBT_LIST ? nbt_new_list() : nbt_new_compound();
        if (sp < 0) root = d;
        else if (st[sp].v->type == NBT_LIST) nbt_list_add(st[sp].d, d);
        else put_key(st[sp].d, st[sp].v->u.comp.f[st[sp].o[st[sp].i - 1]].key, d);
        if (sp + 1 >= NBT_WALK_DEPTH) walk_too_deep("a copied tag");
        ++sp;
        st[sp].v = v;
        st[sp].i = 0;
        st[sp].o = v->type == NBT_COMPOUND ? key_order(v) : NULL;
        st[sp].d = d;

        /* the scalars up to the next container, closing those that are done */
        v = NULL;
        while (sp >= 0)
        {
            const nbt *c = st[sp].v;
            int i = st[sp].i;
            if (c->type == NBT_LIST ? i < c->u.list.n : i < c->u.comp.n)
            {
                ++st[sp].i;
                const nbt *x = c->type == NBT_LIST ? c->u.list.v[i] : c->u.comp.f[st[sp].o[i]].val;
                if (is_container(x))
                {
                    v = x;
                    break;
                }
                nbt *y = copy_scalar(x);
                if (y == NULL)
                {
                    for (; sp >= 0; --sp) free(st[sp].o);
                    nbt_free(root);
                    return NULL;
                }
                if (c->type == NBT_LIST) nbt_list_add(st[sp].d, y);
                else put_key(st[sp].d, c->u.comp.f[st[sp].o[i]].key, y);
                continue;
            }
            free(st[sp].o);
            --sp;
        }
        if (v == NULL) return root;
    }
}

/* ---- parsing the same form back ---- */

struct parse { const char *p; };

/* Hex digits, at most count, at least one. */
static int hex_digits(struct parse *s, int count, uint64_t *out)
{
    uint64_t v = 0;
    int n = 0;
    while (n < count)
    {
        char c = s->p[n];
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (d < 0) break;
        v = (v << 4) | (uint64_t)d;
        ++n;
    }
    if (n == 0) return 0;
    s->p += n;
    *out = v;
    return 1;
}

/* Signed decimal, at least one digit; consumes it. */
static int dec_digits(struct parse *s, int64_t *out)
{
    const char *q = s->p;
    int neg = 0;
    if (*q == '-') { neg = 1; ++q; }
    if (*q < '0' || *q > '9') return 0;
    int64_t v = 0;
    while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); ++q; }
    s->p = q;
    *out = neg ? -v : v;
    return 1;
}

static nbt *parse_scalar(struct parse *s)
{
    /* s->p is just past the opening quote */
    const char *q = s->p;
    if (!strncmp(q, "str:", 4))
    {
        const char *end = strchr(q + 4, '"');
        if (!end) return NULL;
        char *text = xmalloc((size_t)(end - q - 4) + 1);
        memcpy(text, q + 4, (size_t)(end - q - 4));
        text[end - q - 4] = 0;
        nbt *v = nbt_new_string(text);
        free(text);
        s->p = end + 1;
        return v;
    }
    struct parse inner = {q};
    if (!strncmp(q, "ia:", 3))
    {
        inner.p = q + 3;
        int *vals = NULL, n = 0, cap = 0;
        while (*inner.p != '"')
        {
            int64_t d;
            if (n && *inner.p == ',') ++inner.p;
            if (!dec_digits(&inner, &d)) { free(vals); return NULL; }
            if (n == cap) { cap = cap ? 2 * cap : 4; vals = xrealloc(vals, (size_t)cap * sizeof *vals); }
            vals[n++] = (int)d;
        }
        nbt *v = nbt_new_int_array(vals, n);
        free(vals);
        s->p = inner.p + 1;
        return v;
    }
    if (!strncmp(q, "ba:", 3))
    {
        inner.p = q + 3;
        signed char *vals = NULL;
        int n = 0, cap = 0;
        while (*inner.p != '"')
        {
            int64_t d;
            if (n && *inner.p == ',') ++inner.p;
            if (!dec_digits(&inner, &d)) { free(vals); return NULL; }
            if (n == cap) { cap = cap ? 2 * cap : 4; vals = xrealloc(vals, (size_t)cap * sizeof *vals); }
            vals[n++] = (signed char)d;
        }
        nbt *v = nbt_new_byte_array(vals, n);
        free(vals);
        s->p = inner.p + 1;
        return v;
    }
    const char *end = strchr(q, '"');
    if (!end) return NULL;
    size_t len = (size_t)(end - q);
    nbt *v = NULL;
    if (len >= 2 && q[1] == ':')
    {
        switch (q[0])
        {
            case 'b': case 's': case 'i': case 'l':
            {
                struct parse d = {q + 2};
                int64_t x;
                if (!dec_digits(&d, &x) || d.p != end) break;
                v = q[0] == 'b' ? nbt_new_byte((int)x) : q[0] == 's' ? nbt_new_short((int)x)
                    : q[0] == 'i' ? nbt_new_int((int)x) : nbt_new_long(x);
                break;
            }
            case 'f':
            {
                struct parse d = {q + 2};
                uint64_t bits;
                if (!hex_digits(&d, 8, &bits) || d.p != end || end - (q + 2) != 8) break;
                uint32_t b32 = (uint32_t)bits;
                float f;
                memcpy(&f, &b32, sizeof f);
                v = nbt_new_float(f);
                break;
            }
            case 'd':
            {
                uint64_t bits = 0;
                if (end - (q + 2) != 16) break;
                for (int i = 0; i < 16; ++i)
                {
                    char c = q[2 + i];
                    int dig = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
                    if (dig < 0) { v = NULL; bits = 0; break; }
                    bits = (bits << 4) | (uint64_t)dig;
                    if (i == 15)
                    {
                        double d;
                        memcpy(&d, &bits, sizeof d);
                        v = nbt_new_double(d);
                    }
                }
                break;
            }
            default: break;
        }
    }
    if (!v) return NULL;
    s->p = end + 1;
    return v;
}

/* A value: a compound, a list or a tagged scalar. The containers being
 * filled sit on an explicit stack, each with the key of the field whose value
 * is being read; a failure frees them innermost first, key before container,
 * as the recursive descent unwound. */
static nbt *parse_value(struct parse *s)
{
    struct { nbt *c; char *key; } st[NBT_WALK_DEPTH];
    int sp = -1;
    nbt *val;

    for (;;)
    {
        /* one value starts here */
        if (*s->p == '{' || *s->p == '[')
        {
            int list = *s->p == '[';

            ++s->p;
            nbt *c = list ? nbt_new_list() : nbt_new_compound();

            if (*s->p == (list ? ']' : '}'))
            {
                ++s->p;
                val = c;
            }
            else
            {
                if (sp + 1 >= NBT_WALK_DEPTH) walk_too_deep("a parsed tag");
                ++sp;
                WL_DEPTH_NOTE(nbt, sp + 1);
                st[sp].c = c;
                st[sp].key = NULL;

                if (list) continue;

                goto key;
            }
        }
        else if (*s->p == '"')
        {
            ++s->p;
            val = parse_scalar(s);
        }
        else val = NULL;

        /* val completes the innermost open container's field or element, and
         * a closed container completes its own parent's */
        for (;;)
        {
            if (!val) goto fail;
            if (sp < 0) return val;

            nbt *c = st[sp].c;

            if (c->type == NBT_COMPOUND)
            {
                nbt_put(c, st[sp].key, val);
                free(st[sp].key);
                st[sp].key = NULL;
                if (*s->p == ',') { ++s->p; goto key; }
                if (*s->p != '}') goto fail;
            }
            else
            {
                nbt_list_add(c, val);
                if (*s->p == ',') { ++s->p; break; }
                if (*s->p != ']') goto fail;
            }

            ++s->p;
            val = c;
            --sp;
        }

        continue;

    key:
        /* the next field's key of the compound on top */
        {
            if (*s->p != '"') goto fail;
            ++s->p;
            const char *end = strchr(s->p, '"');
            if (!end) goto fail;
            char *key = xmalloc((size_t)(end - s->p) + 1);
            memcpy(key, s->p, (size_t)(end - s->p));
            key[end - s->p] = 0;
            s->p = end + 1;
            if (*s->p != ':') { free(key); goto fail; }
            ++s->p;
            st[sp].key = key;
        }

        continue;

    fail:
        for (; sp >= 0; --sp)
        {
            free(st[sp].key);
            nbt_free(st[sp].c);
        }

        return NULL;
    }
}

nbt *nbt_parse(const char *text)
{
    struct parse s = {text};
    nbt *v = parse_value(&s);
    if (v && *s.p) { nbt_free(v); return NULL; }
    return v;
}

/* ---- structural comparison ---- */

struct differ { int found; };

static int same_scalar(const nbt *a, const nbt *b)
{
    switch (a->type)
    {
        case NBT_FLOAT:
        {
            uint32_t x, y;
            memcpy(&x, &a->u.f, sizeof x);
            memcpy(&y, &b->u.f, sizeof y);
            return x == y;
        }
        case NBT_DOUBLE:
        {
            uint64_t x, y;
            memcpy(&x, &a->u.d, sizeof x);
            memcpy(&y, &b->u.d, sizeof y);
            return x == y;
        }
        case NBT_STRING: return !strcmp(a->u.s, b->u.s);
        case NBT_BYTE_ARRAY:
            return a->u.bytes.n == b->u.bytes.n &&
                !memcmp(a->u.bytes.v, b->u.bytes.v, (size_t)a->u.bytes.n);
        case NBT_INT_ARRAY:
            return a->u.ints.n == b->u.ints.n &&
                !memcmp(a->u.ints.v, b->u.ints.v, (size_t)a->u.ints.n * sizeof *a->u.ints.v);
        default: return a->u.i == b->u.i;
    }
}

static void differ_set(char *out, size_t outn, const char *path, const nbt *a, const nbt *b)
{
    char *wa = nbt_render(a), *wb = nbt_render(b);
    snprintf(out, outn, "%s: want %s got %s", path[0] ? path : "(root)", wa, wb);
    free(wa);
    free(wb);
}

/* path carries the descent, out carries the first mismatch. */
static void diff_rec(const nbt *a, const nbt *b, char *path, size_t cap,
                     char *out, size_t outn, struct differ *d)
{
    if (d->found) return;
    if (a->type != b->type) { differ_set(out, outn, path, a, b); d->found = 1; return; }
    switch (a->type)
    {
        case NBT_COMPOUND:
        {
            int *oa = key_order(a), *ob = key_order(b);
            for (int i = 0; i < b->u.comp.n && !d->found; ++i)
                if (!nbt_get(a, b->u.comp.f[ob[i]].key->s))
                {
                    char *wb = nbt_render(b->u.comp.f[ob[i]].val);
                    snprintf(out, outn, "%s.%s: want (absent) got %s", path, b->u.comp.f[ob[i]].key->s, wb);
                    free(wb);
                    d->found = 1;
                }
            for (int i = 0; i < a->u.comp.n && !d->found; ++i)
            {
                const char *key = a->u.comp.f[oa[i]].key->s;
                const nbt *want = a->u.comp.f[oa[i]].val;
                const nbt *got = nbt_get(b, key);
                if (!got)
                {
                    char *wa = nbt_render(want);
                    snprintf(out, outn, "%s.%s: want %s got (absent)", path, key, wa);
                    free(wa);
                    d->found = 1;
                    break;
                }
                size_t len = strlen(path);
                snprintf(path + len, cap - len, ".%s", key);
                diff_rec(want, got, path, cap, out, outn, d);
                path[len] = 0;
            }
            free(oa);
            free(ob);
            break;
        }
        case NBT_LIST:
            if (a->u.list.n != b->u.list.n)
            {
                snprintf(out, outn, "%s: want %d entries got %d", path, a->u.list.n, b->u.list.n);
                d->found = 1;
                break;
            }
            for (int i = 0; i < a->u.list.n && !d->found; ++i)
            {
                size_t len = strlen(path);
                snprintf(path + len, cap - len, "[%d]", i);
                diff_rec(a->u.list.v[i], b->u.list.v[i], path, cap, out, outn, d);
                path[len] = 0;
            }
            break;
        default:
            if (!same_scalar(a, b)) { differ_set(out, outn, path, a, b); d->found = 1; }
            break;
    }
}

int nbt_diff(const nbt *a, const nbt *b, char *buf, size_t n)
{
    static const size_t PATH_CAP = 512;
    char path[512];
    struct differ d = {0};
    path[0] = 0;
    buf[0] = 0;
    diff_rec(a, b, path, PATH_CAP, buf, n, &d);
    return d.found;
}
