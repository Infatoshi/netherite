/* JDK 8 hash-container iteration orders, computed as data (GPU audit L4).
 *
 * Where vanilla iterates a HashMap or HashSet, the order it sees is game
 * state: which chunk the spawner tries first, which block an explosion
 * breaks first. A put-only java.util.HashMap whose bins stay lists iterates
 * its keys bin by bin (bin = spread(hash) & (cap - 1), cap the table length
 * it ended at), and inside a bin in insertion order: a put appends at the
 * bin's tail, and a resize splits each bin into a low and a high run, each
 * keeping its order. So the order is a stable sort of the insertion sequence
 * by bin, with no chains to walk. What stays per container is the insertion
 * sequence, a membership index where the container needs one, and the table
 * length.
 *
 * A bin that reaches 9 keys at a table of 64 or more becomes a red-black
 * TreeNode bin in JDK 8, whose order is not the insertion order: the
 * chunk windows emulate the HashMap there (jorder.c; a 17x17 spawner window
 * reaches a bin of 9 at about 0.2% of chunks, lane/longrun's CAP-10 count),
 * the explosion's set stops the program instead (no blast reached 8). */
#ifndef NETHERITE_JORDER_H
#define NETHERITE_JORDER_H

#include <stdint.h>

/* ChunkCoordIntPair.hashCode: two LCG steps, in 32-bit arithmetic. */
static inline int32_t jord_ccp_hash(int32_t cx, int32_t cz)
{
    uint32_t a = (uint32_t)(1664525u * (uint32_t)cx + 1013904223u);
    uint32_t b = (uint32_t)(1664525u * (uint32_t)(cz ^ (int32_t)-559038737) + 1013904223u);
    return (int32_t)(a ^ b);
}

/* ChunkPosition.hashCode: x * 8976890 + y * 981131 + z, with Java int
 * overflow. */
static inline int32_t jord_cpos_hash(int32_t x, int32_t y, int32_t z)
{
    return (int32_t)((uint32_t)x * 8976890u + (uint32_t)y * 981131u + (uint32_t)z);
}

/* HashMap.hash: h ^ (h >>> 16). */
static inline uint32_t jord_spread(int32_t h)
{
    uint32_t u = (uint32_t)h;
    return u ^ (u >> 16);
}

/* The table length a HashMap reaches holding n keys put from empty: 16,
 * doubled while the size is over three quarters of it. */
static inline int jord_cap(int n)
{
    int cap = 16;

    while (n > cap * 3 / 4) cap *= 2;

    return cap;
}

/* The largest window jord_ccp_window orders, and the largest table it sorts
 * into: a view distance of 16's 1,089 keys (serverreplay.h SR_MAX_VIEW). */
#define JORD_WINDOW_MAX_KEYS 1089
#define JORD_WINDOW_MAX_CAP 2048

/* A HashMap or HashSet of ChunkCoordIntPair filled with the (2r+1)^2 chunks
 * around (pcx, pcz), x outer and z inner (the spawner's eligible chunks,
 * World.activeChunkSet), put into a HashMap whose table has *cap bins when
 * the fill starts (a cleared map keeps its table; 0: none yet): its
 * iteration order, as (cx, cz) pairs into out, with JDK 8's TreeNode bins
 * (jorder.c); *cap becomes the table the fill ends at. Returns the key
 * count, 0 when the window or a table is past JORD_WINDOW_MAX_KEYS or
 * JORD_WINDOW_MAX_CAP. */
int jord_ccp_window(int pcx, int pcz, int r, int *cap, int *out);

/* One caller's last jord_ccp_window answer: a fill with the same centre,
 * radius and starting table is the same order and ends at the same table,
 * so a caller whose centre stays put (a player inside one chunk) copies it
 * instead of hashing the window again. valid 0: none yet. */
struct jord_window_memo {
    int valid, pcx, pcz, r, cap_in, cap_out, n;
    int out[2 * JORD_WINDOW_MAX_KEYS];
};

/* jord_ccp_window through M */
int jord_ccp_window_memo(struct jord_window_memo *m, int pcx, int pcz, int r, int *cap, int *out);

/* A put-only HashSet of ChunkPosition (World.doExplosionA's affected set)
 * over its caller's storage: keys (x, y, z per key, in insertion order),
 * index (the membership table, open addressing over the key numbers, idx_cap
 * a power of two over twice the most keys) and count (per bin of the JDK
 * table, up to max_cap bins). A put allocates nothing; more keys than fit
 * stops the program. A fill whose bin reaches 9 keys (JDK 8's treeifyBin,
 * or a resize under 64 bins) takes its order from the HashMap emulation,
 * over the index's storage: at most JORD_SET3_TREED_MAX_KEYS keys (a
 * size-6 blast reaches about 3,200). */
#define JORD_SET3_TREED_MAX_KEYS 8192
struct jord_set3 {
    int32_t *keys;
    int32_t *index;
    int32_t *count;
    int n, max_keys;
    int cap;            /* the JDK table's length */
    int idx_cap, max_cap;
    int treed;          /* a bin reached 9 keys */
};

void jord_set3_init(struct jord_set3 *s, int32_t *keys, int max_keys, int32_t *index, int idx_cap,
                    int32_t *count, int max_cap);

/* 1 when the key is held. */
int jord_set3_contains(const struct jord_set3 *s, int32_t x, int32_t y, int32_t z);

/* HashSet.add: 1 when the key is new (it joins the insertion order), 0 when
 * it was held. */
int jord_set3_add(struct jord_set3 *s, int32_t x, int32_t y, int32_t z);

/* The iteration order: the keys as (x, y, z) into out (3 * n ints). Uses
 * the count storage, and the index's after a bin of 9. Returns n. */
int jord_set3_order(struct jord_set3 *s, int32_t *out);

#endif
