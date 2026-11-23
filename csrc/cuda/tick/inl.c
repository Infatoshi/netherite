/* The fast halves of the three calls the nw-dev pass puts at every pointer
 * load (nwdev_in), pointer store (nwdev_out) and write (nwdev_wb): linked
 * into each part of the device module before the pass runs on it, and
 * inlined there; the slow halves are cuda/tick/dev.c's. Compiled without
 * nw-rename (the pass finds these by name). */
#include <stdint.h>

#include "dev.h"

extern struct nwdev_tabs nwdev_tabs;
void *nwdev_in_slow(uint64_t p);
void *nwdev_out_slow(void *v, void *dst);
void nwdev_wb_slow(void *v, uint64_t n);
void nwdev_bad_call(void);

/* a host global's or function's address becomes the device's */
__attribute__((always_inline, used)) static void *nwdev_in(void *v)
{
    uint64_t p = (uint64_t)v;
    if (p - nwdev_tabs.in_lo >= nwdev_tabs.in_span) return v;
    return nwdev_in_slow(p);
}

/* a device global's or function's address, stored at dst, becomes the
 * host's */
__attribute__((always_inline, used)) static void *nwdev_out(void *v, void *dst)
{
    uint64_t p = (uint64_t)v;
    /* an image pointer (image.c's window, 2^44 up) is the same on both */
    if ((p >> 44) == 1) return v;
    if (p - nwdev_tabs.gl_lo < nwdev_tabs.gl_span) return nwdev_out_slow(v, dst);
    if (p - nwdev_tabs.fn_lo >= nwdev_tabs.fn_span) return v;
    for (uint64_t h = (p * 0x9e3779b97f4a7c15ull) >> 40;; ++h)
    {
        const uint64_t *e = &nwdev_tabs.fn_hash[2 * (h & nwdev_tabs.fn_mask)];
        if (e[0] == p) return (void *)e[1];
        if (e[0] == 0) return v;
    }
}

/* a write to the image: a page already written this run needs nothing more
 * (its granule was checked when it was first written) */
__attribute__((always_inline, used)) static void *nwdev_wb(void *v, uint64_t n)
{
    uint64_t off = (uint64_t)v - nwdev_base;
    if (off >= nwdev_size) return v;
    uint64_t pg = off >> 12;
    if (((off + n - 1) >> 12) != pg || !((nwdev_cur->dirty[pg >> 5] >> (pg & 31)) & 1)) nwdev_wb_slow(v, n);
    return v;
}

/* before every indirect call: a host function the device module does not
 * have came back from nwdev_in as the scratch page; calling it would stop
 * the device, so the run ends here, flagged */
__attribute__((always_inline, used)) static void nwdev_callee(void *fp)
{
    if (fp == (void *)nwdev_cur->scratch) nwdev_bad_call();
}
