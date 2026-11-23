/* The provider's chunksToUnload: Collections.newSetFromMap(new
 * ConcurrentHashMap()) of Long, the order the unload loop sees (GPU audit
 * L4: order as data).
 *
 * The JDK 8 ConcurrentHashMap as it was ported (chunkload.c before L4): the
 * table is a power of two; a put appends at the tail of a plain bin; a bin
 * of 8 treeifies into a TreeBin, whose list keeps the bin's order and whose
 * red-black tree orders by (spread hash, Long value); a put into a tree bin
 * prepends the new node to the list; a remove unlinks it and may untreeify
 * the bin (by the tree's shape) back to a plain list in the list's order; a
 * resize splits every bin's list into its lo and hi runs preserving order (a
 * tree bin whose runs stay over 6 keys is rebuilt, or kept when the other
 * run is empty); addCount doubles the table once the size reaches sizeCtl;
 * treeifyBin on a table under 64 presizes instead.
 *
 * Every list operation is an end of the list, and nothing reorders a list,
 * so a bin's list order is a number per key: an append takes the next
 * stamp counting up, a prepend the next counting down. The iteration order
 * is a sort of the live keys by (bin, stamp), computed when asked
 * (cs_order); a key's slot never moves. What stays per bin is its key count
 * and, for a TreeBin, its tree's root: the tree (index links in the slots)
 * only decides the untreeify on remove. Membership is an open-addressed
 * index by key. All storage is fixed, made at the first cs_init. */
#ifndef NETHERITE_CHUNKSET_H
#define NETHERITE_CHUNKSET_H

#include <stddef.h>
#include <stdint.h>

/* the most bins and live keys */
#define CU_MAX_BINS (1 << 16)
#define CU_MAX_NODES (1 << 16)

struct cu_slot {
    int64_t key;
    int64_t stamp;  /* the key's place in its bin's list */
    int hash;       /* spread(Long.hashCode), what CHM stores per node */
    int parent, left, right;    /* the TreeBin's tree; parent chains free slots */
    int red;
    int live;
};

struct chunkset {
    int *count, *root;          /* per bin: keys; the TreeBin's root, -1 plain */
    int *count_alt, *root_alt;  /* the transfer's other table */
    int *start;                 /* per bin: the order's counting sort */
    int *run;                   /* the transfer's order (slots) */
    int32_t *index;             /* slot + 1 by key, linear probing */
    struct cu_slot *e;
    int n;                      /* bin count, a power of two */
    int size, sizeCtl;
    int ne, free;               /* slots used so far; the free chain */
    int64_t up, down;           /* the next append's stamp, the next prepend's */
    uint64_t gen;               /* bumped by every add and init: an order
                                 * cs_order gave stays the order (less the
                                 * keys taken since) while it holds */
};

/* An empty set (a new table). */
void cs_init(struct chunkset *s);

/* An empty set whose table already has bins bins (a power of two, at least
 * the new table's): a snapshot's set, whose table grew through its history. */
void cs_init_bins(struct chunkset *s, int bins);
void cs_free(struct chunkset *s);
/* the bytes its tables have committed (envmem.h) */
size_t cs_bytes(const struct chunkset *s);

/* Set.add (ConcurrentHashMap.put); a held key only runs putVal's treeify
 * check. */
void cs_put(struct chunkset *s, int64_t key);

/* Set.remove: 1 when the key was held. */
int cs_take(struct chunkset *s, int64_t key);

int cs_has(const struct chunkset *s, int64_t key);

/* The held keys' list places as a captured iteration order gives them (a
 * snapshot's set: the order is bins ascending, and inside a bin the list,
 * whose order a TreeBin's prepends made; re-adding the keys cannot rebuild
 * it). Keys not held are skipped; later adds append after, prepend before,
 * every stamp given here. */
void cs_restamp(struct chunkset *s, const int64_t *keys, int n);

/* The iteration order: the live keys' slots into out, bins ascending, list
 * order inside a bin. Returns the size. */
int cs_order(struct chunkset *s, int *out);

#endif
