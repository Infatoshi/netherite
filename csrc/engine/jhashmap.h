/* java.util.HashMap's iteration order for a HashMap keyed by a Long:
 * MapGenStructure's structureMap, keyed by ChunkCoordIntPair.chunkXZ2Int.
 * JDK 8 put appends to the tail of the bucket's chain, resize splits every
 * old chain into a low and a high half in chain order, and iteration walks
 * the buckets ascending and each chain front to back. (The ChunkPosition-keyed
 * set the explosion used is jorder.h's computed order now.) */
#ifndef NETHERITE_JHASHMAP_H
#define NETHERITE_JHASHMAP_H

#include <stdint.h>

/* MapGenStructure.structureMap,
 * keyed by ChunkCoordIntPair.chunkXZ2Int(cx, cz). Long.hashCode is
 * key ^ key >>> 32, which for that packing is cx ^ cz in 32-bit arithmetic,
 * then the JDK 8 spreading XOR. The value is opaque (the caller's own index).
 * The two keys' id fields are (cx, cz) as int32; put of an existing key
 * replaces the value and keeps the node where it is, as HashMap.put does. */
struct jhm64_node {
    struct jhm64_node *next;
    int64_t key;
    int64_t value;
};

struct jhm64 {
    struct jhm64_node **table;
    int cap;            /* table length, a power of two */
    int n;
};

void jhm64_init(struct jhm64 *m);
void jhm64_free(struct jhm64 *m);

/* HashMap.put for a chunkXZ2Int key. */
void jhm64_put(struct jhm64 *m, int64_t key, int64_t value);
int jhm64_size(const struct jhm64 *m);
int jhm64_contains(const struct jhm64 *m, int64_t key);

/* The iteration order: out gets each key's value, keys gets the keys when it
 * is not NULL. Both arrays are owned by the caller and must hold jhm64_size
 * entries. Returns the count. */
int jhm64_order(const struct jhm64 *m, int64_t *out, int64_t *keys);

#endif
