/* The heap allocation counter the phase profiler reads (phaseprof.c,
 * make phase-profile): preloaded as out/native/allocount.so, it counts every
 * malloc, calloc, realloc and aligned allocation of the process in
 * nw_alloc_calls and hands each to glibc's own. Not linked into any tool. */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

extern void *__libc_malloc(size_t n);
extern void *__libc_calloc(size_t n, size_t size);
extern void *__libc_realloc(void *p, size_t n);
extern void *__libc_memalign(size_t align, size_t n);

uint64_t nw_alloc_calls;

void *malloc(size_t n)
{
    ++nw_alloc_calls;
    return __libc_malloc(n);
}

void *calloc(size_t n, size_t size)
{
    ++nw_alloc_calls;
    return __libc_calloc(n, size);
}

void *realloc(void *p, size_t n)
{
    ++nw_alloc_calls;
    return __libc_realloc(p, n);
}

void *memalign(size_t align, size_t n)
{
    ++nw_alloc_calls;
    return __libc_memalign(align, n);
}

void *aligned_alloc(size_t align, size_t n)
{
    ++nw_alloc_calls;
    return __libc_memalign(align, n);
}

int posix_memalign(void **out, size_t align, size_t n)
{
    ++nw_alloc_calls;
    void *p = __libc_memalign(align, n);
    if (p == NULL) return ENOMEM;
    *out = p;
    return 0;
}
