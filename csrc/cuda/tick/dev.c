/* The device side of the engine's C on the GPU (GPU plan L17): the kernel
 * that runs S6 (servertick_chunk_loop) for a batch of environments, one
 * thread each, and the runtime the nw-dev pass (cuda/tick/nwpass.cpp) calls
 * into: pointer translation at every pointer load and store, and the write
 * barrier's slow halves (the fast halves, inlined everywhere, are
 * cuda/tick/inl.c). Compiled for nvptx64 without nw-rename, so every name here
 * the pass must leave alone starts nwdev_ (the C library's names below it
 * instruments like the engine's); linked with the engine (cuda/tick/tick.mk). The
 * host side is cuda/tick/host.c.
 *
 * Every environment's image sits on the device at the host's own address
 * (host.c maps it there), so an image pointer needs nothing. Memory holds
 * a global's or a function's host address; a register holds the device's.
 * nwdev_in maps a loaded host address to the device's, nwdev_out a stored
 * device address to the host's, from the tables host.c fills from the
 * host's symbol table (nwdev_syms, emitted by the pass).
 *
 * nwdev_wb marks each image page the block's environment writes in its
 * dirty bitmap (what comes back to the host), and flags a write into a
 * granule the device holds no memory of its own for (the child maps one
 * zero granule of the environment's there): the run is then redone in C. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../engine/env.h"
#include "../../engine/servertick.h"
#include "dev.h"

struct env *__attribute__((address_space(3))) nwdev_envs[NWDEV_PACK];
struct nwdev_job *__attribute__((address_space(3))) nwdev_curs[NWDEV_PACK];
uint64_t __attribute__((address_space(3))) nwdev_bases[NWDEV_PACK], nwdev_sizes[NWDEV_PACK];

/* the translation tables, set by the host before the first launch */
struct nwdev_tabs nwdev_tabs;

#define NWDEV_TID() __nvvm_read_ptx_sreg_tid_x()
#define NWDEV_BID() __nvvm_read_ptx_sreg_ctaid_x()

static void nwdev_flag(uint32_t why, uint64_t at)
{
    struct nwdev_job *j = nwdev_cur;
    if (j->fault == 0) j->fault_addr = at;
    j->fault |= why;
}

/* the entry of a sorted map holding p, or NULL */
static const struct nwdev_map *nwdev_find(const struct nwdev_map *m, uint32_t n, uint64_t p)
{
    uint32_t lo = 0, hi = n;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;
        if (m[mid].from <= p) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return NULL;
    const struct nwdev_map *e = &m[lo - 1];
    return p - e->from < (e->size ? e->size : 1) ? e : NULL;
}

/* a call through a host function pointer the device has no function for
 * (nwdev_callee): flagged by nwdev_in already; the block's thread ends */
__attribute__((noinline)) static void nwdev_exit_thread(void) { __asm__ volatile("exit;"); }

/* the slow halves of inl.c's nwdev_in, nwdev_out and nwdev_wb */
__attribute__((noinline)) void *nwdev_in_slow(uint64_t p)
{
    const struct nwdev_map *e = nwdev_find(nwdev_tabs.in, nwdev_tabs.nin, p);
    if (e != NULL) return (void *)(e->to + (p - e->from));
    /* a host address with no device counterpart: reading it would fault,
     * so the run reads the scratch page instead and is redone in C */
    nwdev_flag(NWDEV_F_IN, p);
    return nwdev_cur->scratch;
}

__attribute__((noinline)) void *nwdev_out_slow(void *v, void *dst)
{
    uint64_t p = (uint64_t)v;
    const struct nwdev_map *e = nwdev_find(nwdev_tabs.out, nwdev_tabs.nout, p);
    if (e != NULL) return (void *)(e->to + (p - e->from));
    /* a device-only constant or function (one the optimizer made, or one
     * whose host twin differs): fine on the stack, but the host would read
     * it from the image, so the run is redone in C */
    if ((uint64_t)dst - nwdev_base < nwdev_size) nwdev_flag(NWDEV_F_OUT, p);
    return v;
}

__attribute__((noinline)) void nwdev_wb_slow(void *v, uint64_t n)
{
    uint64_t off = (uint64_t)v - nwdev_base;
    if (n == 0) return;
    struct nwdev_job *j = nwdev_cur;
    uint64_t last = off + n - 1;
    if (last >= nwdev_size) last = nwdev_size - 1;
    for (uint64_t g = off >> 21; g <= last >> 21; ++g)
        if (!((j->real[g >> 5] >> (g & 31)) & 1))
        {
            /* the run is redone in C: it ends here, before the write and
             * before its pages are listed (past the extents' margins
             * nothing is mapped: a pool's or the slab's growth that reached
             * there faulted the whole context, S4's falling-block adoption
             * in fight-skeleton-s1, and the download's gather after it) */
            nwdev_flag(NWDEV_F_UNBACKED, (uint64_t)v);
            nwdev_exit_thread();
        }
    for (uint64_t pg = off >> 12; pg <= last >> 12; ++pg)
    {
        uint32_t *w = &j->dirty[pg >> 5], bit = 1u << (pg & 31);
        if (*w & bit) continue;
        *w |= bit;
        if (j->ndl < j->dlcap) j->dlist[j->ndl++] = (uint32_t)pg;
        else nwdev_flag(NWDEV_F_DLIST, (uint64_t)v);
    }
}

__attribute__((noinline)) void nwdev_bad_call(void)
{
    nwdev_exit_thread();
}

/* a function the device build cannot run (the pass gave it this body,
 * which returns zero): the run goes on to its end and is redone in C; the
 * name is the flagged address */
void nwdev_unsupported(const char *name)
{
    nwdev_flag(NWDEV_F_UNSUPPORTED, (uint64_t)name);
}

/* One thread per job, up to NWDEV_PACK a block (host.c's pack: 1 by
 * default, a warp to itself): the job's phase of its world (S6, or another
 * one servertick_phase runs). */
__attribute__((nvptx_kernel)) void chunkloop(struct nwdev_job *jobs, uint32_t n)
{
    uint32_t k = NWDEV_BID() * __nvvm_read_ptx_sreg_ntid_x() + NWDEV_TID();
    if (k >= n) return;
    struct nwdev_job *j = &jobs[k];
    nwdev_cur = j;
    nw_env = (struct env *)j->env;
    nwdev_base = j->env;
    nwdev_size = nw_env->img.size;
    j->fault = 0;
    j->writes = 0;
    if (!servertick_phase((struct servertick *)j->st, (int)j->phase)) nwdev_flag(NWDEV_F_UNSUPPORTED, 0);
    j->done = 1;
}

/* The host side's page movers, one block of 256 threads per page, each
 * over a whole launch's environments: the upload's scatter (stage page
 * src[k] to the image address addr[k], or zeros), the download's gather
 * (the page at addr[k] to stage page k) and the dirty bits' reset (the
 * dirty words at word[k]). */
__attribute__((nvptx_kernel)) void tick_scatter(const uint64_t *addr, const int64_t *src, const uint64_t *stage)
{
    uint32_t k = NWDEV_BID(), t = NWDEV_TID();
    uint64_t *dst = (uint64_t *)addr[k];
    int64_t s = src[k];
    for (uint32_t i = t; i < 512; i += 256) dst[i] = s < 0 ? 0 : stage[(uint64_t)s * 512 + i];
}

__attribute__((nvptx_kernel)) void tick_gather(const uint64_t *addr, uint64_t *stage)
{
    uint32_t k = NWDEV_BID(), t = NWDEV_TID();
    const uint64_t *from = (const uint64_t *)addr[k];
    for (uint32_t i = t; i < 512; i += 256) stage[(uint64_t)k * 512 + i] = from[i];
}

__attribute__((nvptx_kernel)) void tick_clear(const uint64_t *word, uint32_t n)
{
    uint32_t k = NWDEV_BID() * 256 + NWDEV_TID();
    if (k < n) *(uint32_t *)word[k] = 0;   /* every bit of the word is in the list */
}

/* The engine's memcpy, memmove and memset of 128 bytes or more (or of an
 * unknown length): nw-dev calls these in place of the intrinsics, which the
 * NVPTX backend makes byte loops; a word at a time where both ends are
 * aligned to it. The barrier ran at the call site. */
void *nwdev_memcpy(void *d, const void *s, uint64_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    uint64_t a = (uint64_t)dp | (uint64_t)sp;
    if ((a & 7) == 0)
        for (; n >= 8; n -= 8, dp += 8, sp += 8) *(uint64_t *)dp = *(const uint64_t *)sp;
    else if ((a & 3) == 0)
        for (; n >= 4; n -= 4, dp += 4, sp += 4) *(uint32_t *)dp = *(const uint32_t *)sp;
    for (; n; --n) *dp++ = *sp++;
    return d;
}

void *nwdev_memmove(void *d, const void *s, uint64_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp <= sp || dp >= sp + n) return nwdev_memcpy(d, s, n);
    /* backwards: the destination overlaps the source's end */
    dp += n;
    sp += n;
    uint64_t a = (uint64_t)dp | (uint64_t)sp | n;
    if ((a & 7) == 0)
        for (; n >= 8; n -= 8) { dp -= 8; sp -= 8; *(uint64_t *)dp = *(const uint64_t *)sp; }
    for (; n; --n) *--dp = *--sp;
    return d;
}

void *nwdev_memset(void *d, int c, uint64_t n)
{
    unsigned char *dp = d;
    uint64_t v = (uint64_t)(unsigned char)c * 0x0101010101010101ull;
    if (((uint64_t)dp & 7) == 0)
        for (; n >= 8; n -= 8, dp += 8) *(uint64_t *)dp = v;
    for (; n; --n) *dp++ = (unsigned char)c;
    return d;
}

/* ------------------------------------------------ the C library it reaches */

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; ++i)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) ++a, ++b;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; --n, ++a, ++b)
    {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

char *strchr(const char *s, int c)
{
    for (;; ++s)
    {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

/* what the tick prints (before an abort, as a rule): the format is kept in
 * the job for the host's note */
int fprintf(FILE *f, const char *fmt, ...)
{
    (void)f;
    nwdev_cur->msg = (uint64_t)fmt;
    return 0;
}
int snprintf(char *s, size_t n, const char *fmt, ...) { (void)fmt; if (n) s[0] = 0; return 0; }
int fputs(const char *s, FILE *f) { (void)s; (void)f; return 0; }
int fflush(FILE *f) { (void)f; return 0; }
FILE *stderr, *stdout;

/* abort() and exit() end the block's thread: the run is flagged, and C
 * redoes it (and aborts there too if the tick is wrong, not the device) */
void abort(void)
{
    nwdev_flag(NWDEV_F_ABORT, nwdev_cur->msg);
    nwdev_exit_thread();
    __builtin_unreachable();
}

void exit(int rc)
{
    (void)rc;
    nwdev_flag(NWDEV_F_ABORT, nwdev_cur->msg);
    nwdev_exit_thread();
    __builtin_unreachable();
}

struct timespec;
int clock_gettime(int clk, struct timespec *ts) { (void)clk; (void)ts; return -1; }

/* image.c hands a freed block's pages back with MADV_DONTNEED, which reads
 * zero afterwards: here the zeros are written */
int madvise(void *p, size_t n, int advice)
{
    if (advice == 4 /* MADV_DONTNEED */) __builtin_memset(p, 0, n);
    return 0;
}

void *mmap(void *p, size_t n, int prot, int flags, int fd, long off)
{
    (void)p; (void)n; (void)prot; (void)flags; (void)fd; (void)off;
    return (void *)-1;
}

int munmap(void *p, size_t n) { (void)p; (void)n; return 0; }

/* the libm the tick reaches that has one right answer: sqrt is correctly
 * rounded on both, fmod exact (musl's, by the bits). The rest (sin, cos,
 * atan2: glibc's own roundings) stay out, and a run that reaches them is
 * redone in C. The device's engine is compiled with -fmath-errno, so these
 * are calls, not intrinsics the backend would expand its own way. */
double sqrt(double x) { return __builtin_sqrt(x); }
float sqrtf(float x) { return __builtin_sqrtf(x); }

double fmod(double x, double y)
{
    union { double f; uint64_t i; } ux = {x}, uy = {y};
    int ex = ux.i >> 52 & 0x7ff, ey = uy.i >> 52 & 0x7ff;
    int sx = ux.i >> 63;
    uint64_t i, uxi = ux.i;
    if (uy.i << 1 == 0 || __builtin_isnan(y) || ex == 0x7ff) return (x * y) / (x * y);
    if (uxi << 1 <= uy.i << 1)
    {
        if (uxi << 1 == uy.i << 1) return 0 * x;
        return x;
    }
    if (!ex)
    {
        for (i = uxi << 12; i >> 63 == 0; ex--, i <<= 1) {}
        uxi <<= -ex + 1;
    }
    else
    {
        uxi &= -1ULL >> 12;
        uxi |= 1ULL << 52;
    }
    if (!ey)
    {
        for (i = uy.i << 12; i >> 63 == 0; ey--, i <<= 1) {}
        uy.i <<= -ey + 1;
    }
    else
    {
        uy.i &= -1ULL >> 12;
        uy.i |= 1ULL << 52;
    }
    for (; ex > ey; ex--)
    {
        i = uxi - uy.i;
        if (i >> 63 == 0)
        {
            if (i == 0) return 0 * x;
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - uy.i;
    if (i >> 63 == 0)
    {
        if (i == 0) return 0 * x;
        uxi = i;
    }
    for (; uxi >> 52 == 0; uxi <<= 1, ex--) {}
    if (ex > 0)
    {
        uxi -= 1ULL << 52;
        uxi |= (uint64_t)ex << 52;
    }
    else uxi >>= -ex + 1;
    uxi |= (uint64_t)sx << 63;
    ux.i = uxi;
    return ux.f;
}

float fmodf(float x, float y)
{
    union { float f; uint32_t i; } ux = {x}, uy = {y};
    int ex = ux.i >> 23 & 0xff, ey = uy.i >> 23 & 0xff;
    uint32_t sx = ux.i & 0x80000000, i, uxi = ux.i;
    if (uy.i << 1 == 0 || __builtin_isnan(y) || ex == 0xff) return (x * y) / (x * y);
    if (uxi << 1 <= uy.i << 1)
    {
        if (uxi << 1 == uy.i << 1) return 0 * x;
        return x;
    }
    if (!ex)
    {
        for (i = uxi << 9; i >> 31 == 0; ex--, i <<= 1) {}
        uxi <<= -ex + 1;
    }
    else
    {
        uxi &= -1U >> 9;
        uxi |= 1U << 23;
    }
    if (!ey)
    {
        for (i = uy.i << 9; i >> 31 == 0; ey--, i <<= 1) {}
        uy.i <<= -ey + 1;
    }
    else
    {
        uy.i &= -1U >> 9;
        uy.i |= 1U << 23;
    }
    for (; ex > ey; ex--)
    {
        i = uxi - uy.i;
        if (i >> 31 == 0)
        {
            if (i == 0) return 0 * x;
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - uy.i;
    if (i >> 31 == 0)
    {
        if (i == 0) return 0 * x;
        uxi = i;
    }
    for (; uxi >> 23 == 0; uxi <<= 1, ex--) {}
    if (ex > 0)
    {
        uxi -= 1U << 23;
        uxi |= (uint32_t)ex << 23;
    }
    else uxi >>= -ex + 1;
    uxi |= sx;
    ux.i = uxi;
    return ux.f;
}
