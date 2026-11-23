/* See image.h. This file is the one that reaches the C library's malloc:
 * envheap.h's names are undone before any header declares them. */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#define _GNU_SOURCE   /* MAP_FIXED_NOREPLACE, malloc_usable_size */
#include "image.h"
#include "image_wt.h"
#include "arena.h"
#include "env.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#if defined(__linux__) && defined(__x86_64__)
#include <emmintrin.h>
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
#if defined(__linux__)
/* a present page zeroed with streaming stores (lane/cpuhot): whole lines written past the cache, so the page's old
 * lines are not read from DRAM first as a memset's stores read them (the page was last written many steps ago); the
 * same zeros. track_zero_range fences once after its pages */
static void track_zero_page(unsigned char *q)
{
#if defined(__x86_64__)
    const __m128i z = _mm_setzero_si128();
    for (size_t i = 0; i < 4096; i += 64)
    {
        _mm_stream_si128((__m128i *)(void *)(q + i), z);
        _mm_stream_si128((__m128i *)(void *)(q + i + 16), z);
        _mm_stream_si128((__m128i *)(void *)(q + i + 32), z);
        _mm_stream_si128((__m128i *)(void *)(q + i + 48), z);
    }
#else
    memset(q, 0, 4096);
#endif
}
#endif

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
            if (vec[k] & 1) track_zero_page(q + k * 4096);
    }
#if defined(__x86_64__)
    _mm_sfence();   /* the streamed zeros ordered before the env's next stores (and the step's hand-back) */
#endif
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

#ifdef POOL_CROSS_COUNT
#include <time.h>
static double trim_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}
#define TRIM_T(i) tt[i] = trim_ms()
#else
#define TRIM_T(i) (void)0
#endif

/* a free block by its offset, and where it sits in the chain walk */
struct trim_block {
    int64_t off;
    size_t at;
};

static int trim_cmp(const void *a, const void *b)
{
    return off_cmp(&((const struct trim_block *)a)->off, &((const struct trim_block *)b)->off);
}

/* v by offset: two passes of a 16-bit radix sort on bits 5 to 36 (a
 * block's offset is a multiple of 32, the heap under 32 GB), tmp the
 * scratch; qsort for an offset past them */
static void trim_sort(struct trim_block *v, struct trim_block *tmp, size_t n)
{
    uint32_t *count = calloc(65536, sizeof *count);
    int fits = count != NULL;
    for (size_t i = 0; i < n && fits; ++i) fits = v[i].off >= 0 && v[i].off < ((int64_t)1 << 37);
    if (!fits)
    {
        free(count);
        qsort(v, n, sizeof *v, trim_cmp);
        return;
    }
    for (int pass = 0; pass < 2; ++pass)
    {
        const int shift = 5 + 16 * pass;
        struct trim_block *from = pass ? tmp : v, *to = pass ? v : tmp;
        memset(count, 0, 65536 * sizeof *count);
        for (size_t i = 0; i < n; ++i) ++count[(from[i].off >> shift) & 0xffff];
        uint32_t sum = 0;
        for (size_t d = 0; d < 65536; ++d)
        {
            uint32_t c = count[d];
            count[d] = sum;
            sum += c;
        }
        for (size_t i = 0; i < n; ++i) to[count[(from[i].off >> shift) & 0xffff]++] = from[i];
    }
    free(count);
}

size_t image_heap_trim(struct env *e)
{
    if (!e->img.size) return 0;
#ifdef POOL_CROSS_COUNT
    double tt[6];
#endif
    TRIM_T(0);
    unsigned char *heap = (unsigned char *)e + e->img.heap_off;
    /* every free block in its chain's order, the chain's class its own (the
     * walk reads each block's link and nothing else: one miss a block),
     * then by offset (lane/crossreset: a seed start's load frees 160k to
     * 190k blocks, and walking the chains twice with a bsearch a block
     * took 17 to 22 ms an image) */
    size_t n = 0, cap = 1024, nr = 0;
    int64_t *offs = malloc(cap * sizeof *offs);
    size_t first[IMG_HEAP_CLASSES + 1];
    if (offs == NULL) return 0;
    for (int k = 0; k < IMG_HEAP_CLASSES; ++k)
    {
        first[k] = n;
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
    }
    first[IMG_HEAP_CLASSES] = n;
    TRIM_T(1);
    struct trim_block *by = malloc((n ? n : 1) * 2 * sizeof *by);
    unsigned char *drop = calloc(n ? n : 1, 1), *kc = malloc(n ? n : 1);
    uintptr_t *pages = malloc((n ? n : 1) * 2 * sizeof *pages);
    if (by == NULL || drop == NULL || kc == NULL || pages == NULL)
    {
        free(offs); free(by); free(drop); free(kc); free(pages);
        return 0;
    }
    for (int k = 0; k < IMG_HEAP_CLASSES; ++k)
        for (size_t i = first[k]; i < first[k + 1]; ++i)
        {
            by[i].off = offs[i];
            by[i].at = i;
            kc[i] = (unsigned char)k;
        }
    trim_sort(by, by + n, n);
    TRIM_T(2);
    /* runs of adjacent free blocks: the whole pages inside a run hold
     * nothing but the headers and links of the blocks that start on them
     * (a free block is zeros past its link), which leave their chains */
    for (size_t i = 0; i < n;)
    {
        size_t j = i;
        int64_t hi = by[i].off + ((int64_t)32 << kc[by[i].at]);
        while (j + 1 < n && by[j + 1].off == hi)
        {
            ++j;
            hi = by[j].off + ((int64_t)32 << kc[by[j].at]);
        }
        uintptr_t p0 = ((uintptr_t)(heap + by[i].off) + 4095) & ~(uintptr_t)4095, p1 = (uintptr_t)(heap + hi) & ~(uintptr_t)4095;
        if (p1 > p0)
        {
            for (size_t k = i; k <= j; ++k)
                if ((uintptr_t)(heap + by[k].off) >= p0 && (uintptr_t)(heap + by[k].off) < p1) drop[by[k].at] = 1;
            pages[2 * nr] = p0;
            pages[2 * nr + 1] = p1;
            ++nr;
        }
        i = j + 1;
    }
    TRIM_T(3);
    /* the chains in their order without the dropped blocks: a link written
     * only where dropped blocks followed the block it sits in */
    for (int k = 0; k < IMG_HEAP_CLASSES && nr; ++k)
    {
        int64_t *link = &e->img.heap_free[k];
        int skipped = 0;
        for (size_t i = first[k]; i < first[k + 1]; ++i)
        {
            if (drop[i]) { skipped = 1; continue; }
            if (skipped) *link = offs[i];
            skipped = 0;
            link = (int64_t *)(void *)(heap + offs[i] + sizeof(struct img_block));
        }
        if (skipped) *link = -1;
    }
#ifdef IMAGE_TRIM_VERIFY
    /* the verify build: every kept block's class is its chain's, and each
     * chain is the walk's order without the dropped blocks */
    {
        size_t bad = 0, kept = 0;
        for (int k = 0; k < IMG_HEAP_CLASSES; ++k)
        {
            int64_t off = e->img.heap_free[k];
            for (size_t i = first[k]; i < first[k + 1]; ++i)
            {
                bad += (int)BLK_CLS((const struct img_block *)(heap + offs[i])) != k;
                if (drop[i]) continue;
                ++kept;
                bad += off != offs[i];
                if (off >= 0) memcpy(&off, heap + off + sizeof(struct img_block), sizeof off);
            }
            bad += off != -1;
        }
        fprintf(stderr, "TRIMVERIFY %zu free blocks, %zu kept, %zu differ\n", n, kept, bad);
    }
#endif
    TRIM_T(4);
    size_t given = 0;
    for (size_t i = 0; i < nr; ++i)
        if (image_give_pages(e, (void *)pages[2 * i], pages[2 * i + 1] - pages[2 * i])) given += pages[2 * i + 1] - pages[2 * i];
    TRIM_T(5);
#ifdef POOL_CROSS_COUNT
    fprintf(stderr, "CROSSTRIM %zu free blocks, %zu runs, %zu pages given: walk %.1f, sort %.1f, runs %.1f, relink %.1f, give %.1f ms\n", n, nr,
            given / 4096, tt[1] - tt[0], tt[2] - tt[1], tt[3] - tt[2], tt[4] - tt[3], tt[5] - tt[4]);
#endif
    free(offs);
    free(by);
    free(drop);
    free(kc);
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
    struct env *e = neth_env;
    if (e->img.heap_on) return image_alloc(e, n, 0);
    return malloc(n);
}

void *env_calloc(size_t n, size_t size)
{
    struct env *e = neth_env;
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
    const uint64_t *c = k < 0 ? &neth_env->img.frees : &neth_env->img.frees_ge[k];
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
        __atomic_fetch_add(&neth_env->img.frees_ge[k], 1, __ATOMIC_RELAXED);
}

void env_free_mem(void *p)
{
    if (p == NULL) return;
    __atomic_fetch_add(&neth_env->img.frees, 1, __ATOMIC_RELAXED);   /* threads without an env share env_first */
    struct env *e = image_owner(p);
    count_free_tiers(e, p);
    if (e != NULL) image_free(e, p);
    else free(p);
}

void *env_realloc(void *p, size_t n)
{
    if (p == NULL) return env_malloc(n);

    struct env *owner = image_owner(p);
    struct env *e = neth_env;
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

/* --------------------------------------------- the reset's write tracker */

#if defined(__linux__) && defined(PAGEMAP_SCAN)

struct img_wtrack {
    int uffd, pagemap;
};

_Static_assert(sizeof(struct img_wrun) == sizeof(struct page_region), "a written run is the scan's region");

struct img_wtrack *image_wtrack_open(void)
{
    struct img_wtrack *t = malloc(sizeof *t);
    if (t == NULL) return NULL;
    t->uffd = (int)syscall(SYS_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
    t->pagemap = -1;
    struct uffdio_api api = {.api = UFFD_API, .features = UFFD_FEATURE_WP_ASYNC};
    if (t->uffd < 0 || ioctl(t->uffd, UFFDIO_API, &api) != 0 ||
        (t->pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC)) < 0)
    {
        image_wtrack_close(t);
        return NULL;
    }
    return t;
}

void image_wtrack_close(struct img_wtrack *t)
{
    if (t == NULL) return;
    if (t->pagemap >= 0) close(t->pagemap);
    if (t->uffd >= 0) close(t->uffd);
    free(t);
}

int image_wtrack_add(struct img_wtrack *t, struct env *e)
{
    struct uffdio_register r = {.range = {.start = (uintptr_t)e, .len = e->img.size}, .mode = UFFDIO_REGISTER_MODE_WP};
    return e->img.size && ioctl(t->uffd, UFFDIO_REGISTER, &r) == 0 ? 0 : -1;
}

long image_wtrack_scan(struct img_wtrack *t, struct env *e, size_t *at, size_t end, struct img_wrun *out, size_t max)
{
    const uintptr_t base = (uintptr_t)e;
    /* every written entry, present or not (a page swapped out, or being
     * migrated, which compaction does at any time: its entry is not present
     * for a while); the return mask's second category keeps the scan off its
     * fast path, which counts an empty entry as written */
    struct pm_scan_arg a = {
        .size = sizeof a,
        .start = base + *at,
        .end = base + end,
        .vec = (uintptr_t)out,
        .vec_len = max,
        .category_mask = PAGE_IS_WRITTEN,
        .return_mask = PAGE_IS_WRITTEN | PAGE_IS_PRESENT | PAGE_IS_SWAPPED,
    };
    long n = ioctl(t->pagemap, PAGEMAP_SCAN, &a);
    if (n < 0) return -1;
    *at = n < (long)max ? end : (size_t)(a.walk_end - base);
    /* lane/rawskill: a hole (neither present nor swapped: a page handed back and not written since, reading zero) is
     * not a written page. Linux 7.0 lists the holes of a protected range as written (its protect walk leaves them no
     * marker): a reset to the same start then zeroed every page the last whole copy had handed back, the image's
     * 6.5 GB arena, 2.1 million faults and 0.9 s a reset at 10 s episodes (on the kernel before, lane/resetcopy's
     * 2 ms resets, none was). A swapped or migrating page holds bytes and stays listed. */
    long k = 0;
    for (long i = 0; i < n; ++i)
    {
        if (!(out[i].cat & (PAGE_IS_PRESENT | PAGE_IS_SWAPPED))) continue;
        out[k] = out[i];
        out[k].lo -= base;
        out[k].hi -= base;
        ++k;
    }
    return k;
}

int image_wtrack_protect(struct img_wtrack *t, struct env *e, size_t end, const struct img_wrun *skip, size_t nskip)
{
    /* no output: the scan's exclusive write-protect walk (one TLB flush a
     * page table it changed) */
    struct pm_scan_arg a = {
        .size = sizeof a,
        .flags = PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
        .category_mask = PAGE_IS_WRITTEN,
        .return_mask = PAGE_IS_WRITTEN,
    };
    const uintptr_t base = (uintptr_t)e;
    size_t at = 0;
    for (size_t i = 0; i <= nskip; ++i)
    {
        size_t lo = i < nskip && skip[i].lo < end ? skip[i].lo : end;
        if (lo > at)
        {
            a.start = base + at;
            a.end = base + lo;
            if (ioctl(t->pagemap, PAGEMAP_SCAN, &a) < 0) return -1;
        }
        if (lo >= end) break;
        at = skip[i].hi;
    }
    e->img.track = 1;
    e->img.track_fd = t->uffd;
    e->img.nzap = 0;
    return 0;
}

int image_wtrack_protect_runs(struct img_wtrack *t, struct env *e, const struct img_wrun *runs, size_t n)
{
    struct pm_scan_arg a = {
        .size = sizeof a,
        .flags = PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
        .category_mask = PAGE_IS_WRITTEN,
        .return_mask = PAGE_IS_WRITTEN,
    };
    const uintptr_t base = (uintptr_t)e;
    for (size_t i = 0; i < n; ++i)
    {
        if (runs[i].hi <= runs[i].lo) continue;
        a.start = base + runs[i].lo;
        a.end = base + runs[i].hi;
        if (ioctl(t->pagemap, PAGEMAP_SCAN, &a) < 0) return -1;
    }
    e->img.track = 1;
    e->img.track_fd = t->uffd;
    e->img.nzap = 0;
    return 0;
}

int image_wtrack_unprotect(struct img_wtrack *t, struct env *e, size_t end)
{
    struct uffdio_writeprotect w = {.range = {.start = (uintptr_t)e, .len = end}, .mode = 0};
    return ioctl(t->uffd, UFFDIO_WRITEPROTECT, &w) == 0 ? 0 : -1;
}

#else

struct img_wtrack *image_wtrack_open(void) { return NULL; }
int image_wtrack_unprotect(struct img_wtrack *t, struct env *e, size_t end)
{
    (void)t;
    (void)e;
    (void)end;
    return -1;
}
void image_wtrack_close(struct img_wtrack *t) { (void)t; }
int image_wtrack_add(struct img_wtrack *t, struct env *e)
{
    (void)t;
    (void)e;
    return -1;
}
long image_wtrack_scan(struct img_wtrack *t, struct env *e, size_t *at, size_t end, struct img_wrun *out, size_t max)
{
    (void)t;
    (void)e;
    (void)at;
    (void)end;
    (void)out;
    (void)max;
    return -1;
}
int image_wtrack_protect(struct img_wtrack *t, struct env *e, size_t end, const struct img_wrun *skip, size_t nskip)
{
    (void)t;
    (void)e;
    (void)end;
    (void)skip;
    (void)nskip;
    return -1;
}
int image_wtrack_protect_runs(struct img_wtrack *t, struct env *e, const struct img_wrun *runs, size_t n)
{
    (void)t;
    (void)e;
    (void)runs;
    (void)n;
    return -1;
}

#endif

/* ------------------------------------------------- the pages it holds */

int image_pagemap_open(void)
{
#if defined(__linux__) && defined(PAGEMAP_SCAN)
    return open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
#else
    return -1;
#endif
}

long image_held(int fd, const struct env *e, size_t *at, size_t end, struct img_wrun *out, size_t max)
{
#if defined(__linux__) && defined(PAGEMAP_SCAN)
    const uintptr_t base = (uintptr_t)e;
    /* a page with memory of its own (not the shared zero page), or its
     * memory swapped out or being migrated: an entry neither holds no byte
     * but zero */
    struct pm_scan_arg a = {
        .size = sizeof a,
        .start = base + *at,
        .end = base + ((end + 4095) & ~(size_t)4095),
        .vec = (uintptr_t)out,
        .vec_len = max,
        .category_inverted = PAGE_IS_PFNZERO,
        .category_mask = PAGE_IS_PFNZERO,
        .category_anyof_mask = PAGE_IS_PRESENT | PAGE_IS_SWAPPED,
        .return_mask = PAGE_IS_PRESENT | PAGE_IS_SWAPPED,
    };
    if (fd < 0) return -1;
    long n = ioctl(fd, PAGEMAP_SCAN, &a);
    if (n < 0) return -1;
    for (long i = 0; i < n; ++i)
    {
        out[i].lo -= base;
        out[i].hi -= base;
    }
    *at = n < (long)max ? end : (size_t)(a.walk_end - base);
    return n;
#else
    (void)fd;
    (void)e;
    (void)at;
    (void)end;
    (void)out;
    (void)max;
    return -1;
#endif
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
