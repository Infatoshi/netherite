/* See image.h. This file is the one that reaches the C library's malloc:
 * envheap.h's names are undone before any header declares them. */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#define _GNU_SOURCE   /* MAP_FIXED_NOREPLACE, malloc_usable_size */
#include "image.h"
#include "arena.h"
#include "env.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#endif
#include <sys/mman.h>
#if defined(__linux__)
#include <malloc.h>
#include <sys/prctl.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>   /* the Mac builds this too (Elliot plays there) */
#define malloc_usable_size(p) malloc_size(p)
#undef round_page   /* mach's macro; this file has its own */
#elif defined(__NVPTX__)
#define malloc_usable_size(p) ((size_t)0)   /* the device build (cuda/tick/tick.mk): every block is the image's */
#endif

#if defined(__linux__) && !defined(PR_SET_VMA)
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0   /* the address is only a hint; map_slot checks it */
#endif

/* the window and its slots: image.h */

struct env *image_owner(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    if (a - IMG_WINDOW >= IMG_SLOT * IMG_SLOTS) return NULL;
    return (struct env *)(a & ~(IMG_SLOT - 1));
}

int image_holds(const struct env *e, const void *p)
{
    return e->img.size != 0 && (size_t)((const unsigned char *)p - (const unsigned char *)e) < e->img.size;
}

static size_t round_page(size_t n)
{
    return (n + 4095) & ~(size_t)4095;
}

static size_t image_bytes(void)
{
    return round_page(sizeof(struct env)) + round_page(arena_env_bytes()) + IMG_HEAP_BYTES;
}

/* A mapping of len bytes at the first free slot of the window, named for
 * /proc/self/maps (the grave's scan skips a named mapping). */
static void *map_slot(size_t len)
{
    if (len > IMG_SLOT) return NULL;
    for (int i = 0; i < IMG_SLOTS; ++i)
    {
        void *want = (void *)(IMG_WINDOW + (uintptr_t)i * IMG_SLOT);
        void *p = mmap(want, len, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED)
        {
            if (errno == EEXIST) continue;
            return NULL;
        }
        if (p != want)
        {
            /* an old kernel took the address as a hint */
            munmap(p, len);
            continue;
        }
#if defined(__linux__)
        (void)prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, (unsigned long)p, len, (unsigned long)"nw-image");
#endif
        return p;
    }
    return NULL;
}

struct env *image_env_new(void)
{
    size_t len = image_bytes();
    struct env *e = map_slot(len);

    if (e == NULL) return NULL;
    e->img.size = len;
    e->img.heap_off = (int64_t)(round_page(sizeof(struct env)) + round_page(arena_env_bytes()));
    e->img.heap_cap = IMG_HEAP_BYTES;
    for (int k = 0; k < IMG_HEAP_CLASSES; ++k) e->img.heap_free[k] = -1;
    return e;
}

void image_env_free(struct env *e)
{
    if (e != NULL && e->img.size) munmap(e, e->img.size);
}

/* The arena's part of the image: where env_init carves the pools. */
void *image_arena_base(struct env *e)
{
    return (unsigned char *)e + round_page(sizeof(struct env));
}

/* ------------------------------------------------------------------ heap */

#define IMG_BLOCK_MAGIC 0x6e77696du   /* "nwim" */

struct img_block {
    uint32_t magic;
    uint32_t cls;             /* the class in the low byte; above it the block's generation (BLK_GEN) */
    uint64_t n;
};

/* A block's generation counts the times it was freed or reallocated (mod
 * 2^24), so the same bytes stay at an address while its block's generation
 * stays (image_free_count_at); it survives on the free chain with the
 * header. */
#define BLK_CLS(b) ((int)((b)->cls & 0xffu))
#define BLK_GEN 0x100u

/* A free block is zeros but for its chain link: image_free clears it (a
 * block of more than a page gives its pages past the first back instead,
 * madvise, so they read zero again and cost nothing until written), so a
 * block comes out zeroed, and the image holds no stale copy of what its
 * last owner kept (no old pointer to rebase, no old address to pin a buried
 * living). A block of a page or more is page aligned. Under a write tracker
 * (img.track, imgxfer.h) the pages handed back go in the zap log (the
 * tracker cannot see them: a copy would keep their old bytes), and past the
 * log's room the present pages are cleared by writes. */
_Static_assert(sizeof(struct img_block) == 16, "a block's payload is 16-byte aligned");

static int heap_class(size_t need)
{
    int k = 0;
    while (((size_t)32 << k) < need) ++k;
    return k;
}

void *image_alloc(struct env *e, size_t n, int zero)
{
    size_t need = n + sizeof(struct img_block);
    int k = heap_class(need);

    if (k >= IMG_HEAP_CLASSES)
    {
        fprintf(stderr, "image: a block of %zu bytes is past the heap's classes\n", n);
        abort();
    }

    unsigned char *heap = (unsigned char *)e + e->img.heap_off;
    int64_t off = e->img.heap_free[k];
    struct img_block *b;

    if (off >= 0)
    {
        b = (struct img_block *)(heap + off);
        memcpy(&e->img.heap_free[k], b + 1, sizeof(int64_t));
        memset(b + 1, 0, sizeof(int64_t));
    }
    else
    {
        size_t len = (size_t)32 << k;
        /* a block of a page or more starts on a page */
        size_t at = e->img.heap_used;
        if (len >= 4096) at = (at + 4095) & ~(size_t)4095;
        if (at + len > e->img.heap_cap)
        {
            fprintf(stderr, "image: the heap is full (%zu bytes)\n", e->img.heap_cap);
            abort();
        }
        /* a page start skipped for alignment stays a free block of its class
         * run: the gap is left unused (it is never written, so it costs no
         * memory) */
        e->img.heap_used = at + len;
        b = (struct img_block *)(heap + at);
    }
    b->cls = (off >= 0 ? b->cls & ~0xffu : 0) | (uint32_t)k;
    b->magic = IMG_BLOCK_MAGIC;
    b->n = n;
    ++e->img.heap_live;
    return b + 1;
}

static struct img_block *block_of(const void *p)
{
    struct img_block *b = (struct img_block *)p - 1;
    if (b->magic != IMG_BLOCK_MAGIC)
    {
        fprintf(stderr, "image: %p is not a heap block\n", p);
        abort();
    }
    return b;
}

/* Pages given back under a write tracker once the zap log is full: the
 * present ones are cleared by writes, which the tracker sees; a page not
 * present reads zero here and in every copy (a page handed back while
 * tracked is in the log, and the next export carries it) */
static void track_zero_range(unsigned char *p, size_t len)
{
#if !defined(__linux__)
    memset(p, 0, len);   /* no tracker here: not reached */
#else
    unsigned char vec[256];
    for (size_t at = 0; at < len; at += sizeof vec * 4096)
    {
        size_t n = len - at < sizeof vec * 4096 ? len - at : sizeof vec * 4096;
        unsigned char *q = p + at;
        if (mincore(q, n, vec) != 0)
        {
            memset(q, 0, n);
            continue;
        }
        for (size_t k = 0; k < n / 4096; ++k)
            if (vec[k] & 1) memset(q + k * 4096, 0, 4096);
    }
#endif
}

/* the tracker's protection off [p, p + len) (its empty pages keep a marker
 * each otherwise, and with it their page table, which every scan walks) */
static void track_release(void *p, size_t len, int fd)
{
#if defined(__linux__)
    struct uffdio_writeprotect w = {.range = {.start = (uintptr_t)p, .len = len}, .mode = 0};
    (void)ioctl(fd, UFFDIO_WRITEPROTECT, &w);
#else
    (void)p;
    (void)len;
    (void)fd;
#endif
}

int image_give_pages(struct env *e, void *p, size_t len)
{
    if (len == 0) return 1;
    if (e->img.track && e->img.nzap >= IMG_ZAP_MAX)
    {
        /* past the zap log: the present pages cleared by writes */
        track_zero_range(p, len);
        return 0;
    }
    if (e->img.track) track_release(p, len, e->img.track_fd);
    if (madvise(p, len, MADV_DONTNEED) != 0)
    {
        memset(p, 0, len);
        return 0;
    }
    if (e->img.track)
    {
        uint32_t k = e->img.nzap++;
        e->img.zap[k].off = (uint64_t)((unsigned char *)p - (unsigned char *)e);
        e->img.zap[k].len = len;
    }
    return 1;
}

void image_free(struct env *e, void *p)
{
    struct img_block *b = block_of(p);
    unsigned char *heap = (unsigned char *)e + e->img.heap_off;
    int64_t off = (int64_t)((unsigned char *)b - heap);

    size_t len = (size_t)32 << BLK_CLS(b);

    b->cls += BLK_GEN;
    if (len <= 4096) memset(b + 1, 0, len - sizeof *b);
    else
    {
        memset(b + 1, 0, 4096 - sizeof *b);
        (void)image_give_pages(e, (unsigned char *)b + 4096, len - 4096);
    }
    memcpy(b + 1, &e->img.heap_free[BLK_CLS(b)], sizeof(int64_t));
    e->img.heap_free[BLK_CLS(b)] = off;
    --e->img.heap_live;
}

size_t image_block_size(const void *p)
{
    return block_of(p)->n;
}

static size_t block_class_bytes(const struct img_block *b)
{
    return (size_t)32 << BLK_CLS(b);
}

static int off_cmp(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

size_t image_heap_trim(struct env *e)
{
    if (!e->img.size) return 0;
    unsigned char *heap = (unsigned char *)e + e->img.heap_off;
    /* every free block, by offset */
    size_t n = 0, cap = 1024, nr = 0;
    int64_t *offs = malloc(cap * sizeof *offs);
    if (offs == NULL) return 0;
    for (int k = 0; k < IMG_HEAP_CLASSES; ++k)
        for (int64_t off = e->img.heap_free[k]; off >= 0;)
        {
            if (n == cap)
            {
                int64_t *grown = realloc(offs, (cap *= 2) * sizeof *offs);
                if (grown == NULL) { free(offs); return 0; }
                offs = grown;
            }
            offs[n++] = off;
            memcpy(&off, heap + off + sizeof(struct img_block), sizeof off);
        }
    qsort(offs, n, sizeof *offs, off_cmp);
    unsigned char *drop = calloc(n ? n : 1, 1);
    uintptr_t *pages = malloc((n ? n : 1) * 2 * sizeof *pages);
    if (drop == NULL || pages == NULL) { free(offs); free(drop); free(pages); return 0; }
    /* runs of adjacent free blocks: the whole pages inside a run hold
     * nothing but the headers and links of the blocks that start on them
     * (a free block is zeros past its link), which leave their chains */
    for (size_t i = 0; i < n;)
    {
        size_t j = i;
        int64_t hi = offs[i] + (int64_t)block_class_bytes((const struct img_block *)(heap + offs[i]));
        while (j + 1 < n && offs[j + 1] == hi)
        {
            ++j;
            hi = offs[j] + (int64_t)block_class_bytes((const struct img_block *)(heap + offs[j]));
        }
        uintptr_t p0 = ((uintptr_t)(heap + offs[i]) + 4095) & ~(uintptr_t)4095, p1 = (uintptr_t)(heap + hi) & ~(uintptr_t)4095;
        if (p1 > p0)
        {
            for (size_t k = i; k <= j; ++k)
                if ((uintptr_t)(heap + offs[k]) >= p0 && (uintptr_t)(heap + offs[k]) < p1) drop[k] = 1;
            pages[2 * nr] = p0;
            pages[2 * nr + 1] = p1;
            ++nr;
        }
        i = j + 1;
    }
    /* the chains again, in their order, without the dropped blocks */
    for (int k = 0; k < IMG_HEAP_CLASSES && nr; ++k)
    {
        int64_t *link = &e->img.heap_free[k];
        for (int64_t off = *link; off >= 0;)
        {
            int64_t next;
            memcpy(&next, heap + off + sizeof(struct img_block), sizeof next);
            const int64_t *f = bsearch(&off, offs, n, sizeof *offs, off_cmp);
            if (f != NULL && drop[f - offs]) *link = next;
            else link = (int64_t *)(void *)(heap + off + sizeof(struct img_block));
            off = next;
        }
    }
    size_t given = 0;
    for (size_t i = 0; i < nr; ++i)
        if (image_give_pages(e, (void *)pages[2 * i], pages[2 * i + 1] - pages[2 * i])) given += pages[2 * i + 1] - pages[2 * i];
    free(offs);
    free(drop);
    free(pages);
    return given;
}

int image_heap_set(struct env *e, int on)
{
    int was = e->img.heap_on;
    if (e->img.size) e->img.heap_on = on;
    return was;
}

/* ----------------------------------------------------- the engine's malloc */

void *env_malloc(size_t n)
{
    struct env *e = nw_env;
    if (e->img.heap_on) return image_alloc(e, n, 0);
    return malloc(n);
}

void *env_calloc(size_t n, size_t size)
{
    struct env *e = nw_env;
    if (e->img.heap_on)
    {
        if (size && n > SIZE_MAX / size) return NULL;
        return image_alloc(e, n * size, 1);
    }
    return calloc(n, size);
}

uint64_t image_free_count(size_t bytes)
{
    int k = -1;
    while (k + 1 < IMG_FREE_TIERS && bytes >= ((size_t)IMG_FREE_TIER0 << (k + 1))) ++k;
    const uint64_t *c = k < 0 ? &nw_env->img.frees : &nw_env->img.frees_ge[k];
    return __atomic_load_n(c, __ATOMIC_RELAXED);
}

uint64_t image_free_count_at(const void *p, size_t bytes)
{
    struct env *e = image_owner(p);
    if (e != NULL && e->img.size)
    {
        const unsigned char *heap = (const unsigned char *)e + e->img.heap_off, *q = p;
        if (q >= heap + sizeof(struct img_block) && q < heap + e->img.heap_used)
        {
            const struct img_block *b = (const struct img_block *)(const void *)q - 1;
            if (b->magic == IMG_BLOCK_MAGIC && b->n == bytes && BLK_CLS(b) < IMG_HEAP_CLASSES &&
                heap_class(bytes + sizeof *b) == BLK_CLS(b))
                return (uint64_t)__atomic_load_n(&b->cls, __ATOMIC_RELAXED) & 0xffffffffu;
        }
    }
    int k = -1;
    while (k + 1 < IMG_FREE_TIERS && bytes >= ((size_t)IMG_FREE_TIER0 << (k + 1))) ++k;
    return image_free_count(bytes) << 8 | (uint64_t)(0x40 + k + 1);
}

/* p's block (owner's, or the C library's) is about to go: its tiers count */
static void count_free_tiers(struct env *owner, void *p)
{
    size_t len = owner != NULL ? block_class_bytes(block_of(p)) : malloc_usable_size(p);
    for (int k = 0; k < IMG_FREE_TIERS && len >= ((size_t)IMG_FREE_TIER0 << k); ++k)
        __atomic_fetch_add(&nw_env->img.frees_ge[k], 1, __ATOMIC_RELAXED);
}

void env_free_mem(void *p)
{
    if (p == NULL) return;
    __atomic_fetch_add(&nw_env->img.frees, 1, __ATOMIC_RELAXED);   /* threads without an env share env_first */
    struct env *e = image_owner(p);
    count_free_tiers(e, p);
    if (e != NULL) image_free(e, p);
    else free(p);
}

void *env_realloc(void *p, size_t n)
{
    if (p == NULL) return env_malloc(n);

    struct env *owner = image_owner(p);
    struct env *e = nw_env;
    __atomic_fetch_add(&e->img.frees, 1, __ATOMIC_RELAXED);
    count_free_tiers(owner, p);

    if (owner != NULL)
    {
        /* an image's block stays in its image */
        struct img_block *b = block_of(p);
        if (n + sizeof *b <= block_class_bytes(b))
        {
            b->cls += BLK_GEN;
            b->n = n;
            return p;
        }
        void *q = image_alloc(owner, n, 0);
        memcpy(q, p, b->n < n ? b->n : n);
        image_free(owner, p);
        return q;
    }
    if (e->img.heap_on)
    {
        /* the C library's block moves into the image */
        size_t have = malloc_usable_size(p);
        void *q = image_alloc(e, n, 0);
        memcpy(q, p, have < n ? have : n);
        free(p);
        return q;
    }
    return realloc(p, n);
}

void *proc_malloc(size_t n) { return malloc(n); }
void *proc_calloc(size_t n, size_t size) { return calloc(n, size); }
void proc_free(void *p) { free(p); }

char *env_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = env_malloc(n);
    if (d != NULL) memcpy(d, s, n);
    return d;
}

/* ----------------------------------------------------------- relocation */

int image_extents(const struct env *e, struct img_extent *out, int max)
{
    int n = 0;
    const unsigned char *base = (const unsigned char *)e;
    const struct arena_env *a = &e->arena;
    int part = IMG_PART_ENV;
#define EXTENT(p, bytes)                                                         \
    do {                                                                         \
        size_t len_ = (bytes);                                                   \
        if (len_ && n < max) out[n++] = (struct img_extent){(size_t)((const unsigned char *)(p) - base), len_, part}; \
    } while (0)

    EXTENT(e, sizeof *e);
    part = IMG_PART_POOLS;
    const struct slot_pool *pools[] = {&a->paths, &a->livings, &a->ai_parts, &a->villager_parts, &a->player_parts,
                                       &a->an_ents, &a->ie_ents, &a->fh_ents, &a->doors, &a->bands, &a->nibs, &a->chunks};
    for (size_t i = 0; i < sizeof pools / sizeof *pools; ++i)
    {
        const struct slot_pool *p = pools[i];
        EXTENT(p->base, (size_t)p->top * p->size);
        EXTENT(p->next, (size_t)p->top * sizeof *p->next);
        EXTENT(p->gen, (size_t)p->top * sizeof *p->gen);
    }
    /* the path finder's arrays (a->pf) are one search's scratch */
    part = IMG_PART_SLAB;
    EXTENT(a->slab.base, a->slab.used);
    part = IMG_PART_HEAP;
    EXTENT(base + e->img.heap_off, e->img.heap_used);
#undef EXTENT
    return n;
}

struct env *image_relocate(struct env *a, const struct env *twin, struct img_reloc_stats *st)
{
    struct img_reloc_stats s = {0};

    if (!a->img.size || twin->img.size != a->img.size) return NULL;

    unsigned char *nb = map_slot(a->img.size);
    if (nb == NULL) return NULL;

    struct img_extent ext[64];
    int next = image_extents(a, ext, 64);
    const unsigned char *ab = (const unsigned char *)a, *tb = (const unsigned char *)twin;
    const uintptr_t lo = (uintptr_t)ab, size = a->img.size;
    const intptr_t delta = (intptr_t)((uintptr_t)nb - lo);

    for (int i = 0; i < next; ++i)
    {
        size_t off = ext[i].off & ~(size_t)7, end = (ext[i].off + ext[i].len + 7) & ~(size_t)7;
        for (size_t p = off; p < end;)
        {
            /* a run up to the next page boundary; a run of zeros stays
             * unwritten (the new mapping is zeros), so it costs no memory */
            size_t q = (p + 4096) & ~(size_t)4095;
            if (q > end) q = end;
            const uint64_t *src = (const uint64_t *)(ab + p);
            size_t nw = (q - p) / 8;
            uint64_t any = 0;
            for (size_t k = 0; k < nw; ++k) any |= src[k];
            s.words_scanned += nw;
            if (any)
            {
                uint64_t *dst = (uint64_t *)(nb + p);
                const uint64_t *tw = (const uint64_t *)(tb + p);
                memcpy(dst, src, q - p);
                s.bytes_copied += q - p;
                for (size_t k = 0; k < nw; ++k)
                    if ((uintptr_t)src[k] - lo < size && src[k] != tw[k])
                    {
                        dst[k] = (uint64_t)((intptr_t)src[k] + delta);
                        ++s.pointers_rebased;
                        ++s.rebased_in[ext[i].part];
                    }
            }
            p = q;
        }
    }
    s.extents = (size_t)next;

    struct env *moved = (struct env *)nb;
    arena_live_replace(&a->arena, &moved->arena);
    munmap(a, a->img.size);
    if (st) *st = s;
    return moved;
}

/* ------------------------------------------------------------- moving */

void *image_slot_reserve(size_t len)
{
    void *p = map_slot(len);
    if (p != NULL && mprotect(p, len, PROT_NONE) != 0)
    {
        munmap(p, len);
        return NULL;
    }
    return p;
}

int image_move(void *from, void *to, size_t len)
{
#if defined(__linux__)
    void *r = mremap(from, len, len, MREMAP_MAYMOVE | MREMAP_FIXED, to);
    if (r != to) return -1;
    /* the vacated slot stays taken, so no other mapping lands in it */
    void *h = mmap(from, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    return h == from ? 0 : -1;
#else
    (void)from;
    (void)to;
    (void)len;
    return -1;
#endif
}
