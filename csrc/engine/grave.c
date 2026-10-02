/* Dead livings, released once no memory refers to them (grave.h).
 *
 * The bookkeeping lives in its own mmap areas, which the scan skips, and the
 * sweep allocates nothing from malloc: a freed malloc block keeps its bytes,
 * and a stale copy of a buried address there would pin it. Such stale copies
 * elsewhere (a list's slot past its end, a dead stack frame) only keep a
 * buried living for a later sweep; they never free a live one. */
#define _GNU_SOURCE
#include "grave.h"
#include "arena.h"
#include "env.h"
#include "living.h"

#include <fcntl.h>
#include <stdatomic.h>
#if defined(__x86_64__)
#include <immintrin.h>   /* the AVX2 scan; the grave runs on Linux x86-64 only */
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__linux__) && defined(__x86_64__)
#define GRAVE_SUPPORTED 1
#else
#define GRAVE_SUPPORTED 0
#endif


/* one object's bytes, sorted by lo; rec < 0 for the bookkeeping areas */
struct grave_range {
    uintptr_t lo, hi;
    int32_t rec;
};

/* a living with its parts */
#define LIVING_BYTES (sizeof(struct living) + sizeof(struct living_ai) + sizeof(struct living_villager) + sizeof(struct living_player))

static size_t threshold;
/* grave_enable_images: each sweep reads its own image's memory only */
static int images_only;
/* the current environment's records (grave.h struct grave_env) */
#define G (nw_env->grave)
#define recs (G.recs)
#define nrecs (G.nrecs)
#define caprecs (G.caprecs)
#define buried_bytes (G.buried_bytes)
#define next_sweep (G.next_sweep)

#if GRAVE_SUPPORTED
/* the sweep's scratch, one mapping: ranges, marks, the worklist, the page
 * bitmap and the maps text; the thread's own (the env pool's workers sweep
 * their envs at once, grave_enable_images) */
static _Thread_local unsigned char *scratch;
static _Thread_local size_t scratch_len;
static _Thread_local struct grave_range *ranges;
static _Thread_local size_t nranges;
static _Thread_local uint8_t *marks;
static _Thread_local int32_t *work;
static _Thread_local size_t nwork;
static _Thread_local uint8_t *pages;
static _Thread_local uintptr_t span_lo, span_len;

static void *area(size_t len)
{
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
#endif

static struct { uintptr_t lo, hi; } ignored[8];
static int nignored;

void grave_ignore(const void *p, size_t len)
{
    for (int i = 0; i < nignored; ++i)
        if (ignored[i].lo == (uintptr_t)p) return;
    if (nignored == 8) return;
    ignored[nignored].lo = (uintptr_t)p;
    ignored[nignored].hi = (uintptr_t)p + len;
    ++nignored;
}

#if GRAVE_SUPPORTED
static int have_avx2;
#endif
static int forbidden;
/* grave_hold: another thread may end it */
static _Atomic int held;

void grave_hold(int on)
{
    atomic_store(&held, on);
}

void grave_forbid(void)
{
    forbidden = 1;
    threshold = 0;
}

void grave_enable_images(size_t livings)
{
    grave_enable(livings * LIVING_BYTES);
    images_only = threshold != 0;
}

void grave_enable(size_t t)
{
    if (forbidden) return;
#if GRAVE_SUPPORTED
    have_avx2 = __builtin_cpu_supports("avx2");
#endif
    threshold = GRAVE_SUPPORTED ? t : 0;
}

void grave_bury(struct living *l, struct an_ent *en)
{
    if (!threshold || l == NULL) return;
    if (nrecs == caprecs)
    {
        size_t cap = caprecs ? caprecs * 2 : 4096;
        struct grave_rec *n = fixed_array(cap, sizeof *n);
        if (recs) { memcpy(n, recs, nrecs * sizeof *n); fixed_array_free(recs, caprecs, sizeof *recs); }
        recs = n;
        caprecs = cap;
    }
    recs[nrecs].l = l;
    recs[nrecs].en = en;
    ++nrecs;
    buried_bytes += LIVING_BYTES;
}

#if GRAVE_SUPPORTED

static void sort_ranges(struct grave_range *a, size_t n)
{
    /* heapsort by lo: in place, no malloc */
    for (size_t start = n / 2; start-- > 0;)
        for (size_t i = start;;)
        {
            size_t c = 2 * i + 1, m = i;
            if (c < n && a[c].lo > a[m].lo) m = c;
            if (c + 1 < n && a[c + 1].lo > a[m].lo) m = c + 1;
            if (m == i) break;
            struct grave_range t = a[i]; a[i] = a[m]; a[m] = t;
            i = m;
        }
    for (size_t end = n; end-- > 1;)
    {
        struct grave_range t = a[0]; a[0] = a[end]; a[end] = t;
        for (size_t i = 0;;)
        {
            size_t c = 2 * i + 1, m = i;
            if (c < end && a[c].lo > a[m].lo) m = c;
            if (c + 1 < end && a[c + 1].lo > a[m].lo) m = c + 1;
            if (m == i) break;
            t = a[i]; a[i] = a[m]; a[m] = t;
            i = m;
        }
    }
}

/* the last range with lo <= w */
static size_t range_at(uintptr_t w)
{
    size_t a = 0, b = nranges;
    while (b - a > 1)
    {
        size_t m = (a + b) / 2;
        if (ranges[m].lo <= w) a = m; else b = m;
    }
    return a;
}

/* A buried record's memory: the living, its parts in the arena (living.h)
 * and its list entry. Returns how many regions went into lo/hi. */
static int rec_regions(const struct grave_rec *r, uintptr_t lo[5], uintptr_t hi[5])
{
    int n = 0;
    lo[n] = (uintptr_t)r->l; hi[n++] = (uintptr_t)(r->l + 1);
    if (r->l->part > 0)
    {
        struct living_ai *ai = lv_ai_peek(r->l);
        struct living_villager *vi = lv_villager_peek(r->l);
        struct living_player *pl = lv_player_peek(r->l);
        if (ai) { lo[n] = (uintptr_t)ai; hi[n++] = (uintptr_t)(ai + 1); }
        if (vi) { lo[n] = (uintptr_t)vi; hi[n++] = (uintptr_t)(vi + 1); }
        if (pl) { lo[n] = (uintptr_t)pl; hi[n++] = (uintptr_t)(pl + 1); }
    }
    if (r->en) { lo[n] = (uintptr_t)r->en; hi[n++] = (uintptr_t)(r->en + 1); }
    return n;
}

/* marks: 1 kept, 2 a second burial of the same living (dropped unfreed) */
static void mark(int32_t r)
{
    if (r < 0 || (marks[r] & 1)) return;
    marks[r] |= 1;
    work[nwork++] = r;
}

/* the buried livings by pool slot (a living reference, living.h lref, names
 * a slot): slot_rec[slot] is the first record, rec_next the others */
static _Thread_local int32_t *slot_rec, *rec_next;

static inline void scan_lref(uintptr_t w)
{
    uint32_t slot = (uint32_t)w;
    if (slot >= ARENA_LIVINGS) return;
    for (int32_t r = slot_rec[slot]; r >= 0; r = rec_next[r]) mark(r);
}

static inline void scan_word(const uintptr_t *p, uintptr_t lo, uintptr_t len)
{
    if ((*p >> 48) == LREF_TAG)
    {
        scan_lref(*p);
        return;
    }
    uintptr_t off = *p - lo;
    if (off >= len) return;
    size_t pg = off >> 12;
    if (pages && !(pages[pg >> 3] >> (pg & 7) & 1)) return;
    size_t i = range_at(*p);
    if (*p >= ranges[i].lo && *p < ranges[i].hi) mark(ranges[i].rec);
}

/* The same filter eight words at a time (AVX2, where the CPU has it): the
 * sweep reads every writable page of the process, and this loop is most of
 * its instructions (gold-g30-s42: 165 M of a sweep's 205 M in the
 * four-word form). */
__attribute__((target("avx2"), noinline)) static const uintptr_t *scan_words8(const uintptr_t *p, const uintptr_t *end)
{
    const __m256i sign = _mm256_set1_epi64x((long long)0x8000000000000000ull);
    const __m256i lo = _mm256_set1_epi64x((long long)span_lo);
    const __m256i lim = _mm256_xor_si256(_mm256_set1_epi64x((long long)span_len), sign);
    const __m256i tag = _mm256_set1_epi64x((long long)LREF_TAG);
    for (; end - p >= 8; p += 8)
    {
        __m256i a = _mm256_loadu_si256((const __m256i *)p), b = _mm256_loadu_si256((const __m256i *)(p + 4));
        /* p - lo < len unsigned, as a signed compare with the sign bits flipped */
        __m256i ina = _mm256_cmpgt_epi64(lim, _mm256_xor_si256(_mm256_sub_epi64(a, lo), sign));
        __m256i inb = _mm256_cmpgt_epi64(lim, _mm256_xor_si256(_mm256_sub_epi64(b, lo), sign));
        __m256i tga = _mm256_cmpeq_epi64(_mm256_srli_epi64(a, 48), tag);
        __m256i tgb = _mm256_cmpeq_epi64(_mm256_srli_epi64(b, 48), tag);
        __m256i any = _mm256_or_si256(_mm256_or_si256(ina, inb), _mm256_or_si256(tga, tgb));
        if (_mm256_testz_si256(any, any)) continue;
        for (int j = 0; j < 8; ++j) scan_word(p + j, span_lo, span_len);
    }
    return p;
}

__attribute__((noinline)) static void scan_words(const uintptr_t *p, const uintptr_t *end)
{
    if (have_avx2) p = scan_words8(p, end);
    const uintptr_t lo = span_lo, len = span_len;
    /* nearly every word is outside the span: test four at a time */
    for (; end - p >= 4; p += 4)
    {
        int any = (p[0] - lo < len) | (p[1] - lo < len) | (p[2] - lo < len) | (p[3] - lo < len) |
                  ((p[0] >> 48) == LREF_TAG) | ((p[1] >> 48) == LREF_TAG) | ((p[2] >> 48) == LREF_TAG) |
                  ((p[3] >> 48) == LREF_TAG);
        if (!any) continue;
        for (int j = 0; j < 4; ++j) scan_word(p + j, lo, len);
    }
    for (; p < end; ++p) scan_word(p, lo, len);
}

/* [s, e) without the ranges (the buried objects and the bookkeeping) */
static void scan_region(uintptr_t s, uintptr_t e)
{
    s = (s + 7) & ~(uintptr_t)7;
    size_t i = nranges && ranges[0].lo <= s ? range_at(s) : 0;
    while (s < e)
    {
        while (i < nranges && ranges[i].hi <= s) ++i;
        uintptr_t stop = i < nranges && ranges[i].lo < e ? ranges[i].lo : e;
        if (stop > s) scan_words((const uintptr_t *)s, (const uintptr_t *)(stop & ~(uintptr_t)7));
        if (stop >= e) break;
        s = (ranges[i].hi + 7) & ~(uintptr_t)7;
    }
}

/* [a, b), whole pages of one mapping, without its guard pages: glibc's
 * thread stacks (2.41 on) keep their guard inside the rw-p mapping with
 * MADV_GUARD_INSTALL, and a read of one faults. /proc/self/pagemap's bit 58
 * names them (Linux 6.15); without pagemap (pm < 0) the range is read whole,
 * as before. */
static void scan_unguarded(int pm, uintptr_t a, uintptr_t b)
{
    if (pm < 0)
    {
        scan_region(a, b);
        return;
    }
    uint64_t ent[128];
    uintptr_t run = a;   /* the start of the pages read so far unguarded */
    for (uintptr_t pg = a; pg < b;)
    {
        size_t k = (b - pg) >> 12;
        if (k > 128) k = 128;
        if (k == 0 || pread(pm, ent, k * sizeof *ent, (off_t)((pg >> 12) * sizeof *ent)) != (ssize_t)(k * sizeof *ent))
            break;
        for (size_t i = 0; i < k; ++i, pg += 4096)
            if (ent[i] >> 58 & 1)
            {
                if (pg > run) scan_region(run, pg);
                run = pg + 4096;
            }
    }
    if (b > run) scan_region(run, b);
}

/* [a, b) without the environments' arenas, whose pools are scanned slot by
 * slot up to their high-water marks instead of over the whole reservation.
 * The arenas are disjoint and ar is in address order, so one pass cuts them
 * out low to high: the same regions in the same order as cutting at each
 * arena and recursing on both sides, with no recursion on the tick's path
 * (make callgraph). */
static void scan_mapping(int pm, uintptr_t a, uintptr_t b, struct arena_env *const *ar, int nar)
{
    for (int i = 0; i < nar && a < b; ++i)
    {
        uintptr_t lo = (uintptr_t)ar[i]->a.base, hi = lo + ar[i]->a.size;
        if (hi <= a || lo >= b) continue;
        if (lo > a) scan_unguarded(pm, a, lo);
        a = hi;
    }
    if (a < b) scan_unguarded(pm, a, b);
}

/* The scan's reader of a region: scan_region, or scan_written's pagemap
 * filter (images_only's sweep: the image's heap and slab are mostly pages
 * never written or given back, gigabytes of address space) */
static _Thread_local int scan_pm = -1;

/* [a, b) without the pages that are neither present nor swapped out: a page
 * never written (or handed back, madvise) reads zero and holds no
 * reference. A page only read maps the zero page and is present: it is
 * scanned (and every page another process shares). Without pagemap the
 * range is read whole. */
static void scan_written(uintptr_t a, uintptr_t b)
{
    if (scan_pm < 0 || b - a < 8 * 4096)
    {
        scan_region(a, b);
        return;
    }
    uint64_t ent[512];
    uintptr_t run = a, pg = a & ~(uintptr_t)4095;
    int in = 1;   /* [run, pg) is to be scanned */
    while (pg < b)
    {
        size_t k = ((b - pg) + 4095) >> 12;
        if (k > 512) k = 512;
        if (pread(scan_pm, ent, k * sizeof *ent, (off_t)((pg >> 12) * sizeof *ent)) != (ssize_t)(k * sizeof *ent))
        {
            if (!in) run = pg;
            in = 1;
            pg = b;
            break;
        }
        for (size_t i = 0; i < k; ++i, pg += 4096)
        {
            int keep = (ent[i] >> 63 & 1) || (ent[i] >> 62 & 1);
            if (keep && !in)
            {
                run = pg > a ? pg : a;
                in = 1;
            }
            else if (!keep && in)
            {
                uintptr_t stop = pg > a ? pg : a;
                if (stop > run) scan_region(run, stop);
                in = 0;
            }
        }
    }
    if (in && b > run) scan_region(run, b);
}

static void scan_pools(struct arena_env **ar, int nar)
{
    for (int i = 0; i < nar; ++i)
    {
        /* the entities and the livings' parts */
        const struct slot_pool *pools[7] = { &ar[i]->livings, &ar[i]->an_ents, &ar[i]->ie_ents, &ar[i]->fh_ents,
                                             &ar[i]->ai_parts, &ar[i]->villager_parts, &ar[i]->player_parts };
        for (int k = 0; k < 7; ++k)
            if (pools[k]->top > 0)
                scan_written((uintptr_t)pools[k]->base, (uintptr_t)pools[k]->base + (size_t)pools[k]->top * pools[k]->size);
        /* the list slab to its high-water mark: what the tick keeps there
         * holds living references too (the replay's saved entities) */
        scan_written((uintptr_t)ar[i]->slab.base, (uintptr_t)ar[i]->slab.base + ar[i]->slab.used);
        /* an image's own struct env (but its scratch stack, empty between
         * rows) and its heap: the mapping is named, so the maps walk skips it */
        const struct env *e = (const struct env *)(const void *)ar[i];
        if (e->img.size)
        {
            uintptr_t lo = (uintptr_t)e, stk = (uintptr_t)e->scratch.stack;
            scan_written(lo, stk);
            scan_written(stk + sizeof e->scratch.stack, lo + sizeof *e);
            scan_written(lo + (uintptr_t)e->img.heap_off, lo + (uintptr_t)e->img.heap_off + e->img.heap_used);
        }
    }
}

/* the program's .data and .bss (the linker's symbols) */
extern char __data_start[], _end[];

__attribute__((noinline)) static void sweep(uintptr_t stack_from)
{
    size_t need = (5 * nrecs + 16) * sizeof *ranges + nrecs + nrecs * sizeof *work + (1 << 20)
                + (size_t)ARENA_LIVINGS * sizeof *slot_rec + nrecs * sizeof *rec_next;
    /* the page bitmap: sized below once the span is known */
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    for (size_t k = 0; k < nrecs; ++k)
    {
        uintptr_t rl[5], rh[5];
        int nr = rec_regions(&recs[k], rl, rh);
        for (int j = 0; j < nr; ++j)
        {
            if (rl[j] < lo) lo = rl[j];
            if (rh[j] > hi) hi = rh[j];
        }
    }
    /* the page filter, when the buried objects share one heap span */
    size_t npages = ((hi - lo) >> 12) + 2;
    if (npages > ((size_t)1 << 32)) npages = 0;
    need += npages / 8 + 64;
    if (need > scratch_len)
    {
        if (scratch) munmap(scratch, scratch_len);
        scratch_len = (need + 4095) & ~(size_t)4095;
        scratch_len += scratch_len / 2;
        scratch = area(scratch_len);
        if (scratch == NULL) { scratch_len = 0; return; }
    }
    unsigned char *s = scratch;
    ranges = (struct grave_range *)s; s += (5 * nrecs + 16) * sizeof *ranges;
    work = (int32_t *)s; s += nrecs * sizeof *work;
    slot_rec = (int32_t *)s; s += (size_t)ARENA_LIVINGS * sizeof *slot_rec;
    rec_next = (int32_t *)s; s += nrecs * sizeof *rec_next;
    marks = s; s += nrecs;
    pages = npages ? s : NULL; s += npages / 8 + 8;
    char *maps = (char *)s;
    size_t maps_cap = (size_t)(scratch + scratch_len - s) - 1;
    memset(marks, 0, nrecs);
    if (pages) memset(pages, 0, npages / 8 + 8);

    nranges = 0;
    for (size_t k = 0; k < nrecs; ++k)
    {
        uintptr_t rl[5], rh[5];
        int nr = rec_regions(&recs[k], rl, rh);
        for (int j = 0; j < nr; ++j) ranges[nranges++] = (struct grave_range){rl[j], rh[j], (int32_t)k};
    }
    memset(slot_rec, 0xff, (size_t)ARENA_LIVINGS * sizeof *slot_rec);
    for (size_t k = 0; k < nrecs; ++k)
    {
        rec_next[k] = -1;
        if (recs[k].l->part <= 0) continue;
        uint32_t slot = (uint32_t)(recs[k].l->part - 1);
        rec_next[k] = slot_rec[slot];
        slot_rec[slot] = (int32_t)k;
    }
    span_lo = lo;
    span_len = hi - lo;
    for (size_t i = 0; pages && i < nranges; ++i)
        for (uintptr_t pg = (ranges[i].lo - lo) >> 12; pg <= (ranges[i].hi - 1 - lo) >> 12; ++pg)
            pages[pg >> 3] |= (uint8_t)(1u << (pg & 7));
    /* the bookkeeping is never a root */
    ranges[nranges++] = (struct grave_range){(uintptr_t)recs, (uintptr_t)recs + caprecs * sizeof *recs, -1};
    ranges[nranges++] = (struct grave_range){(uintptr_t)scratch, (uintptr_t)scratch + scratch_len, -1};
    for (int i = 0; i < nignored; ++i) ranges[nranges++] = (struct grave_range){ignored[i].lo, ignored[i].hi, -1};
    sort_ranges(ranges, nranges);
    /* a living buried twice (revived and killed again): the second record
     * goes and the first is kept this sweep */
    nwork = 0;
    for (size_t i = 1; i < nranges; ++i)
        if (ranges[i].lo == ranges[i - 1].lo && ranges[i].rec >= 0 && ranges[i - 1].rec >= 0)
        {
            marks[ranges[i].rec] |= 2;
            ranges[i].rec = ranges[i - 1].rec;
            mark(ranges[i].rec);
        }

    /* grave_enable_images: the roots are the image's own memory (everything
     * the tick keeps is there; the caller's stack between rows holds no
     * living) */
    if (images_only && nw_env->img.size)
    {
        struct arena_env *self = &nw_env->arena;
        scan_pm = open("/proc/self/pagemap", O_RDONLY);
        scan_pools(&self, 1);
        if (scan_pm >= 0) close(scan_pm);
        scan_pm = -1;
        goto walk;
    }
    /* the roots: the program's data, and every writable private anonymous
     * mapping (the heap, malloc's mmaps, the stack from the caller's frame
     * up) */
    scan_region((uintptr_t)__data_start, (uintptr_t)_end);
    struct arena_env *ar[ARENA_MAX_LIVE];
    int nar = arena_live(ar);
    scan_pools(ar, nar);
    /* scan_mapping takes the arenas in address order */
    for (int i = 1; i < nar; ++i)
        for (int j = i; j > 0 && (uintptr_t)ar[j - 1]->a.base > (uintptr_t)ar[j]->a.base; --j)
        {
            struct arena_env *t = ar[j];
            ar[j] = ar[j - 1];
            ar[j - 1] = t;
        }
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) { nwork = 0; for (size_t k = 0; k < nrecs; ++k) marks[k] = 1; goto keep; }
    size_t len = 0;
    for (ssize_t n; len < maps_cap && (n = read(fd, maps + len, maps_cap - len)) > 0;) len += (size_t)n;
    close(fd);
    maps[len] = 0;
    int pm = open("/proc/self/pagemap", O_RDONLY);
    for (char *line = maps; line && *line;)
    {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        /* start-end perms offset dev inode path */
        char *p = line;
        uintptr_t a = strtoul(p, &p, 16);
        uintptr_t b = strtoul(p + 1, &p, 16);
        char perms[5] = "";
        memcpy(perms, p + 1, 4);
        strtoul(p + 6, &p, 16);
        while (*p == ' ') ++p;
        while (*p && *p != ' ') ++p;
        unsigned long inode = strtoul(p, &p, 10);
        while (*p == ' ') ++p;
        const char *path = p;
        if (perms[0] == 'r' && perms[1] == 'w' && perms[3] == 'p')
        {
            int is_stack = strcmp(path, "[stack]") == 0;
            if (is_stack)
            {
                if (stack_from > a && stack_from < b) a = stack_from & ~(uintptr_t)7;
                scan_region(a, b);
            }
            else if (inode == 0 && (path[0] == 0 || strcmp(path, "[heap]") == 0))
                scan_mapping(pm, a, b, ar, nar);
        }
        line = nl ? nl + 1 : NULL;
    }
    if (pm >= 0) close(pm);

walk:
    /* what a kept one points at is kept too */
    while (nwork)
    {
        int32_t r = work[--nwork];
        uintptr_t rl[5], rh[5];
        int nr = rec_regions(&recs[r], rl, rh);
        for (int j = 0; j < nr; ++j) scan_words((const uintptr_t *)rl[j], (const uintptr_t *)rh[j]);
    }

keep:;
    size_t kept = 0;
    for (size_t k = 0; k < nrecs; ++k)
    {
        if (marks[k] & 2) continue;
        if (marks[k]) { recs[kept++] = recs[k]; continue; }
        /* cleared first: memory malloc hands out again unzeroed must not
         * carry this one's pointers to others still buried */
        living_drop_paths(recs[k].l);
        living_release(recs[k].l);
        an_ent_release(recs[k].en);
    }
    nrecs = kept;
    buried_bytes = kept * LIVING_BYTES;
    /* a sweep that frees little waits for another threshold's worth */
    next_sweep = buried_bytes + threshold;
}

/* Callee-saved registers may hold the only copy of a pointer the caller
 * still uses: store them where the stack scan reads. */
__attribute__((noinline)) void grave_collect(void)
{
    if (!threshold || atomic_load(&held) || buried_bytes < (next_sweep ? next_sweep : threshold)) return;
    volatile uintptr_t regs[6];
    __asm__ volatile("mov %%rbx, 0(%0)\n\tmov %%rbp, 8(%0)\n\tmov %%r12, 16(%0)\n\t"
                     "mov %%r13, 24(%0)\n\tmov %%r14, 32(%0)\n\tmov %%r15, 40(%0)"
                     : : "r"(regs) : "memory");
    sweep((uintptr_t)regs);
    regs[0] = 0;
}

#else

void grave_collect(void)
{
}

#endif
