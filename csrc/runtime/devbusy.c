/* The device's busy time from CUPTI's activity records (devbusy.h). */
#define _GNU_SOURCE
#include "devbusy.h"

#include <cupti.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* an interval of the device's: [start, end) ns, kind 0 a kernel, 1 a copy
 * or memset */
struct span { uint64_t start, end; int kind; };

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct span *spans;
static size_t nspans, capspans;
static uint64_t dropped;

enum { BUF_BYTES = 8 << 20 };

static void CUPTIAPI buf_request(uint8_t **buf, size_t *size, size_t *max_records)
{
    *buf = aligned_alloc(8, BUF_BYTES);
    *size = *buf ? BUF_BYTES : 0;
    *max_records = 0;
}

static void add(uint64_t start, uint64_t end, int kind)
{
    if (end <= start) return;
    if (nspans == capspans)
    {
        size_t cap = capspans ? 2 * capspans : 1 << 16;
        struct span *v = realloc(spans, cap * sizeof *v);
        if (v == NULL) { ++dropped; return; }
        spans = v;
        capspans = cap;
    }
    spans[nspans++] = (struct span){start, end, kind};
}

static void CUPTIAPI buf_complete(CUcontext ctx, uint32_t stream, uint8_t *buf, size_t size, size_t valid)
{
    (void)ctx;
    (void)stream;
    (void)size;
    CUpti_Activity *rec = NULL;
    pthread_mutex_lock(&mu);
    while (valid > 0 && cuptiActivityGetNextRecord(buf, valid, &rec) == CUPTI_SUCCESS)
    {
        switch (rec->kind)
        {
        case CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL:
        case CUPTI_ACTIVITY_KIND_KERNEL:
        {
            const CUpti_ActivityKernel12 *k = (const CUpti_ActivityKernel12 *)rec;
            add(k->start, k->end, 0);
            break;
        }
        case CUPTI_ACTIVITY_KIND_MEMCPY:
        {
            const CUpti_ActivityMemcpy6 *m = (const CUpti_ActivityMemcpy6 *)rec;
            add(m->start, m->end, 1);
            break;
        }
        case CUPTI_ACTIVITY_KIND_MEMSET:
        {
            const CUpti_ActivityMemset4 *m = (const CUpti_ActivityMemset4 *)rec;
            add(m->start, m->end, 1);
            break;
        }
        default:
            break;
        }
    }
    size_t lost = 0;
    if (cuptiActivityGetNumDroppedRecords(ctx, stream, &lost) == CUPTI_SUCCESS) dropped += lost;
    pthread_mutex_unlock(&mu);
    free(buf);
}

int devbusy_start(void)
{
    if (cuptiActivityRegisterCallbacks(buf_request, buf_complete) != CUPTI_SUCCESS) return -1;
    if (cuptiActivityEnable(CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL) != CUPTI_SUCCESS) return -1;
    if (cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMCPY) != CUPTI_SUCCESS) return -1;
    if (cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMSET) != CUPTI_SUCCESS) return -1;
    return 0;
}

uint64_t devbusy_now(void)
{
    uint64_t t = 0;
    cuptiGetTimestamp(&t);
    return t;
}

static int by_start(const void *a, const void *b)
{
    const struct span *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}

/* the union of the spans of KIND (-1: every kind) clipped to [t0, t1), ns;
 * spans sorted by start */
static uint64_t span_union(const struct span *v, size_t n, int kind, uint64_t t0, uint64_t t1)
{
    uint64_t total = 0, lo = 0, hi = 0;
    int open = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (kind >= 0 && v[i].kind != kind) continue;
        uint64_t s = v[i].start < t0 ? t0 : v[i].start, e = v[i].end > t1 ? t1 : v[i].end;
        if (e <= s) continue;
        if (open && s <= hi) { if (e > hi) hi = e; continue; }
        if (open) total += hi - lo;
        lo = s;
        hi = e;
        open = 1;
    }
    if (open) total += hi - lo;
    return total;
}

void devbusy_read(uint64_t t0, uint64_t t1, struct devbusy *out)
{
    memset(out, 0, sizeof *out);
    cuptiActivityFlushAll(CUPTI_ACTIVITY_FLAG_FLUSH_FORCED);
    pthread_mutex_lock(&mu);
    qsort(spans, nspans, sizeof *spans, by_start);
    uint64_t summed = 0;
    size_t keep = 0;
    for (size_t i = 0; i < nspans; ++i)
    {
        uint64_t s = spans[i].start < t0 ? t0 : spans[i].start, e = spans[i].end > t1 ? t1 : spans[i].end;
        if (e > s) { summed += e - s; ++out->records; }
    }
    out->busy_ms = (double)span_union(spans, nspans, -1, t0, t1) / 1e6;
    out->kernel_ms = (double)span_union(spans, nspans, 0, t0, t1) / 1e6;
    out->copy_ms = (double)span_union(spans, nspans, 1, t0, t1) / 1e6;
    out->summed_ms = (double)summed / 1e6;
    out->dropped = dropped;
    /* the spans that end after t1 stay for a later window */
    for (size_t i = 0; i < nspans; ++i)
        if (spans[i].end > t1) spans[keep++] = spans[i];
    nspans = keep;
    dropped = 0;
    pthread_mutex_unlock(&mu);
}
