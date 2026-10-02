/* See imgxfer.h. The tracker is Linux's (userfaultfd, PAGEMAP_SCAN); the
 * transfer format and the whole-image export are everywhere. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#include "imgxfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "env.h"
#include "image.h"

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/perf_event.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif
#endif

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

/* a mapping of len bytes, named on Linux: the grave's scan reads unnamed
 * writable mappings only, and a transfer holds the image's pointers */
void *img_map_named(size_t len, const char *name)
{
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return NULL;
#if defined(__linux__)
    (void)prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, (unsigned long)p, len, (unsigned long)name);
#else
    (void)name;
#endif
    return p;
}

void img_unmap(void *p, size_t len)
{
    if (p != NULL) munmap(p, len);
}

int img_resident(const void *p, size_t len, unsigned char *vec)
{
    /* the vector is unsigned char on Linux and char on the Mac */
    return mincore((void *)(uintptr_t)p, len, (void *)vec);
}

int img_xfer_init(struct img_xfer *x, uint64_t size)
{
    memset(x, 0, sizeof *x);
    x->cap = (size_t)(size / IMG_PAGE);
    x->bytes = img_map_named((size_t)size, "nw-xfer");
    x->page = img_map_named((x->cap * sizeof *x->page + IMG_PAGE - 1) & ~(size_t)(IMG_PAGE - 1), "nw-xfer");
    if (x->bytes == NULL || x->page == NULL)
    {
        img_xfer_free(x);
        return -1;
    }
    return 0;
}

void img_xfer_free(struct img_xfer *x)
{
    if (x->bytes) munmap(x->bytes, x->cap * IMG_PAGE);
    if (x->page) munmap(x->page, (x->cap * sizeof *x->page + IMG_PAGE - 1) & ~(size_t)(IMG_PAGE - 1));
    memset(x, 0, sizeof *x);
}

void img_xfer_trim(struct img_xfer *x)
{
    if (x->bytes) madvise(x->bytes, x->cap * IMG_PAGE, MADV_DONTNEED);
    if (x->page) madvise(x->page, (x->cap * sizeof *x->page + IMG_PAGE - 1) & ~(size_t)(IMG_PAGE - 1), MADV_DONTNEED);
}

static int page_zero(const unsigned char *p)
{
    const uint64_t *w = (const uint64_t *)(const void *)p;
    uint64_t any = 0;
    for (int k = 0; k < IMG_PAGE / 8; ++k) any |= w[k];
    return any == 0;
}

static int add_page(struct img_xfer *x, const unsigned char *base, size_t pg)
{
    if (x->npages >= x->cap) return -1;
    x->page[x->npages++] = (uint32_t)pg;
    memcpy(x->bytes + x->ndata++ * IMG_PAGE, base + pg * IMG_PAGE, IMG_PAGE);
    return 0;
}

struct pg_range {
    size_t lo, hi;            /* pages [lo, hi) */
};

static int range_cmp(const void *a, const void *b)
{
    const struct pg_range *x = a, *y = b;
    return x->lo < y->lo ? -1 : x->lo > y->lo;
}

/* every nonzero page of the live extents, in page order */
static long export_full(struct env *e, struct img_xfer *x)
{
    struct img_extent ext[64];
    struct pg_range r[64];
    int n = image_extents(e, ext, 64);
    const unsigned char *base = (const unsigned char *)e;

    for (int i = 0; i < n; ++i)
        r[i] = (struct pg_range){ext[i].off / IMG_PAGE, (ext[i].off + ext[i].len + IMG_PAGE - 1) / IMG_PAGE};
    qsort(r, (size_t)n, sizeof *r, range_cmp);

    /* a page that holds no memory reads zero, and reading it would map the
     * zero page there (which a tracker then takes for a write) */
    unsigned char mc[1024];
    size_t next = 0;          /* the first page not yet looked at */
    for (int i = 0; i < n; ++i)
        for (size_t pg = r[i].lo > next ? r[i].lo : next; pg < r[i].hi;)
        {
            size_t m = r[i].hi - pg < sizeof mc ? r[i].hi - pg : sizeof mc;
            if (img_resident(base + pg * IMG_PAGE, m * IMG_PAGE, mc) != 0) memset(mc, 1, m);
            for (size_t k = 0; k < m; ++k)
                if ((mc[k] & 1) && !page_zero(base + (pg + k) * IMG_PAGE) && add_page(x, base, pg + k) != 0) return -1;
            pg += m;
            next = pg;
        }
    return (long)x->npages;
}

#if defined(__linux__)

struct img_track {
    int uffd, pagemap;
    pthread_t reader;
    int reader_on;
    void *reader_stack;
    struct page_region vec[4096];
    unsigned char mc[4096];   /* mincore's answer for a run of pages */
};

/* glibc puts the thread's static TLS (the harnesses keep megabytes there) on it */
#define READER_STACK ((size_t)64 << 20)

/* mremap of a registered mapping waits until its UFFD_EVENT_REMAP is read */
static void *reader_main(void *arg)
{
    struct img_track *t = arg;
    struct uffd_msg m;
    for (;;)
    {
        ssize_t r = read(t->uffd, &m, sizeof m);
        if (r < 0 && errno != EINTR && errno != EAGAIN) break;
    }
    return NULL;
}

struct img_track *img_track_open(char *err, size_t errlen)
{
    struct img_track *t = proc_calloc(1, sizeof *t);
    if (t == NULL)
    {
        snprintf(err, errlen, "no memory");
        return NULL;
    }
    t->uffd = (int)syscall(SYS_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
    if (t->uffd < 0)
    {
        snprintf(err, errlen, "userfaultfd: %s", strerror(errno));
        proc_free(t);
        return NULL;
    }
    struct uffdio_api api = {.api = UFFD_API, .features = UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_EVENT_REMAP};
    if (ioctl(t->uffd, UFFDIO_API, &api) != 0)
    {
        snprintf(err, errlen, "userfaultfd API (asynchronous write-protect, remap events): %s", strerror(errno));
        close(t->uffd);
        proc_free(t);
        return NULL;
    }
    t->pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    if (t->pagemap < 0)
    {
        snprintf(err, errlen, "/proc/self/pagemap: %s", strerror(errno));
        close(t->uffd);
        proc_free(t);
        return NULL;
    }
    /* the reader's stack is a named mapping without a guard page: the grave
     * reads every unnamed writable mapping */
    t->reader_stack = img_map_named(READER_STACK, "nw-track-stack");
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    int rc = t->reader_stack == NULL ? ENOMEM : pthread_attr_setstack(&attr, t->reader_stack, READER_STACK);
    if (rc == 0) rc = pthread_create(&t->reader, &attr, reader_main, t);
    if (rc != 0)
    {
        snprintf(err, errlen, "the tracker's event thread does not start: %s", strerror(rc));
        pthread_attr_destroy(&attr);
        img_track_close(t);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    t->reader_on = 1;
    return t;
}

void img_track_close(struct img_track *t)
{
    if (t == NULL) return;
    if (t->reader_on)
    {
        pthread_cancel(t->reader);
        pthread_join(t->reader, NULL);
    }
    if (t->reader_stack) munmap(t->reader_stack, READER_STACK);
    if (t->pagemap >= 0) close(t->pagemap);
    if (t->uffd >= 0) close(t->uffd);
    proc_free(t);
}

static long scan_written(struct img_track *t, struct env *e, struct img_xfer *x);

static int protect(struct img_track *t, uintptr_t lo, size_t len)
{
    struct uffdio_writeprotect w = {.range = {.start = lo, .len = len}, .mode = UFFDIO_WRITEPROTECT_MODE_WP};
    return ioctl(t->uffd, UFFDIO_WRITEPROTECT, &w);
}

static int unprotect(struct img_track *t, uintptr_t lo, size_t len)
{
    struct uffdio_writeprotect w = {.range = {.start = lo, .len = len}, .mode = 0};
    return ioctl(t->uffd, UFFDIO_WRITEPROTECT, &w);
}

int img_track_add(struct img_track *t, struct env *e, char *err, size_t errlen)
{
    uintptr_t lo = (uintptr_t)e;
    struct uffdio_register r = {.range = {.start = lo, .len = e->img.size}, .mode = UFFDIO_REGISTER_MODE_WP};
    if (!e->img.size)
    {
        snprintf(err, errlen, "not an image");
        return -1;
    }
    if (ioctl(t->uffd, UFFDIO_REGISTER, &r) != 0)
    {
        snprintf(err, errlen, "userfaultfd register: %s", strerror(errno));
        return -1;
    }
    /* protect what is there with one scan: a write-protect of the whole
     * range would build page tables for all of it (asynchronous mode implies
     * UFFD_FEATURE_WP_UNPOPULATED: 64 MB of them for the 32 GB heap
     * reservation, and every scan would walk them), while the scan protects
     * present pages only and leaves the holes alone; a page first written in
     * a hole comes in unprotected, so it reads as written */
    if (scan_written(t, e, NULL) < 0)
    {
        snprintf(err, errlen, "PAGEMAP_SCAN: %s", strerror(errno));
        return -1;
    }
    e->img.track = 1;
    e->img.track_fd = t->uffd;
    e->img.nzap = 0;
    return 0;
}

/* the pages written since the last scan, protected again: their indices
 * into x->page, or counted when x is NULL */
static long scan_written(struct img_track *t, struct env *e, struct img_xfer *x)
{
    uintptr_t lo = (uintptr_t)e, hi = lo + e->img.size;
    long pages = 0;
    struct pm_scan_arg a = {
        .size = sizeof a,
        .flags = PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
        .start = lo,
        .end = hi,
        .vec = (uintptr_t)t->vec,
        .vec_len = sizeof t->vec / sizeof *t->vec,
        .category_mask = PAGE_IS_WRITTEN,
        .return_mask = PAGE_IS_WRITTEN,
    };
    for (;;)
    {
        long n = ioctl(t->pagemap, PAGEMAP_SCAN, &a);
        if (n < 0) return -1;
        for (long i = 0; i < n; ++i)
            for (uintptr_t p = t->vec[i].start; p < t->vec[i].end; p += IMG_PAGE)
            {
                if (x != NULL)
                {
                    if (x->npages >= x->cap) return -1;
                    x->page[x->npages++] = (uint32_t)((p - lo) / IMG_PAGE);
                }
                ++pages;
            }
        if (a.walk_end >= hi || n < (long)a.vec_len) break;
        a.start = a.walk_end;
    }
    return pages;
}

static int in_zap_log(const struct env *e, uint32_t pg)
{
    uint64_t off = (uint64_t)pg * IMG_PAGE;
    for (uint32_t k = 0; k < e->img.nzap; ++k)
        if (off - e->img.zap[k].off < e->img.zap[k].len) return 1;
    return 0;
}

static int u32_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* the written pages and the zap log's (image.h: handed back, so they read
 * zero and the scan does not see them), in page order, then their bytes */
static long export_delta(struct env *e, struct img_xfer *x, struct img_track *t)
{
    if (scan_written(t, e, x) < 0) return -1;
    size_t n = x->npages, m = 0;
    for (uint32_t k = 0; k < e->img.nzap; ++k) m += e->img.zap[k].len / IMG_PAGE;
    if (m)
    {
        /* the log's pages at the array's end, sorted, then merged in from the back */
        if (n + m > x->cap) return -1;
        uint32_t *z = x->page + x->cap - m;
        size_t j = 0;
        for (uint32_t k = 0; k < e->img.nzap; ++k)
            for (uint64_t p = e->img.zap[k].off / IMG_PAGE, q = p + e->img.zap[k].len / IMG_PAGE; p < q; ++p)
                z[j++] = (uint32_t)p;
        qsort(z, m, sizeof *z, u32_cmp);
        size_t u = 0;
        for (size_t i = 0; i < m; ++i)
            if (u == 0 || z[u - 1] != z[i]) z[u++] = z[i];
        /* drop the ones the scan has */
        size_t w = 0;
        for (size_t i = 0, a = 0; i < u; ++i)
        {
            while (a < n && x->page[a] < z[i]) ++a;
            if (a < n && x->page[a] == z[i]) continue;
            z[w++] = z[i];
        }
        memmove(x->page + x->cap - w, z, w * sizeof *z);
        z = x->page + x->cap - w;
        size_t out = n + w, i = n, k = w;
        while (k > 0)
        {
            if (i > 0 && x->page[i - 1] > z[k - 1]) x->page[--out] = x->page[--i];
            else x->page[--out] = z[--k];
        }
        x->npages = n + w;
    }
    const unsigned char *base = (const unsigned char *)e;
    /* A page the scan reports that holds no memory and is not in the zap
     * log was never written or was emptied by an import (a sync): zero on
     * both ends, so it is dropped. (The scan reports an empty entry of a
     * page table it walks once, and an import's zero pages come in
     * unprotected so their tables can go.) */
    size_t w = 0;
    for (size_t k = 0; k < x->npages;)
    {
        size_t j = k + 1;
        while (j < x->npages && x->page[j] == x->page[j - 1] + 1 && j - k < sizeof t->mc) ++j;
        if (img_resident(base + (size_t)x->page[k] * IMG_PAGE, (j - k) * IMG_PAGE, t->mc) != 0)
            memset(t->mc, 1, j - k);
        for (size_t i = k; i < j; ++i)
            if ((t->mc[i - k] & 1) || in_zap_log(e, x->page[i])) x->page[w++] = x->page[i];
        k = j;
    }
    x->npages = w;
    e->img.nzap = 0;
    for (size_t k = 0; k < x->npages; ++k)
    {
        const unsigned char *p = base + (size_t)x->page[k] * IMG_PAGE;
        if (page_zero(p)) x->page[k] |= IMG_XFER_ZERO;
        else memcpy(x->bytes + x->ndata++ * IMG_PAGE, p, IMG_PAGE);
    }
    return (long)x->npages;
}

#else

struct img_track {
    int none;
};

struct img_track *img_track_open(char *err, size_t errlen)
{
    snprintf(err, errlen, "no write tracker on this system (Linux userfaultfd)");
    return NULL;
}

void img_track_close(struct img_track *t) { (void)t; }

int img_track_add(struct img_track *t, struct env *e, char *err, size_t errlen)
{
    (void)t;
    (void)e;
    snprintf(err, errlen, "no write tracker on this system");
    return -1;
}

static int protect(struct img_track *t, uintptr_t lo, size_t len)
{
    (void)t;
    (void)lo;
    (void)len;
    return -1;
}

static int unprotect(struct img_track *t, uintptr_t lo, size_t len)
{
    (void)t;
    (void)lo;
    (void)len;
    return -1;
}

static long export_delta(struct env *e, struct img_xfer *x, struct img_track *t)
{
    (void)e;
    (void)x;
    (void)t;
    return -1;
}

#endif

long env_export(struct env *e, struct img_xfer *x, struct img_track *t)
{
    if (!e->img.size || x->cap < e->img.size / IMG_PAGE) return -1;
    x->base = (uint64_t)(uintptr_t)e;
    x->size = e->img.size;
    x->npages = x->ndata = 0;
    x->full = t == NULL;
    if (t == NULL) e->img.nzap = 0;
    return t == NULL ? export_full(e, x) : export_delta(e, x, t);
}

/* zero pages [lo, hi) of base: handed back, so they cost no memory, and
 * unprotected first, so a page table left empty goes too (a hole reads as
 * unwritten; a protected empty page would keep a marker and its table) */
static void zero_run(unsigned char *base, size_t lo, size_t hi, struct img_track *t)
{
    unsigned char *p = base + lo * IMG_PAGE;
    size_t len = (hi - lo) * IMG_PAGE;
    if (t != NULL) (void)unprotect(t, (uintptr_t)p, len);
#if defined(__linux__)
    if (madvise(p, len, MADV_DONTNEED) == 0) return;
#endif
    /* elsewhere MADV_DONTNEED may keep the bytes */
    memset(p, 0, len);
}

long env_import(struct env *e, const struct img_xfer *x, struct img_track *t)
{
    unsigned char *base = (unsigned char *)e;

    if (!e->img.size || x->size != e->img.size) return -1;
    size_t data = 0, zlo = 0, zhi = 0;
    for (size_t k = 0; k < x->npages; ++k)
    {
        size_t pg = x->page[k] & ~IMG_XFER_ZERO;
        if (x->page[k] & IMG_XFER_ZERO)
        {
            if (zhi != pg)
            {
                if (zhi > zlo) zero_run(base, zlo, zhi, t);
                zlo = pg;
            }
            zhi = pg + 1;
            continue;
        }
        memcpy(base + pg * IMG_PAGE, x->bytes + data++ * IMG_PAGE, IMG_PAGE);
    }
    if (zhi > zlo) zero_run(base, zlo, zhi, t);
    /* the pages with bytes are in step: protected again */
    if (t != NULL)
        for (size_t k = 0; k < x->npages;)
        {
            if (x->page[k] & IMG_XFER_ZERO)
            {
                ++k;
                continue;
            }
            size_t j = k + 1;
            while (j < x->npages && !(x->page[j] & IMG_XFER_ZERO) && x->page[j] == x->page[j - 1] + 1) ++j;
            if (protect(t, (uintptr_t)base + (size_t)x->page[k] * IMG_PAGE, (j - k) * IMG_PAGE) != 0) return -1;
            k = j;
        }
    return (long)x->npages;
}

#if defined(__linux__)

static int perf_open(int exclude_user, int group)
{
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.type = PERF_TYPE_HARDWARE;
    a.size = sizeof a;
    a.config = PERF_COUNT_HW_INSTRUCTIONS;
    a.exclude_hv = 1;
    a.exclude_kernel = !exclude_user;
    a.exclude_user = exclude_user;
    a.read_format = PERF_FORMAT_GROUP;
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, group, 0);
}

void img_counters_open(struct img_counters *c)
{
    c->fd = perf_open(0, -1);
    c->n = c->fd >= 0;
    if (c->fd >= 0 && perf_open(1, c->fd) >= 0) c->n = 2;
}

void img_counters_read(const struct img_counters *c, uint64_t out[2])
{
    struct {
        uint64_t nr, v[2];
    } r = {0, {0, 0}};
    out[0] = out[1] = 0;
    if (c->fd < 0 || read(c->fd, &r, sizeof r) < (ssize_t)(8 + 8 * (size_t)c->n)) return;
    out[0] = r.v[0];
    if (c->n > 1) out[1] = r.v[1];
}

#else

void img_counters_open(struct img_counters *c)
{
    c->fd = -1;
    c->n = 0;
}

void img_counters_read(const struct img_counters *c, uint64_t out[2])
{
    (void)c;
    out[0] = out[1] = 0;
}

#endif
