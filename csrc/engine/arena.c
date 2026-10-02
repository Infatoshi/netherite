/* See arena.h. */
#define _DEFAULT_SOURCE   /* mincore */
#if defined(SLAB_CHECK)
#define _GNU_SOURCE       /* dladdr */
#endif
#include "arena.h"
#include "env.h"
#include "fallhang.h"
#include "envstack.h"
#include "image.h"
#include "item_entity.h"
#include "living.h"
#include "villagers.h"
#include "world.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <unistd.h>

#if defined(__linux__) && !defined(PR_SET_VMA)
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif

/* The mapping's name in /proc/self/maps ([anon:name]): the grave's scan
 * reads only unnamed anonymous mappings, so a named one is skipped (a kernel
 * without the feature leaves it unnamed, and the scan reads it as before). */
static void name_mapping(void *p, size_t len, const char *name)
{
#if defined(__linux__)
    (void)prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, (unsigned long)p, len, (unsigned long)name);
#else
    /* no mapping names elsewhere; the grave (Linux x86-64 only) is what reads them */
    (void)p; (void)len; (void)name;
#endif
}

/* the arenas alive in the process, for the grave's scan; written when an
 * environment is made or freed, never inside a tick */
static struct arena_env *live_arenas[ARENA_MAX_LIVE];

int arena_live(struct arena_env **out)
{
    int n = 0;

    for (int i = 0; i < ARENA_MAX_LIVE; ++i)
        if (live_arenas[i]) out[n++] = live_arenas[i];
    return n;
}

void *arena_carve(struct arena *a, size_t bytes)
{
    size_t at = (a->used + 63) & ~(size_t)63;

    if (at + bytes > a->size)
    {
        fprintf(stderr, "arena: %zu bytes past its %zu\n", at + bytes, a->size);
        abort();
    }
    a->used = at + bytes;
    return a->base + at;
}

void *fixed_array(size_t cap, size_t size)
{
    size_t len = (cap * size + 4095) & ~(size_t)4095;

    /* an image's are zeroed blocks of its heap (image.h) */
    if (nw_env->img.size) return image_alloc(nw_env, len, 1);

    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    if (p == MAP_FAILED) abort();
    name_mapping(p, len, "nw-fixed");
    return p;
}

void fixed_array_free(void *p, size_t cap, size_t size)
{
    if (p == NULL) return;

    struct env *owner = image_owner(p);

    if (owner != NULL) image_free(owner, p);
    else munmap(p, (cap * size + 4095) & ~(size_t)4095);
}

size_t fixed_array_resident(const void *p, size_t cap, size_t size)
{
    if (p == NULL) return 0;
    uintptr_t lo = (uintptr_t)p & ~(uintptr_t)4095, hi = ((uintptr_t)p + cap * size + 4095) & ~(uintptr_t)4095;
    size_t pages = (hi - lo) / 4096, n = 0;
    /* /proc/self/pagemap: a page counts when it is present and this
     * process's alone (bit 56) or swapped out (bit 62). A page only ever
     * read (the grave's scan reads whole mappings) maps the shared zero
     * page, which mincore would count. */
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd >= 0)
    {
        uint64_t ent[512];
        for (size_t at = 0; at < pages; at += 512)
        {
            size_t k = pages - at < 512 ? pages - at : 512;
            if (pread(fd, ent, k * sizeof *ent, (off_t)((lo / 4096 + at) * sizeof *ent)) != (ssize_t)(k * sizeof *ent))
            {
                close(fd);
                goto by_mincore;
            }
            for (size_t i = 0; i < k; ++i)
                n += (ent[i] >> 63 & 1 && ent[i] >> 56 & 1) || ent[i] >> 62 & 1;
        }
        close(fd);
        return n * 4096;
    }
by_mincore:
    n = 0;
    unsigned char vec[4096];
    for (size_t at = 0; at < pages; at += sizeof vec)
    {
        size_t k = pages - at < sizeof vec ? pages - at : sizeof vec;
        if (mincore((void *)(lo + at * 4096), k * 4096, (void *)vec) != 0) return 0; /* char * on macOS */
        for (size_t i = 0; i < k; ++i) n += vec[i] & 1;
    }
    return n * 4096;
}

/* ------------------------------------------------------------ slot pools */

static size_t pool_slot_size(size_t size)
{
    return (size + 63) & ~(size_t)63;
}

static size_t pool_bytes(int32_t cap, size_t size)
{
    return (size_t)cap * (pool_slot_size(size) + 2 * sizeof(int32_t)) + 3 * 64;
}

static void pool_init(struct slot_pool *p, struct arena *a, int32_t cap, size_t size, const char *what)
{
    p->size = pool_slot_size(size);
    p->cap = cap;
    p->top = p->live = p->freed = 0;
    p->free_head = -1;
    p->what = what;
    p->base = arena_carve(a, (size_t)cap * p->size);
    p->next = arena_carve(a, (size_t)cap * sizeof *p->next);
    p->gen = arena_carve(a, (size_t)cap * sizeof *p->gen);
}

void *pool_take(struct slot_pool *p)
{
    void *obj = pool_take_raw(p);

    memset(obj, 0, p->size);
    return obj;
}

void *pool_take_raw(struct slot_pool *p)
{
    int32_t i = p->free_head;

    if (i >= 0) p->free_head = p->next[i];
    else
    {
        if (p->top == p->cap)
        {
            fprintf(stderr, "arena: more than %d %s\n", p->cap, p->what);
            abort();
        }
        i = p->top++;
    }
    ++p->live;

    return pool_at(p, i);
}

static void pool_give_as(struct slot_pool *p, void *obj, int clear);

void pool_give(struct slot_pool *p, void *obj)
{
    pool_give_as(p, obj, 1);
}

void pool_give_keep(struct slot_pool *p, void *obj)
{
    pool_give_as(p, obj, 0);
}

static void pool_give_as(struct slot_pool *p, void *obj, int clear)
{
    if (obj == NULL) return;
    if (!pool_holds(p, obj))
    {
        fprintf(stderr, "arena: %p is not one of the %s\n", obj, p->what);
        abort();
    }

    int32_t i = pool_index(p, obj);

    /* cleared: a stale copy of an address in it must not keep another entity */
    if (clear) memset(obj, 0, p->size);
    ++p->gen[i];
    p->next[i] = p->free_head;
    p->free_head = i;
    --p->live;
    ++p->freed;
}

static void pool_trim(struct slot_pool *p)
{
    if ((size_t)p->freed * p->size < ARENA_TRIM_BYTES) return;
    p->freed = 0;

    size_t n = (size_t)p->top;
    uint8_t *is_free ENV_LOCAL = envstack_zeroed((n + 7) / 8);

    for (int32_t i = p->free_head; i >= 0; i = p->next[i]) is_free[i >> 3] |= (uint8_t)(1u << (i & 7));
    for (size_t i = 0; i < n;)
    {
        if (!(is_free[i >> 3] >> (i & 7) & 1))
        {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < n && (is_free[j >> 3] >> (j & 7) & 1)) ++j;
        uintptr_t lo = ((uintptr_t)p->base + i * p->size + 4095) & ~(uintptr_t)4095;
        uintptr_t hi = ((uintptr_t)p->base + j * p->size) & ~(uintptr_t)4095;
        if (hi > lo) (void)image_give_pages(nw_env, (void *)lo, hi - lo);
        i = j;
    }
}

void fixed_array_trim(void *p, size_t used, int *hi, size_t size)
{
    if (*hi < 0 || (size_t)*hi <= used) return;

    uintptr_t lo = ((uintptr_t)p + used * size + 4095) & ~(uintptr_t)4095;
    uintptr_t end = ((uintptr_t)p + (size_t)*hi * size + 4095) & ~(uintptr_t)4095;

    if (end <= lo || end - lo < FIXED_TRIM_BYTES) return;
    (void)image_give_pages(nw_env, (void *)lo, end - lo);
    *hi = (int)used;
}

void arena_trim(void)
{
    struct arena_env *a = nw_arena();
    struct slot_pool *pools[] = {&a->bands, &a->nibs, &a->chunks, &a->livings, &a->ai_parts, &a->villager_parts,
                                 &a->player_parts, &a->an_ents, &a->fh_ents, &a->paths};

    for (size_t k = 0; k < sizeof pools / sizeof *pools; ++k) pool_trim(pools[k]);
}

/* ------------------------------------------------------------------ slab */

static int slab_class(size_t bytes)
{
    int k = 6;

    while (((size_t)1 << k) < bytes) ++k;
    return k;
}

#if defined(SLAB_CHECK)
/* The slab's check build (make ... CFLAGS+=-DSLAB_CHECK, Linux): every block
 * a class larger, led by a 64-byte header (the chain link, a magic, its
 * class, live or free, the call sites that took and gave it back), the
 * caller's bytes poisoned while it is free and a red zone after them. A
 * write into a freed block, a block given back twice or with another size's
 * class, and a write past a block's class stop the program naming the
 * block and the sites. */
#include <dlfcn.h>
#include <execinfo.h>

#define SC_HDR 64
#define SC_MAGIC 0x5ab1c4ecu
#define SC_POISON 0xdb
#define SC_RED 0xa5
#define SC_RED_MAX 4096
#define SC_SITES 3

struct sc_hdr {
    int64_t link;
    uint32_t magic;
    uint8_t k, state, pad[2];   /* state 1 live, 2 free */
    void *took[SC_SITES], *gave[SC_SITES];
};

static void sc_sites(void **out)
{
    void *bt[SC_SITES + 2];
    int n = backtrace(bt, SC_SITES + 2);

    for (int i = 0; i < SC_SITES; ++i) out[i] = i + 2 < n ? bt[i + 2] : NULL;
}

static void sc_print_sites(const char *what, void *const *sites)
{
    fprintf(stderr, "  %s:", what);
    for (int i = 0; i < SC_SITES; ++i)
    {
        Dl_info di;
        if (sites[i] == NULL) continue;
        if (dladdr(sites[i], &di) && di.dli_fname)
            fprintf(stderr, " %s+0x%zx", di.dli_fname, (size_t)((uintptr_t)sites[i] - (uintptr_t)di.dli_fbase));
        else fprintf(stderr, " %p", sites[i]);
    }
    fputc('\n', stderr);
}

_Noreturn static void sc_fail(const struct slab *s, int64_t hoff, const char *why, size_t at)
{
    const struct sc_hdr *h = (const struct sc_hdr *)(const void *)(s->base + hoff);
    void *now[SC_SITES];

    sc_sites(now);
    fprintf(stderr, "slab check: %s: block %lld (user %lld) class %d state %d, byte %zu (env %p)\n", why, (long long)hoff,
            (long long)hoff + SC_HDR, h->k, h->state, at, (const void *)s);
    sc_print_sites("took", h->took);
    sc_print_sites("gave", h->gave);
    sc_print_sites("now", now);
    abort();
}

static size_t sc_red_len(int k)
{
    size_t r = ((size_t)1 << k) - SC_HDR;   /* the block is 2 << k: the header, the class, this */
    return r < SC_RED_MAX ? r : SC_RED_MAX;
}

static void sc_check_bytes(const struct slab *s, int64_t hoff, size_t from, size_t len, int v, const char *why)
{
    const unsigned char *b = s->base + hoff + from;
    for (size_t i = 0; i < len; ++i)
        if (b[i] != v) sc_fail(s, hoff, why, from + i);
}

int64_t slab_alloc(struct slab *s, size_t bytes, int zero)
{
    int k = slab_class(bytes);
    size_t len = (size_t)1 << k;
    int64_t off = s->free_head[k + 1];
    struct sc_hdr *h;

    if (off >= 0)
    {
        if ((uint64_t)off >= s->used) { fprintf(stderr, "slab check: class %d chain head %lld past %zu\n", k, (long long)off, s->used); abort(); }
        h = (struct sc_hdr *)(void *)(s->base + off);
        if (h->magic != SC_MAGIC || h->state != 2 || h->k != k) sc_fail(s, off, "a chain block's header is not a free one of its class", 0);
        if (h->link >= 0)
        {
            const struct sc_hdr *nx = (const struct sc_hdr *)(const void *)(s->base + h->link);
            if ((uint64_t)h->link >= s->used || nx->magic != SC_MAGIC || nx->state != 2 || nx->k != k)
                sc_fail(s, off, "the chain link of a freed block was written after its free", 0);
        }
        else if (h->link != -1) sc_fail(s, off, "the chain link of a freed block was written after its free", 0);
        sc_check_bytes(s, off, SC_HDR, len, SC_POISON, "a freed block was written after its free");
        sc_check_bytes(s, off, SC_HDR + len, sc_red_len(k), SC_RED, "a freed block's red zone was written");
        s->free_head[k + 1] = h->link;
        memset(s->base + off + SC_HDR, zero ? 0 : 0xcd, len);
    }
    else
    {
        if (s->used + 2 * len > s->size)
        {
            fprintf(stderr, "arena: the list slab is full (%zu bytes)\n", s->size);
            abort();
        }
        off = (int64_t)s->used;
        s->used += 2 * len;
        h = (struct sc_hdr *)(void *)(s->base + off);
        memset(s->base + off + SC_HDR + len, SC_RED, sc_red_len(k));
    }
    h->link = -1;
    h->magic = SC_MAGIC;
    h->k = (uint8_t)k;
    h->state = 1;
    sc_sites(h->took);
    memset(h->gave, 0, sizeof h->gave);
    return off + SC_HDR;
}

void slab_free(struct slab *s, int64_t off, size_t bytes)
{
    int k = slab_class(bytes);
    size_t len = (size_t)1 << k;
    int64_t hoff = off - SC_HDR;
    struct sc_hdr *h = (struct sc_hdr *)(void *)(s->base + hoff);

    if (hoff < 0 || (uint64_t)hoff >= s->used) { fprintf(stderr, "slab check: free of %lld outside the slab\n", (long long)off); abort(); }
    if (h->magic != SC_MAGIC) sc_fail(s, hoff, "a free of no block (its header is gone)", 0);
    if (h->state == 2) sc_fail(s, hoff, "a block given back twice", 0);
    if (h->k != k)
    {
        fprintf(stderr, "slab check: given back as class %d (%zu bytes)\n", k, bytes);
        sc_fail(s, hoff, "a block given back with another class's size", 0);
    }
    sc_check_bytes(s, hoff, SC_HDR + len, sc_red_len(k), SC_RED, "a write past a block's class");
    memset(s->base + off, SC_POISON, len);
    h->state = 2;
    sc_sites(h->gave);
    h->link = s->free_head[k + 1];
    s->free_head[k + 1] = hoff;
}

#else
int64_t slab_alloc(struct slab *s, size_t bytes, int zero)
{
    int k = slab_class(bytes);
    size_t len = (size_t)1 << k;
    int64_t off = s->free_head[k];

    if (off >= 0)
    {
        memcpy(&s->free_head[k], s->base + off, sizeof(int64_t));
        if (zero) memset(s->base + off, 0, len);
        return off;
    }
    if (s->used + len > s->size)
    {
        fprintf(stderr, "arena: the list slab is full (%zu bytes)\n", s->size);
        abort();
    }
    off = (int64_t)s->used;
    s->used += len;
    return off;   /* never used: zero */
}

void slab_free(struct slab *s, int64_t off, size_t bytes)
{
    int k = slab_class(bytes);

    memcpy(s->base + off, &s->free_head[k], sizeof(int64_t));
    s->free_head[k] = off;
    if (k >= SLAB_GIVE_CLASS)
    {
        /* its whole pages past the chain link go back to the kernel: a
         * madvise, not an allocation (struct env starts with its arena) */
        uintptr_t lo = ((uintptr_t)(s->base + off) + sizeof(int64_t) + 4095) & ~(uintptr_t)4095;
        uintptr_t hi = ((uintptr_t)(s->base + off) + ((size_t)1 << k)) & ~(uintptr_t)4095;
        struct env *owner = (struct env *)(void *)((unsigned char *)s - offsetof(struct arena_env, slab));

        if (hi > lo) (void)image_give_pages(owner, (void *)lo, hi - lo);
    }
}

#endif

void slab_census(const struct slab *s, size_t *live, size_t *free_bytes)
{
    size_t f = 0;

    for (int k = 0; k < SLAB_CLASSES; ++k)
        for (int64_t off = s->free_head[k]; off >= 0; memcpy(&off, s->base + off, sizeof off)) f += (size_t)1 << k;
    *free_bytes = f;
    *live = s->used - f;
}

size_t slab_block_bytes(size_t bytes)
{
    return bytes ? (size_t)1 << slab_class(bytes) : 0;
}

void *slab_take(size_t bytes)
{
    struct slab *s = &nw_arena()->slab;

    return slab_ptr(s, slab_alloc(s, bytes ? bytes : 1, 0));
}

void slab_give(void *p, size_t bytes)
{
    struct slab *s = &nw_arena()->slab;

    if (p != NULL) slab_free(s, (int64_t)((unsigned char *)p - s->base), bytes ? bytes : 1);
}

void *slab_grow(void *p, size_t old_bytes, size_t new_bytes)
{
    if (p != NULL && slab_class(old_bytes ? old_bytes : 1) == slab_class(new_bytes ? new_bytes : 1)) return p;

    void *q = slab_take(new_bytes);

    if (p != NULL)
    {
        memcpy(q, p, old_bytes < new_bytes ? old_bytes : new_bytes);
        slab_give(p, old_bytes);
    }
    return q;
}

void list_full(const char *what, int cap)
{
    fprintf(stderr, "netherite: more than %d %s (Java's list grows; this one is fixed)\n", cap, what);
    abort();
}

void sec_push(struct sec_list *l, int32_t v, int max)
{
    if (l->n >= max) list_full("entities in one chunk section", max);
    if (l->n == l->cap)
    {
        struct slab *s = &nw_arena()->slab;
        int grown = l->cap ? l->cap * 2 : 8;

        if (grown > max) grown = max;

        int64_t off = slab_alloc(s, (size_t)grown * sizeof(int32_t), 0);

        if (l->n) memcpy(slab_ptr(s, off), slab_ptr(s, l->off), (size_t)l->n * sizeof(int32_t));
        if (l->cap) slab_free(s, l->off, (size_t)l->cap * sizeof(int32_t));
        l->off = off;
        l->cap = grown;
    }
    sec_items(l)[l->n++] = v;
}

int sec_remove_first(struct sec_list *l, int32_t v)
{
    int32_t *it = l->cap ? sec_items(l) : NULL;

    for (int i = 0; i < l->n; ++i)
        if (it[i] == v)
        {
            memmove(&it[i], &it[i + 1], (size_t)(l->n - i - 1) * sizeof *it);
            --l->n;
            return 1;
        }
    return 0;
}

void sec_release(struct sec_list *l)
{
    if (l->cap) slab_free(&nw_arena()->slab, l->off, (size_t)l->cap * sizeof(int32_t));
    memset(l, 0, sizeof *l);
}

void *slab_table_room(void *p, int n, int *cap, size_t size)
{
    if (n < *cap) return p;

    struct slab *s = &nw_arena()->slab;
    int grown = *cap ? *cap * 2 : 32;
    int64_t off = slab_alloc(s, (size_t)grown * size, 1);
    void *q = slab_ptr(s, off);

    if (p != NULL)
    {
        memcpy(q, p, (size_t)n * size);
        slab_free(s, (int64_t)((unsigned char *)p - s->base), (size_t)*cap * size);
    }
    *cap = grown;
    return q;
}

void slab_table_free(void *p, int cap, size_t size)
{
    if (p == NULL || cap == 0) return;

    struct slab *s = &nw_arena()->slab;

    slab_free(s, (int64_t)((unsigned char *)p - s->base), (size_t)cap * size);
}

size_t arena_env_bytes(void)
{
    return pool_bytes(ARENA_PATHS, sizeof(struct path_ent))
                + pool_bytes(ARENA_LIVINGS, sizeof(struct living))
                + pool_bytes(ARENA_LIVINGS, sizeof(struct living_ai))
                + pool_bytes(ARENA_LIVINGS, sizeof(struct living_villager))
                + pool_bytes(ARENA_LIVINGS, sizeof(struct living_player))
                + pool_bytes(ARENA_AN_ENTS, sizeof(struct an_ent))
                + pool_bytes(ARENA_IE_ENTS, sizeof(ie_ent))
                + pool_bytes(ARENA_FH_ENTS, sizeof(fh_ent))
                + pool_bytes(ARENA_DOORS, sizeof(struct village_door_info))
                + pool_bytes(ARENA_BANDS, sizeof(struct chunk_sec))
                + pool_bytes(ARENA_NIBS, SEC_NIB_BYTES)
                + pool_bytes(ARENA_CHUNKS, sizeof(struct chunk))
                + (size_t)PF_MAX_POINTS * (sizeof(struct pf_point) + sizeof(int) + 2 * (sizeof(int32_t) + sizeof(int))
                                           + 3 * sizeof(int))
                + ARENA_SLAB_BYTES
                + 64 * 16;
}

void arena_env_init(struct arena_env *e)
{
    size_t size = arena_env_bytes();

    /* reserved, not committed: a page costs memory once it is written */
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    if (p == MAP_FAILED) abort();
    name_mapping(p, size, "nw-arena");
    arena_env_init_in(e, p, size);
    e->a.own_mapping = 1;
}

void arena_env_init_in(struct arena_env *e, void *mem, size_t size)
{
    e->a.base = mem;
    e->a.size = size;
    e->a.used = 0;
    e->a.own_mapping = 0;
    pool_init(&e->paths, &e->a, ARENA_PATHS, sizeof(struct path_ent), "paths");
    pool_init(&e->livings, &e->a, ARENA_LIVINGS, sizeof(struct living), "livings");
    pool_init(&e->ai_parts, &e->a, ARENA_LIVINGS, sizeof(struct living_ai), "livings' AI parts");
    pool_init(&e->villager_parts, &e->a, ARENA_LIVINGS, sizeof(struct living_villager), "villagers' trading parts");
    pool_init(&e->player_parts, &e->a, ARENA_LIVINGS, sizeof(struct living_player), "players' inventory parts");
    pool_init(&e->an_ents, &e->a, ARENA_AN_ENTS, sizeof(struct an_ent), "living-world list entries");
    pool_init(&e->ie_ents, &e->a, ARENA_IE_ENTS, sizeof(ie_ent), "item-world entities");
    pool_init(&e->fh_ents, &e->a, ARENA_FH_ENTS, sizeof(fh_ent), "falling and hanging entities");
    pool_init(&e->doors, &e->a, ARENA_DOORS, sizeof(struct village_door_info), "village doors");
    pool_init(&e->bands, &e->a, ARENA_BANDS, sizeof(struct chunk_sec), "chunk bands");
    pool_init(&e->nibs, &e->a, ARENA_NIBS, SEC_NIB_BYTES, "bands' nibble arrays");
    for (int v = 0; v < 16; ++v) memset(pool_take_raw(&e->nibs), v * 0x11, SEC_NIB_BYTES);
    pool_init(&e->chunks, &e->a, ARENA_CHUNKS, sizeof(struct chunk), "chunks");
    if (e->chunks.size != CHUNK_SLOT) abort();
    e->pf.pts = arena_carve(&e->a, (size_t)PF_MAX_POINTS * sizeof(struct pf_point));
    e->pf.heap = arena_carve(&e->a, (size_t)PF_MAX_POINTS * sizeof(int));
    e->pf.keys = arena_carve(&e->a, (size_t)2 * PF_MAX_POINTS * sizeof(int32_t));
    e->pf.vals = arena_carve(&e->a, (size_t)2 * PF_MAX_POINTS * sizeof(int));
    e->pf.out = arena_carve(&e->a, (size_t)3 * PF_MAX_POINTS * sizeof(int));
    e->slab.base = arena_carve(&e->a, ARENA_SLAB_BYTES);
    e->slab.size = ARENA_SLAB_BYTES;
    for (int k = 0; k < SLAB_CLASSES; ++k) e->slab.free_head[k] = -1;

    for (int i = 0; i < ARENA_MAX_LIVE; ++i)
        if (live_arenas[i] == NULL)
        {
            live_arenas[i] = e;
            break;
        }
}

void arena_env_free(struct arena_env *e)
{
    for (int i = 0; i < ARENA_MAX_LIVE; ++i)
        if (live_arenas[i] == e) live_arenas[i] = NULL;
    if (e->a.base && e->a.own_mapping) munmap(e->a.base, e->a.size);
    memset(e, 0, sizeof *e);
}

void arena_live_replace(struct arena_env *old, struct arena_env *moved)
{
    for (int i = 0; i < ARENA_MAX_LIVE; ++i)
        if (live_arenas[i] == old) live_arenas[i] = moved;
}

/* ------------------------------------------------------------------ paths */

pathref path_alloc(int npoints)
{
    struct path_ent *p = pool_take(&nw_env->arena.paths);

    p->cap = npoints > 0 ? npoints : 1;
    p->pts = slab_take((size_t)p->cap * sizeof *p->pts);
    return pool_index(&nw_env->arena.paths, p) + 1;
}

struct path_ent *path_at(pathref h)
{
    return h ? pool_at(&nw_env->arena.paths, h - 1) : NULL;
}

void path_hold(pathref h)
{
    if (h) ++path_at(h)->shares;
}

void path_drop(pathref h)
{
    if (!h) return;

    struct path_ent *p = path_at(h);

    if (p->shares > 0)
    {
        --p->shares;
        return;
    }
    slab_give(p->pts, (size_t)p->cap * sizeof *p->pts);
    pool_give(&nw_env->arena.paths, p);
}

/* ------------------------------------------------------------- entities */

/* A living's parts given back (cleared: stale references in them must not
 * keep another living) and forgotten. */
void living_parts_release(struct living *l)
{
    struct arena_env *a = &nw_env->arena;

    if (l->ai_part) pool_give(&a->ai_parts, pool_at(&a->ai_parts, l->ai_part - 1));
    if (l->villager_part) pool_give(&a->villager_parts, pool_at(&a->villager_parts, l->villager_part - 1));
    if (l->player_part) pool_give(&a->player_parts, pool_at(&a->player_parts, l->player_part - 1));
    l->ai_part = l->villager_part = l->player_part = 0;
}

void *living_part_take(const struct living *cl, int which)
{
    struct arena_env *a = &nw_env->arena;
    struct living *l = (struct living *)(uintptr_t)cl;   /* the pools' memory: always writable */
    struct slot_pool *p = which == LV_PART_AI ? &a->ai_parts : which == LV_PART_VILLAGER ? &a->villager_parts
                                                                                         : &a->player_parts;
    int32_t *at = which == LV_PART_AI ? &l->ai_part : which == LV_PART_VILLAGER ? &l->villager_part : &l->player_part;
    void *obj = pool_take(p);

    *at = pool_index(p, obj) + 1;
    return obj;
}

struct living *living_alloc(void)
{
    struct arena_env *a = &nw_env->arena;
    struct living *l = pool_take(&a->livings);

    l->part = pool_index(&a->livings, l) + 1;
    return l;
}

void living_release(struct living *l)
{
    if (l == NULL) return;

    living_parts_release(l);
    pool_give(&nw_env->arena.livings, l);
}

void ieref_stale(ieref h)
{
    fprintf(stderr, "arena: an item-world reference outlived its entity (slot %u, generation %u)\n",
            (unsigned)(uint32_t)h, (unsigned)((h >> 32) & 0xFFFFu));
    abort();
}

void fhref_stale(fhref h)
{
    fprintf(stderr, "arena: a falling/hanging reference outlived its entity (slot %u, generation %u)\n",
            (unsigned)(uint32_t)h, (unsigned)((h >> 32) & 0xFFFFu));
    abort();
}

void lref_stale(lref h)
{
    fprintf(stderr, "arena: a living reference outlived its living (slot %u, generation %u)\n",
            (unsigned)(uint32_t)h, (unsigned)((h >> 32) & 0xFFFFu));
    abort();
}

void living_part_missing(const struct living *l)
{
    fprintf(stderr, "arena: living %d (kind %d) is not a pool slot and has no parts\n", l->entity_id, l->kind);
    abort();
}
struct an_ent *an_ent_alloc(void) { return pool_take(&nw_env->arena.an_ents); }
void an_ent_release(struct an_ent *en) { pool_give(&nw_env->arena.an_ents, en); }
ie_ent *ie_ent_alloc(void) { return pool_take(&nw_env->arena.ie_ents); }
void ie_ent_release(ie_ent *en) { pool_give_keep(&nw_env->arena.ie_ents, en); }
fh_ent *fh_ent_alloc(void) { return pool_take(&nw_env->arena.fh_ents); }
struct village_door_info *door_alloc(void) { return pool_take(&nw_env->arena.doors); }
void fh_ent_release(fh_ent *en) { pool_give(&nw_env->arena.fh_ents, en); }
