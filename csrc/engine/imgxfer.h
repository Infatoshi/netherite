/* The environment image's transfer format (GPU plan L13): what goes to a
 * device and back.
 *
 * A transfer is pages of one image (image.h) by index, with the address the
 * image had when they were read: the pointers the image still holds
 * (image.h: the ones outside the chunk map, the bands, the RNG table and the
 * directory) name that base, so a device that holds the image at another
 * address reads a pointer p as its own base + (p - base), and writes one back
 * in the host's form.
 *
 * env_export reads a transfer out of an image: the whole image (every
 * nonzero page of its live extents, image_extents) or, with a write tracker,
 * only the pages written since the tracker last saw the image in step with
 * its copy. env_import writes a transfer into an image of the same layout
 * (another address is fine); with the tracker, the pages it wrote count as in
 * step. A host and its device copy exchange deltas: the host's writes since
 * the last exchange go down, the kernel's writes come back.
 *
 * The tracker (Linux 6.7 and later) is one userfaultfd in asynchronous
 * write-protect mode over each image mapping registered with it: a write to
 * a protected page lifts the protection in the kernel, without a handler,
 * and PAGEMAP_SCAN returns the unprotected pages and protects them again.
 * mremap keeps the registration (UFFD_FEATURE_EVENT_REMAP; the tracker's
 * thread acknowledges the move), so an image can move between addresses and
 * stay tracked. A freed heap block of more than a page is zeroed by writes
 * while an image is tracked (image.c; a page handed back to the kernel would
 * read as unwritten). Elsewhere img_track_open returns NULL and a transfer
 * is always the whole image. */
#ifndef NETHERITE_IMGXFER_H
#define NETHERITE_IMGXFER_H

#include <stddef.h>
#include <stdint.h>

#define IMG_PAGE 4096

struct env;

struct img_xfer {
    uint64_t base;            /* the image's address when exported */
    uint64_t size;            /* its mapping's bytes (the layout both ends share) */
    int full;                 /* the whole image, not a delta */
    size_t npages;            /* pages carried */
    size_t ndata;             /* of them with their bytes (the rest are zero) */
    size_t cap;               /* room for */
    uint32_t *page;           /* each page's index in the image (offset / IMG_PAGE), ascending,
                               * with IMG_XFER_ZERO for a page of zeros (no bytes carried) */
    unsigned char *bytes;     /* the ndata pages with bytes, in that order */
};
#define IMG_XFER_ZERO 0x80000000u

/* A transfer's buffers: address space for a whole image of size bytes
 * (committed as written, and named, so the grave's scan skips them). */
int img_xfer_init(struct img_xfer *x, uint64_t size);
void img_xfer_free(struct img_xfer *x);
/* Hand back the buffers' pages (after a large transfer). */
void img_xfer_trim(struct img_xfer *x);

/* A private mapping of len bytes, committed as written and named (Linux) so
 * the grave's scan skips it; NULL if none. */
void *img_map_named(size_t len, const char *name);
void img_unmap(void *p, size_t len);
/* vec[k] & 1 when page k of [p, p + len) holds memory (mincore); -1 on error */
int img_resident(const void *p, size_t len, unsigned char *vec);

struct img_track;

/* The tracker, or NULL (the reason in err) where there is none. */
struct img_track *img_track_open(char *err, size_t errlen);
void img_track_close(struct img_track *t);
/* Watch e's image mapping from now on: every page it holds is protected, so
 * the next export carries only what is written after this. Returns 0, or -1
 * (the reason in err). */
int img_track_add(struct img_track *t, struct env *e, char *err, size_t errlen);

/* Read e's image into x: with t, the pages written since e was last in step
 * (and they are protected again); without, the whole image. Returns the
 * pages carried, or -1. */
long env_export(struct env *e, struct img_xfer *x, struct img_track *t);
/* Write x's pages into e's image (e's layout must be x's); with t, they count
 * as in step (protected again). Returns the pages written, or -1. */
long env_import(struct env *e, const struct img_xfer *x, struct img_track *t);

/* The calling thread's instruction counters (perf_event_open): user space
 * and kernel, read together. Where the kernel refuses one, it reads 0. */
struct img_counters {
    int fd;                   /* the group's leader, -1 none */
    int n;                    /* counters in the group */
};
void img_counters_open(struct img_counters *c);
void img_counters_read(const struct img_counters *c, uint64_t out[2]);

#endif
