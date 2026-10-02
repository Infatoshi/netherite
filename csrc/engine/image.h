/* The environment's image (GPU plan L12): one mapping that holds everything
 * an environment made with env_new owns, so the whole environment is one
 * block of bytes that can be copied to a device, or to another address, and
 * back.
 *
 * Layout, from the mapping's base: struct env (env.h), then its arena
 * (arena.h: the entity, chunk and band pools, the list slab), then the heap.
 * The heap is where the engine's malloc, calloc, realloc and strdup go
 * while the environment runs engine code (envheap.h routes them; session.c
 * turns it on around the load and each row), and where fixed arrays
 * (arena.h fixed_array) are made. Size classes are powers of two from 32
 * bytes, each block led by a 16-byte header, named by offsets; freed blocks
 * go on their class's chain. Pages are committed as written.
 *
 * What is reachable by offset rather than address: the chunks and their
 * bands (world.h chunkref, bandoff), the entity references (living.h lref,
 * the pools' slots), the list slab's blocks, the heap's chains, the random
 * streams (env.h struct rng_table) and the directory below. Everything
 * else the engine keeps is still a C pointer; image_relocate moves the
 * image and rebases those (tests/test_relocate.c counts them per move).
 *
 * The first environment (env.c env_first) is not an image: its arena is a
 * mapping of its own and its heap is the process's. */
#ifndef NETHERITE_IMAGE_H
#define NETHERITE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* heap size classes: 2^5 .. 2^(5 + IMG_HEAP_CLASSES - 1) bytes */
#define IMG_HEAP_CLASSES 36
/* image_free_count's tiers: blocks of 4 KB, 8 KB, .. 1 MB and more */
#define IMG_FREE_TIERS 9
#define IMG_FREE_TIER0 4096
/* the zap log's entries (env_image) */
#define IMG_ZAP_MAX 128
/* the heap's reservation */
#define IMG_HEAP_BYTES ((size_t)32 << 30)

/* Images live in a window of the address space the process leaves alone
 * (Linux x86-64 puts the program near 0x55.., mappings near 0x7f..; CUDA
 * maps near 8 GB and 1 TB, a non-PIE python near 4 MB), one per slot of
 * IMG_SLOT bytes, so the image an address is in is the address rounded
 * down to its slot: free() tells an image's block from the C library's
 * with one compare. 1,088 slots of 64 GB run from 16 TB to 84 TB, below
 * the lowest a PIE program loads (0x555555554000, about 85.3 TB). The env
 * pool (csrc/runtime) holds an image per env and two per start, so one
 * process makes 1,024 envs with slots to spare (lane/imgwin). An image is
 * 38.5 GB of address (image_bytes on 2026-10-01: struct env 28 MB, the
 * arena 6.5 GB, the heap 32 GB), 1.66x within its slot.
 * cuda/tick/host.c unmaps the whole window in its device child. */
#define IMG_WINDOW ((uintptr_t)1 << 44)
#define IMG_SLOT ((uintptr_t)1 << 36)
#define IMG_SLOTS 1088

/* Where the environment's parts sit, as offsets from the image's base (the
 * address of its struct env); 0 for a part it does not have. Set by whoever
 * makes the part (session.c, the harness). A kernel handed the image finds
 * each world and dimension state from here. */
struct img_dir {
    int64_t session;          /* struct session */
    int64_t snapshot;         /* struct snapshot the session started from */
    int64_t sr;               /* struct serverreplay */
    int64_t sr_world[3];      /* sr->w[i], in worldServers order */
    int64_t world[3];         /* each server world's struct world (sr->w[i].st.w) */
    int64_t dim[3];           /* sr->dims[k] by sr_dim_index */
    int64_t client_world;     /* the client's struct world */
};

struct env_image {
    size_t size;              /* the mapping's bytes; 0: not an image */
    int heap_on;              /* the engine's allocations go to the heap */
    int64_t heap_off;         /* the heap's offset in the image */
    size_t heap_cap, heap_used;
    int64_t heap_free[IMG_HEAP_CLASSES];
    uint64_t heap_live;       /* blocks allocated and not freed */
    uint64_t frees;           /* free and realloc calls, from either heap (image_free_count) */
    uint64_t frees_ge[IMG_FREE_TIERS];   /* those of a block of at least IMG_FREE_TIER0 << k bytes */
    /* a write tracker watches the image (imgxfer.h): a freed block's pages
     * handed back to the kernel read as unwritten to it, so each one goes in
     * the zap log for the next export (which carries them as zero pages),
     * and once the log is full a freed block's present pages are cleared by
     * writes instead */
    int track;
    int track_fd;             /* its userfaultfd: a freed block's protection is lifted before the pages go, so
                               * their page tables can go too (a protected empty page keeps a marker) */
    uint32_t nzap;
    struct { uint64_t off, len; } zap[IMG_ZAP_MAX];
    struct img_dir dir;
};

struct env;

/* A new environment in a fresh image (env.c env_new: zeroed, its arena not
 * yet carved) and its release; image_arena_base is where its arena goes. */
struct env *image_env_new(void);

/* The current environment's free and realloc calls so far that gave back a
 * block that could have held BYTES bytes (an image block's class, or the C
 * library's usable size): every one for BYTES under IMG_FREE_TIER0, else
 * those of blocks of at least the tier at or under BYTES (env_image frees,
 * frees_ge). New bytes at an address need the block that held the old ones
 * freed first (raster_obs.h raster_rec_free_count). */
uint64_t image_free_count(size_t bytes);
/* The same for BYTES at P, exact where P starts a block of an image's
 * heap that was allocated for BYTES (its header in place): that block's
 * generation, which its every free and realloc moves, since blocks neither
 * split nor merge, so other bytes come to P only once that block itself was
 * freed (lane/cachefit: a class count moved with every other block's free,
 * and a kept texture copy was compared again nearly every frame). The low
 * byte of the value names which count it is, so two values compare equal
 * only as the same count. */
uint64_t image_free_count_at(const void *p, size_t bytes);
void image_env_free(struct env *e);
void *image_arena_base(struct env *e);

/* 1 when p lies in e's image (e an image); the image p lies in, NULL for
 * none. */
int image_holds(const struct env *e, const void *p);
struct env *image_owner(const void *p);

/* The heap's blocks. image_alloc takes a block of at least n bytes, zeroed
 * when zero is set; image_free gives it back; image_block_size is the
 * block's requested size. */
void *image_alloc(struct env *e, size_t n, int zero);
void image_free(struct env *e, void *p);
/* [p, p + len) (whole pages of e's memory: its image, or the first
 * environment's arena) handed back to the kernel (madvise): they read zero
 * again and cost nothing until written. Under a write tracker they go in the
 * zap log, or once it is full the present ones are cleared by writes. 1 when
 * the pages went back. */
int image_give_pages(struct env *e, void *p, size_t len);
size_t image_block_size(const void *p);
/* The whole pages under runs of e's free heap blocks handed back to the
 * kernel, the blocks that start on them taken off their chains for good
 * (their address space is not used again): a start the env pool copies
 * to every env carries no page of what its load freed (runtime/pool.c,
 * lane/heapprof). Returns the bytes handed back. Never during a row: the
 * blocks it takes off are gone. */
size_t image_heap_trim(struct env *e);

/* Turn the heap on or off for e (an image; no-op otherwise); returns the
 * previous setting. */
int image_heap_set(struct env *e, int on);

/* An image's live bytes, as extents from its base: struct env, the pools'
 * used slots and their chains, the living parts, the list slab and the
 * heap up to their high-water marks. Returns the count (at most max). */
enum { IMG_PART_ENV, IMG_PART_POOLS, IMG_PART_SLAB, IMG_PART_HEAP, IMG_PARTS };
struct img_extent {
    size_t off, len;
    int part;                 /* IMG_PART_* */
};
int image_extents(const struct env *e, struct img_extent *out, int max);

/* image_relocate's report. */
struct img_reloc_stats {
    size_t bytes_copied;      /* nonzero pages copied */
    size_t words_scanned;
    size_t pointers_rebased;  /* words that named the old image */
    size_t rebased_in[IMG_PARTS];   /* the same by the part they sit in */
    size_t extents;
};

/* Copy a's image to a new mapping at another address and continue it
 * there: every live extent is copied, and every 8-byte word in them that
 * points into a's image while the word at the same offset of twin (the
 * same run at another address, at the same row) differs is rebased to the
 * new mapping; a's mapping is released. twin tells a pointer from a value
 * that happens to look like one: a pointer into its own image differs
 * between the two, a value does not. Returns the moved environment (the
 * caller points nw_env at it), NULL on failure (a unchanged). */
struct env *image_relocate(struct env *a, const struct env *twin, struct img_reloc_stats *st);

/* A free slot of the window held by an inaccessible mapping of len bytes
 * (no memory), for image_move to move an image onto; NULL if none. */
void *image_slot_reserve(size_t len);
/* Move the len-byte mapping at from to the address to (mremap: the pages
 * move, not their bytes; whatever was mapped at to is replaced), and hold
 * from with an inaccessible reservation. 0 on success. Linux only. */
int image_move(void *from, void *to, size_t len);

#endif
