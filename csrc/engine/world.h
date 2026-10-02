/* The native world core: a chunk map, and World.setBlock with everything it
 * touches in vanilla 1.7.10. A chunk is Chunk with ExtendedBlockStorage
 * flattened: four per-cell byte arrays in the flat layout x << 12 | z << 8 | y
 * (the layout the setblock probe hashes) plus the height maps, the section mask
 * and the relight flags. */
#ifndef NETHERITE_WORLD_H
#define NETHERITE_WORLD_H

#include <stddef.h>
#include <stdint.h>
#include "aabb.h"
#include "arena.h"
#include "blocks.h"
#include "chunkgen.h"
#include "collide.h"
#include "drops.h"
#include "end.h"
#include "nether.h"
#include "terrain.h"
#include "tileentity.h"

/* A fetch ahead of a read the tick is about to make (a hint: nothing reads
 * its result); none in the device build, whose addresses are the host's
 * until translated */
#if defined(__NVPTX__)
#define NW_PREFETCH(p) ((void)(p))
#else
#define NW_PREFETCH(p) __builtin_prefetch((p))
#endif

/* EnumSkyBlock, with vanilla's defaultLightValue. */
enum { LIGHT_SKY = 0, LIGHT_BLOCK = 1 };
#define LIGHT_DEFAULT_VALUE(type) ((type) == LIGHT_SKY ? 15 : 0)

/* One 16-block band of a chunk's cells: ExtendedBlockStorage's arrays,
 * index x << 8 | z << 4 | (y & 15). The id's low byte per cell
 * (blockLSBArray); metadata, sky light and block light as NibbleArrays (cell i
 * in byte i >> 1, the low nibble for an even i); the id's high nibble
 * (blockMSBArray) only once an id above 255 is stored (none of 1.7.10's
 * blocks is).
 *
 * The three nibble arrays are not in the band: nib[k] names array k (NIB_*)
 * by its address less the band's, either a slot of the environment's
 * nibble pool (arena.h ARENA_NIBS) that is the band's own (bit k of nib_own)
 * or one of the pool's sixteen constant arrays, every nibble v (slot v, made
 * with the arena, never written again). Most arrays are one value: sky
 * light 0 below the ground, block light 0 away from torches and lava,
 * metadata 0 in stone (lane/chunkmem's census: about two in three to five
 * in six of a village's or an explorer's bands), so a band is its ids and
 * the arrays that vary. A read goes through chunk_sec_nib (the offset is in
 * the band's first line, beside ids_hi); a store that changes a constant
 * array takes the band's own slot first, filled with the constant
 * (chunk_sec_nib_set: a pool slot, no allocation); the bulk stores
 * (chunk_cells_in, chunk_sec_nib_store, the codec) hold an array that comes
 * out one value as that constant again. A copy with its arrays inline
 * (struct chunk_sec_flat: the device mesher's and light engine's images)
 * names them by their inline offsets, so every accessor reads it the same. */
#define SEC_CELLS 4096
#define SEC_NIB_BYTES (SEC_CELLS / 2)
enum { NIB_METAS = 0, NIB_SKY = 1, NIB_BLOCK = 2, NIB_N = 3 };
struct chunk_sec {
    uint8_t *ids_hi;              /* first: every id read tests it */
    /* the chunks holding the band besides one: a copy of a chunk (the
     * client's, a chunk packet's) shares its bands, and whichever side
     * stores a different value first gets its own copy (chunk_sec_own), so
     * each chunk reads what it would have read with bands of its own */
    uint32_t shared;
    uint8_t nib_own;              /* bit k: nibble array k is the band's own slot */
    uint8_t pad_[3];
    int32_t nib[NIB_N];           /* nibble array k at (char *)band + nib[k] */
    uint32_t pad2_;
    uint8_t ids[SEC_CELLS];
};
_Static_assert(sizeof(struct chunk_sec) == 32 + SEC_CELLS, "a band's header is 32 bytes");

/* a band with its nibble arrays inline, in NIB_* order after the ids */
struct chunk_sec_flat {
    struct chunk_sec h;
    uint8_t metas[SEC_NIB_BYTES];
    uint8_t sky[SEC_NIB_BYTES];
    uint8_t blocklight[SEC_NIB_BYTES];
};
#define SEC_FLAT_NIB(k) ((int32_t)(sizeof(struct chunk_sec) + (size_t)(k) * SEC_NIB_BYTES))

static inline const uint8_t *chunk_sec_nib(const struct chunk_sec *s, int k)
{
    return (const uint8_t *)s + s->nib[k];
}
#define chunk_sec_metas(s) chunk_sec_nib((s), NIB_METAS)
#define chunk_sec_sky(s) chunk_sec_nib((s), NIB_SKY)
#define chunk_sec_blocklight(s) chunk_sec_nib((s), NIB_BLOCK)

/* A chunk names each of its bands by the band's address less the chunk's
 * (0 for none). Chunks and bands are slots of the environment's arena
 * (arena.h), so the offset is the same wherever the arena's image sits: the
 * chunk struct holds no address (GPU plan L12), and reaching a band costs an
 * add. A copy of a chunk struct must set its own offsets (chunk_copy,
 * chunk_share, chunk_sec_put). */
typedef int64_t bandoff;

/* One chunk. A cell in a section the mask does not hold reads 0, like a cell
 * whose ExtendedBlockStorage was never made: every write makes the section
 * first and a new section is zeroed. The cells live in band[y >> 4], storage
 * made on the first nonzero write to the band and never before: a cell of a
 * band with no storage is 0, so the storage is a sparse copy of one flat
 * x << 12 | z << 8 | y array per field, independent of the mask (which stays
 * the game's). Read and write cells through chunk_cell_* and chunk_set_*.
 * no_sky is the world provider's hasNoSky: it gates every sky-light write the
 * chunk sees, and it is what keeps the Nether's sky array at 0. Chunks are
 * allocated zeroed (chunk_new) and released with chunk_free. */
struct chunk {
    /* The fields every block read and the tick's chunk loop reach come
     * first, laid out by cache line (the tick is bound on memory: a chunk
     * visited once a tick is a miss a line): line 0 holds the key, the
     * section mask and the bands of sections 0 to 5 (y below 96, where
     * most reads land), so World.getBlock there reads one of the chunk's
     * lines; line 2 the random tick loop's gates, line 3 the light
     * re-check memo. */
    int cx, cz;
    uint16_t mask;                /* bit s set when section s exists */
    uint8_t no_sky;               /* the world provider's hasNoSky: the Nether */
    uint8_t gap_lighting_updated;
    uint8_t light_populated, terrain_populated, populated;
    uint8_t lm_valid;
    bandoff band[16];
    /* A parked chunk (chunk_pack: the region store's) keeps its bands as
     * one deflated blob instead, and has no band[] until chunk_unpack. */
    uint8_t *packed;
    uint32_t packed_n;

    /* The section bookkeeping the server tick reads: ExtendedBlockStorage's
     * tickRefCount per section (the needsRandomTick gate), the chunk's
     * isTerrainPopulated / isLightPopulated / field_150815_m (above), and
     * queuedLightChecks (enqueueRelightChecks' cursor). */
    int32_t queued_light_checks;
    uint16_t sections_ticking[16];

    /* The light re-check memo (chunk_tick_flags, world.c). stamp is the
     * world's write epoch at this chunk's last block or light write, or at its
     * insertion into the map; edge_stamp[0..3] the same for writes on the
     * x = 0, x = 15, z = 0 and z = 15 columns (and for any write that makes a
     * section). lm_epoch and lm_loaded are the epoch and the loaded mask of
     * the 5x5 chunks around this one at the last Chunk.func_150809_p attempt
     * that failed and wrote nothing (lm_valid, above). */
    uint64_t stamp, edge_stamp[4], lm_epoch;
    uint32_t lm_loaded;
    int32_t height_min;           /* heightMapMinimum */
    /* the world's write sequence at this chunk's last write of any kind (a
     * block, a metadata or a light write, the insertion; world_note_write):
     * the phase digests (phase.h) hash the chunks written in a phase */
    uint64_t wseq;

    /* The random tick's cell filter (servertick.c), a superset of where
     * band s's random-ticking blocks are: bit RTICK_BIT(x, y, z) set when a
     * cell of that group (one y, an 8 by 8 quarter of the band's x and z)
     * may hold one. 0 until the tick first reads the band after it was put
     * (chunk_sec_put) or overwritten (chunk_cells_in); chunk_set_id adds a
     * random-ticking id's bit, and nothing clears one. A random tick whose
     * cell's bit is clear does not read the band: the id there cannot tick
     * (a pick read 64 lines of every ticking band a step at random;
     * lane/villscale). */
    uint64_t rtick_mask[16];

    int32_t height[256];          /* heightMap, index z << 4 | x */
    int32_t precip[256];          /* precipitationHeightMap, same index */
    uint8_t update_skylight_columns[256];
    struct te_store tes;          /* chunkTileEntityMap */
    int64_t inhabited_time;       /* Chunk.inhabitedTime: PlayerManager's clock, read by
                                   * World.func_147462_b; saved with the chunk */
    uint8_t biome[256];           /* blockBiomeArray, index z << 4 | x: the
                                   * populate driver's getBiomeGenForCoords */
};
_Static_assert(offsetof(struct chunk, band) + 6 * sizeof(bandoff) <= 64, "the key, the mask and bands 0 to 5 share line 0");
_Static_assert(offsetof(struct chunk, sections_ticking) / 64 == (offsetof(struct chunk, sections_ticking) + 31) / 64,
               "the random tick gates in one line");

/* A chunk named by its slot in the environment's chunk pool (arena.h), plus
 * one; 0 is no chunk. chunk_new takes a slot and chunk_free gives it back,
 * so every chunk of an environment is in its arena, and the chunk maps
 * (struct world's slot[] and near[]) hold these instead of addresses. */
typedef int32_t chunkref;

#define CHUNK_SLOT ((sizeof(struct chunk) + 63) & ~(size_t)63)

static inline struct chunk *chunk_ptr(chunkref r)
{
    return r ? (struct chunk *)(nw_arena()->chunks.base + (size_t)((uint32_t)r - 1u) * CHUNK_SLOT) : NULL;
}

static inline chunkref chunk_ref(const struct chunk *c)
{
    return c ? pool_index(&nw_arena()->chunks, c) + 1 : 0;
}

/* The band holding section s of c, NULL for none; chunk_sec_put makes sec
 * (NULL for none) c's section s. */
static inline struct chunk_sec *chunk_sec_at(const struct chunk *c, int s)
{
    bandoff o = c->band[s];
    return o ? (struct chunk_sec *)((unsigned char *)c + o) : NULL;
}

static inline void chunk_sec_put(struct chunk *c, int s, const struct chunk_sec *sec)
{
    c->band[s] = sec ? (const unsigned char *)sec - (const unsigned char *)c : 0;
    c->rtick_mask[s] = 0;
}

/* a local cell's group in rtick_mask: its y in the band, the halves of x and z */
#define RTICK_BIT(x, y, z) (((y) & 15) | ((x) >> 3 & 1) << 4 | ((z) >> 3 & 1) << 5)

/* A cell index c = x << 12 | z << 8 | y: its band and its index in the band. */
#define CELL_SEC(c) (((c) >> 4) & 15)
#define CELL_IN_SEC(c) (((c) >> 8) << 4 | ((c) & 15))

static inline int nibble_get(const uint8_t *a, int i)
{
    return (a[i >> 1] >> ((i & 1) << 2)) & 15;
}

static inline void nibble_set(uint8_t *a, int i, int v)
{
    int sh = (i & 1) << 2;
    a[i >> 1] = (uint8_t)((a[i >> 1] & ~(15 << sh)) | (v & 15) << sh);
}

static inline int chunk_sec_id(const struct chunk_sec *s, int i)
{
    return __builtin_expect(s->ids_hi != NULL, 0) ? s->ids[i] | nibble_get(s->ids_hi, i) << 8 : s->ids[i];
}

static inline int chunk_cell_id(const struct chunk *c, int cell)
{
    const struct chunk_sec *s = chunk_sec_at(c, CELL_SEC(cell));
    return s ? chunk_sec_id(s, CELL_IN_SEC(cell)) : 0;
}

static inline int chunk_cell_meta(const struct chunk *c, int cell)
{
    const struct chunk_sec *s = chunk_sec_at(c, CELL_SEC(cell));
    return s ? nibble_get(chunk_sec_metas(s), CELL_IN_SEC(cell)) : 0;
}

static inline int chunk_cell_sky(const struct chunk *c, int cell)
{
    const struct chunk_sec *s = chunk_sec_at(c, CELL_SEC(cell));
    return s ? nibble_get(chunk_sec_sky(s), CELL_IN_SEC(cell)) : 0;
}

static inline int chunk_cell_blocklight(const struct chunk *c, int cell)
{
    const struct chunk_sec *s = chunk_sec_at(c, CELL_SEC(cell));
    return s ? nibble_get(chunk_sec_blocklight(s), CELL_IN_SEC(cell)) : 0;
}

/* A local cell (x, z in 0..15, y in 0..255) read straight from its band:
 * the same answer as chunk_cell_id(c, x << 12 | z << 8 | y) behind a mask
 * test (World.getBlock and getBlockMetadata on a loaded chunk), without
 * composing the cell index and taking it apart again. */
#define SEC_XYZ(x, y, z) ((x) << 8 | (z) << 4 | ((y) & 15))

static inline int chunk_xyz_id(const struct chunk *c, int x, int y, int z)
{
    if (!(c->mask >> (y >> 4) & 1)) return 0;

    bandoff o = c->band[y >> 4];

    return o ? chunk_sec_id((const struct chunk_sec *)((const unsigned char *)c + o), SEC_XYZ(x, y, z)) : 0;
}

static inline int chunk_xyz_meta(const struct chunk *c, int x, int y, int z)
{
    if (!(c->mask >> (y >> 4) & 1)) return 0;

    bandoff o = c->band[y >> 4];

    return o ? nibble_get(chunk_sec_metas((const struct chunk_sec *)((const unsigned char *)c + o)), SEC_XYZ(x, y, z)) : 0;
}

/* The band's storage to write: made zeroed when it has none, the chunk's own
 * copy when it is shared. */
struct chunk_sec *chunk_sec_make(struct chunk *c, int s);
/* A shared band made the chunk's own (a copy), returned. */
struct chunk_sec *chunk_sec_own(struct chunk *c, int s);

/* A value the band cannot hold (an id past 4095, a nibble past 15): the
 * program stops rather than store something else. */
void chunk_value_range(const char *what, int v);

/* The stores. A 0 into a band with no storage is already there, so storage
 * is made only for a value that is not 0. */
static inline struct chunk_sec *chunk_sec_for(struct chunk *c, int cell, int v)
{
    struct chunk_sec *s = chunk_sec_at(c, CELL_SEC(cell));
    if (s == NULL && v != 0) s = chunk_sec_make(c, CELL_SEC(cell));
    return s;
}

void chunk_sec_set_id_hi(struct chunk_sec *s, int i, int v);
/* AnvilChunkLoader's section s of fresh chunk c from its NBT arrays (index y
 * << 8 | z << 4 | x; add, metas, sky and blocklight 2048 bytes of nibbles
 * each, or NULL), leaving the state chunk_set_id and the nibble setters leave
 * cell by cell in that order: no band when every value is 0. 0 (nothing
 * done) when the band exists already. */
int chunk_sec_load(struct chunk *c, int s, const uint8_t *blocks, const uint8_t *add, const uint8_t *metas,
                   const uint8_t *sky, const uint8_t *blocklight);

static inline void chunk_set_id(struct chunk *c, int cell, int v)
{
    if ((unsigned)v > 4095) chunk_value_range("id", v);
    struct chunk_sec *s = chunk_sec_for(c, cell, v);
    if (s == NULL) return;
    int i = CELL_IN_SEC(cell);
    if (__builtin_expect(s->shared != 0, 0))
    {
        if (chunk_sec_id(s, i) == v) return;
        s = chunk_sec_own(c, CELL_SEC(cell));
    }
    s->ids[i] = (uint8_t)v;
    if (v > 255 || s->ids_hi) chunk_sec_set_id_hi(s, i, v >> 8);
    if (BLOCKS[v].tick_randomly && c->rtick_mask[CELL_SEC(cell)] != 0)
        c->rtick_mask[CELL_SEC(cell)] |= 1ULL << RTICK_BIT(cell >> 12, cell, cell >> 8);
}

/* Nibble array k of s (a band no other chunk shares) made the band's own
 * slot, holding what it read; chunk_sec_nib_set stores cell i's nibble,
 * taking the slot only when the value differs from the constant's. */
void chunk_sec_nib_expand(struct chunk_sec *s, int k);

static inline void chunk_sec_nib_set(struct chunk_sec *s, int k, int i, int v)
{
    if (__builtin_expect(!(s->nib_own >> k & 1), 0))
    {
        if (nibble_get(chunk_sec_nib(s, k), i) == v) return;
        chunk_sec_nib_expand(s, k);
    }
    nibble_set((uint8_t *)s + s->nib[k], i, v);
}

/* Array k of s (unshared) replaced by the 2048 bytes at a: the constant
 * array when every nibble is one value, else the band's own slot. */
void chunk_sec_nib_store(struct chunk_sec *s, int k, const uint8_t *a);
/* The one value of every nibble of a, or -1. */
int nib_uniform(const uint8_t *a);
/* Every own array of s that holds one value made that constant again. */
void chunk_sec_nib_compact(struct chunk_sec *s);

#define CHUNK_NIBBLE_SETTER(name, k, what)                                      \
    static inline void name(struct chunk *c, int cell, int v)                   \
    {                                                                           \
        if ((unsigned)v > 15) chunk_value_range(what, v);                       \
        struct chunk_sec *s = chunk_sec_for(c, cell, v);                        \
        if (s == NULL) return;                                                  \
        if (__builtin_expect(s->shared != 0, 0))                                \
        {                                                                       \
            if (nibble_get(chunk_sec_nib(s, k), CELL_IN_SEC(cell)) == v)        \
                return;                                                         \
            s = chunk_sec_own(c, CELL_SEC(cell));                               \
        }                                                                       \
        chunk_sec_nib_set(s, k, CELL_IN_SEC(cell), v);                          \
    }
CHUNK_NIBBLE_SETTER(chunk_set_meta_cell, NIB_METAS, "metas")
CHUNK_NIBBLE_SETTER(chunk_set_sky, NIB_SKY, "sky")
CHUNK_NIBBLE_SETTER(chunk_set_blocklight, NIB_BLOCK, "blocklight")
#undef CHUNK_NIBBLE_SETTER

/* One column of a band (16 cells, y & 15 from 0) in the flat per-cell forms:
 * ids as uint16, the rest a byte per cell; any output may be NULL, and a
 * NULL band is zeros. chunk_sec_column_in stores the same forms (NULL skips
 * a field). */
void chunk_sec_column_out(const struct chunk_sec *s, int col, uint16_t *ids, uint8_t *metas, uint8_t *sky,
                          uint8_t *blocklight);
void chunk_sec_column_in(struct chunk_sec *s, int col, const uint16_t *ids, const uint8_t *metas,
                         const uint8_t *sky, const uint8_t *blocklight);

/* A zeroed chunk with no storage; chunk_free releases its storage and tile
 * entity store and the chunk. chunk_clear drops the storage and zeroes every
 * field (the tile entity store too, without freeing it). chunk_copy makes dst
 * a deep copy of src's cells and fields (dst's old storage released; its tile
 * entity store is overwritten with src's, which the caller replaces). */
struct chunk *chunk_new(void);
void chunk_free(struct chunk *c);
void chunk_clear(struct chunk *c);
/* The cells alone dropped (every band reads 0 again); the rest stays. */
void chunk_clear_cells(struct chunk *c);
void chunk_copy(struct chunk *dst, const struct chunk *src);
/* chunk_copy whose dst shares src's bands (struct chunk_sec's shared): the
 * client's copy of a server chunk, a chunk packet's */
void chunk_share(struct chunk *dst, const struct chunk *src);
/* dst's band s released and src's shared in its place (NULL when src has none) */
void chunk_share_band(struct chunk *dst, const struct chunk *src, int s);
/* c's band s held as it is now (a later write to c copies it first), NULL
 * for none; the hold is given back with chunk_sec_drop or passes to a chunk
 * with chunk_sec_install, which replaces that chunk's band s */
struct chunk_sec *chunk_sec_hold(const struct chunk *c, int s);
void chunk_sec_drop(struct chunk_sec *sec);
void chunk_sec_install(struct chunk *c, int s, struct chunk_sec *sec);

/* The four fields as flat x << 12 | z << 8 | y arrays: chunk_cells_in stores
 * them (NULL skips a field, which is left as it was), making storage only for
 * a band that holds a nonzero cell; chunk_cells_out writes them (NULL skips). */
void chunk_cells_in(struct chunk *c, const uint16_t *ids, const uint8_t *metas, const uint8_t *sky,
                    const uint8_t *blocklight);
void chunk_cells_out(const struct chunk *c, uint16_t *ids, uint8_t *metas, uint8_t *sky, uint8_t *blocklight);

/* A band as its inline form (the header's nib offsets inline, nib_own 0;
 * ids_hi as it is) */
void chunk_sec_flatten(const struct chunk_sec *s, struct chunk_sec_flat *out);
/* The bytes a band holds: the slot and its own nibble arrays. */
size_t chunk_sec_bytes(const struct chunk_sec *s);

/* Bytes of storage the chunk holds (the struct and its bands). */
size_t chunk_bytes_resident(const struct chunk *c);

/* Park a chunk nothing reads (the region store's): its bands become one
 * deflated blob; chunk_unpack makes them bands again, byte for byte. Only
 * the cells move; every other field stays in the struct. */
void chunk_pack(struct chunk *c);
void chunk_unpack(struct chunk *c);
/* The bands of a blob chunk_pack made, into c (which has none): the region
 * store's spill file (spill.h) keeps blobs outside the chunk. */
void chunk_unpack_blob(struct chunk *c, const uint8_t *blob, uint32_t n);

/* ---------------------------------------------------------- chunk requests
 *
 * Every chunk a server world gains, and every generation, goes through
 * world_chunk_request (GPU audit L4): it records the request, then serves it
 * through the world's generation backend. The load decisions (the provider's
 * saved copy, the populate rule, the unload set) stay with the callers above
 * it; what reaches it is the chunk's source. */
enum chunk_req_kind {
    CREQ_GENERATE,  /* the provider's miss: generate the chunk and insert it */
    CREQ_STORED,    /* a stored chunk the caller took (the region store, the
                     * saved set), inserted */
    CREQ_SUPPLIED,  /* a chunk whose content the caller supplies (a
                     * snapshot's bytes), inserted; its generation is owed */
    CREQ_OWED,      /* the Nether's or the End's owed generation, into scratch
                     * (the overworld's are paid by its next CREQ_GENERATE,
                     * each recorded as a CREQ_OWED) */
    CREQ_KINDS
};

struct chunk_req {
    int32_t cx, cz, kind;
};

/* The requests so far: a count per kind and the last CREQ_RING in order
 * (request k at ring[k % CREQ_RING]). */
#define CREQ_RING 256
struct chunk_reqlog {
    uint64_t n;
    uint64_t kinds[CREQ_KINDS];
    struct chunk_req ring[CREQ_RING];
};

struct world;

/* The block events one server tick can queue (struct world bev): distinct
 * chest and ender chest events only, a few per chest a tick. */
#define WORLD_BEV_MAX 1024

/* What makes a chunk's content: the dimension's ChunkProvider.provideChunk
 * into c (a new chunk), with no population. chunk_backend_c is the C
 * engine's (provide_chunk, and nether.c and end.c through world.c); a
 * batched generator (csrc/cuda's worldgen) goes behind the same
 * call. A backend that made chunks ahead of their requests (genahead.h) also
 * has take, which hands over the chunk made for (cx, cz) with the provider
 * state its generation leaves already applied (world_gen_state), or NULL
 * (generate runs), and owed, which applies that state alone for an owed
 * generation and returns 1, or 0 (generate runs into scratch). */
struct chunk_backend {
    const char *name;
    void (*generate)(struct world *w, int cx, int cz, struct chunk *c);
    struct chunk *(*take)(struct world *w, int cx, int cz);
    int (*owed)(struct world *w, int cx, int cz);
};
extern const struct chunk_backend chunk_backend_c;

/* chunk_key(cx, cz) inline: ChunkCoordIntPair.chunkXZ2Int */
#define WORLD_CKEY(cx, cz) ((int64_t)(uint32_t)(cx) | (int64_t)(uint32_t)(cz) << 32)

struct chunk_near { chunkref ref; int64_t key; } __attribute__((aligned(16)));

struct world {
    int64_t seed;
    struct chunkgen gen;
    chunkref *slot;               /* open addressing on (cx, cz) */
    int64_t *skey;                /* slot[i]'s chunk_key, so a probe reads no chunk */
    size_t cap, used;
    /* A direct-mapped cache in front of the map, slot (cx & 15) | (cz & 15) << 4:
     * block reads cluster in a few chunks and most lookups hit it. Unloading a
     * chunk clears its slot. A hit reads the ref and key from one line. */
    struct chunk_near near[256];
    /* The light pass's chunks (world_light_update_raw and the column loops
     * around it): the refs of the 8x8 chunks from (lwin_cx - 3, lwin_cz - 3),
     * each looked up once (0: not loaded); bit b of lwin_have when lwin[b]
     * holds, of lwin_loaded when it holds a chunk. A cache only: map_ver
     * counts the map's insertions and removals, and the window keeps what it
     * still covers when it moves. */
    uint32_t map_ver, lwin_ver;
    uint64_t lwin_have, lwin_loaded;
    int lwin_cx, lwin_cz;
    chunkref lwin[64];
    /* the last box lw_near_exist answered: its chunk range, the map_ver it
     * was answered at plus one (0: none) and the answer */
    int lbox[4];
    uint32_t lbox_ver;
    int lbox_ok;
    /* world_entity_area_loaded's answers by the area's corner chunk: the
     * corner, the map_ver answered at plus one (0: none) and the answer */
    struct { int cx, cz; uint32_t ver; int ok; } amemo[256];
    int64_t *load_order;          /* ChunkProviderServer.loadedChunks: packed (cz << 32 | cx & 0xffffffff) keys, the order chunks loaded in */
    size_t lon, locap;
    int slab_mem;                 /* slot and load_order are the environment's slab's (world_init_slab) */
    uint8_t stronghold_seen_mask; /* MapGenStronghold starts generated by any chunk ever loaded */
    uint8_t stronghold_unlinked_mask; /* of those, the ones read back from the save (no portal room link) */
    uint64_t epoch;               /* the write epoch chunk stamps take (struct chunk) */
    uint64_t wseq;                /* the write sequence chunks' wseq take */

    /* World.field_147482_g, the tile entity walk list, in the order the
     * entities were created; field_147484_a (addedTileEntityList) and
     * field_147481_N (the walk's own flag, read by getTileEntity and
     * setTileEntity). Tileticks.c runs the pass. */
    struct tile_entity **te_list;
    int te_n, te_cap;
    struct tile_entity **te_added;
    int te_added_n, te_added_cap;
    /* every tile entity this world ever held, freed once at world_free */
    struct tile_entity **te_reg;
    int te_reg_n, te_reg_cap;
    int te_ticking;

    /* Every tile entity this world ever held, in creation order: Java's
     * field_147482_g and the chunk maps only hold references, so an entity
     * that leaves a map is still owned by whatever list points at it and is
     * never freed twice. The registry gives the same guarantee in C: an
     * entity is registered when it first enters the world and freed exactly
     * once, at that world's world_free. It is per world: freeing one world
     * (serverreplay_load drops the populate world before adopting the
     * snapshot's) must not free another world's entities. */
    struct tile_entity **te_registry;
    int te_registry_n, te_registry_cap;

    /* The dimension the chunks come from: 0 the overworld (the default),
     * -1 the Nether (WorldProviderHell, hasNoSky), 1 the End
     * (WorldProviderEnd). Set before the first chunk loads. */
    int dim;
    struct nether nether;         /* the Nether provider, built from the seed */
    struct end end;               /* the End provider, built from the seed */
    unsigned nether_ready, end_ready;
    /* gen (the overworld provider) is built from the seed (world_gen): a
     * world that never generates an overworld chunk never builds its tables
     * (lane/coldaudit: the Nether's and the End's worlds, the saved copies) */
    uint8_t gen_made;

    /* Called for every block write that changed a block, after the write and
     * before the light pass, in call order: the native side of
     * netherite.oracle.Rows.onBlock. A probe installs one to record what a
     * feature writes; NULL otherwise. */
    void (*on_block)(void *ctx, int x, int y, int z, int id, int meta);
    void *on_block_ctx;
    /* the flags of the write in progress (World.setBlock's, 2 sends it to
     * the client), for on_block's listener */
    int write_flags;

    /* Called for every entity a self-drop spawns (BlockReed's
     * dropBlockAsItem on the paths a placement can fire), with the world
     * Random and the Math.random stream already advanced; NULL otherwise. */
    void (*on_drop)(void *ctx, const struct drop_ent *e);
    void *on_drop_ctx;

    /* BlockPressurePlate.onEntityCollidedWithBlock, Entity.func_145775_I's
     * per-cell call for a plate cell: the press path (meta 0 only) runs
     * func_150062_a's count, meta write, neighbor notifies and the scheduled
     * release tick. ctx is the replay (the count queries its entity lists).
     * NULL otherwise. */
    void (*on_plate)(void *ctx, int x, int y, int z, int id);
    void *on_plate_ctx;

    /* The oracle's ChunkProviderServer.loadChunkOnProvideRequest = false (the
     * server tick probe's setting): a read or a write outside the loaded set
     * sees an EmptyChunk instead of generating one, so air, light 0, sky
     * invisible, precipitation height 0, and a write there is dropped. Off is
     * vanilla's generate-on-demand. */
    int no_generate;

    /* World.isRemote: a client world. Its scheduleBlockUpdate is World's
     * empty one (only WorldServer keeps a pending-tick list), so a client
     * prediction's neighbour updates schedule nothing. */
    int is_remote;

    /* WorldServer.field_147490_S, the block events a tick queues
     * (func_147452_c drops one equal to a queued one) and func_147488_Z
     * delivers at the end of the next WorldServer.tick. Only the events
     * with a server-side effect are kept: the chest's and the ender
     * chest's event 1, whose receiveClientEvent sets the tile's player
     * count to the parameter queued. bev_on: a live server world (the
     * whole-server replay's); a probe world queues nothing. */
    int bev_on;
    int bev_n;
    int32_t bev[WORLD_BEV_MAX][6];

    /* ChunkProviderServer.provideChunk for a world_load_chunk that misses
     * (a read or a write, with generation on, that reaches a chunk that is
     * not loaded): the whole-server
     * replay's provider loads it (the saved copy or a new one, the populate
     * rule, the unload-set removal) and returns it. NULL: generate in place. */
    struct chunk *(*provide)(void *ctx, int cx, int cz);
    void *provide_ctx;

    /* World.setBlock's attempt, before the chunk decides whether anything
     * changes, and the matching return: the native side of
     * netherite.oracle.Rows.onAttempt / onAttemptEnd, so a probe can record an
     * attempt whose write changed nothing (it still runs the light pass) and
     * keep the capture in attempt order. NULL otherwise. */
    void (*on_attempt)(void *ctx, int x, int y, int z, int id, int meta);
    void (*on_attempt_end)(void *ctx, int x, int y, int z, int id, int meta);

    /* Called once for every chunk world_load_chunk generates, after the chunk
     * is inserted: the populate lane's hook for the four structure maps, which
     * every generated chunk extends (MapGenBase.func_151539_a), in load order.
     * NULL otherwise. */
    void (*on_chunk)(void *ctx, int cx, int cz);
    void *on_chunk_ctx;

    /* The overworld chunks world_insert_chunk inserted without generating,
     * packed chunk_key()s in insertion order. The provider carries state from
     * one generation to the next (the biome objects' topBlock, surface.h), so
     * the first generation after them runs theirs first, into scratch, while
     * gen.owed still says the provider was not rebuilt since. */
    int64_t *owed;
    size_t nowed, owedcap;

    /* the chunk requests (world_chunk_request) and the backend serving
     * them, NULL for chunk_backend_c */
    struct chunk_reqlog reqs;
    const struct chunk_backend *backend;
    void *backend_ctx;            /* the backend's own state (genahead.h) */
};

void world_init(struct world *w, int64_t seed);
/* world_init for a world the environment makes while it ticks (the replay's
 * region store): its chunk map and load order in the environment's slab */
void world_init_slab(struct world *w, int64_t seed);
void world_free(struct world *w);
void world_te_registry_move(struct world *dst, struct world *src);
/* The region store's spill file (regionspill.h) keeps a parked chunk's tile
 * entities as bytes: they leave their world's registry when they go there
 * (unregister: 1 when w held te) and join the loading world's when they come
 * back. listed: te is in w's tick list or its pending additions. */
void world_te_register(struct world *w, struct tile_entity *te);
int world_te_unregister(struct world *w, struct tile_entity *te);
int world_te_listed(const struct world *w, const struct tile_entity *te);

/* ChunkCoordIntPair.chunkXZ2Int. */
int64_t chunk_key(int cx, int cz);

/* ChunkProviderServer.loadChunk with Probe.rawChunks set: provide the chunk,
 * insert it, no population. Generates on a miss. Appends the key to the
 * provider's load order. */
struct chunk *world_load_chunk(struct world *w, int cx, int cz);

/* The one way a chunk enters a server world and the one way generation runs
 * (see chunk_req_kind): records the request, then serves it. given is the
 * chunk CREQ_STORED and CREQ_SUPPLIED insert ((cx, cz) are its own; (cx, cz)
 * must not be loaded); biome is CREQ_SUPPLIED's (world_insert_chunk). Returns
 * the chunk (CREQ_GENERATE's loaded one when it is), NULL for CREQ_OWED.
 * world_put_chunk is for the chunk stores and client copies, never a server
 * world's load. */
struct chunk *world_chunk_request(struct world *w, int kind, int cx, int cz, struct chunk *given,
                                  const uint8_t *biome);

/* The provider state a generation of a chunk in w leaves, applied without
 * generating: the overworld's surface.top entries the chunk's columns wrote
 * (tops, by the chunk's 256 biome ids) and its provider rand; the Nether's or
 * the End's provider rand (built first when it is not). rand is the Random's
 * internal 48-bit state. */
void world_gen_state(struct world *w, const uint8_t *biome, const uint8_t *tops, uint64_t rand);

/* The Nether's (dim -1) or the End's (1) provider of w, built from w's seed on
 * first use; the raw array of chunk (cx, cz) (DIM_CELLS ids, index
 * x << 11 | z << 7 | y) through it; and the Chunk constructor over a raw
 * array (the whole chunk: its bands, biome and height maps). */
void world_dim_provider(struct world *w, int dim);
void world_dim_raw(struct world *w, int cx, int cz, int dim, uint16_t *blocks);
void world_dim_construct(struct chunk *c, int cx, int cz, int dim, const uint16_t *blocks);

/* world_load_chunk without the provide hook: the generator's chunk, inserted.
 * The provider's own miss path. */
struct chunk *world_generate_chunk(struct world *w, int cx, int cz);

/* world_generate_chunk for a chunk whose content the caller supplies (a
 * snapshot's recorded bytes): the same insertion, load order, stronghold note
 * and on_chunk hook, with a zeroed chunk (cx, cz and the dimension's no_sky
 * set) in place of the generator's, carrying biome (256 ids) or, when biome
 * is NULL, what world_chunk_generated_meta gives. The generation it skips
 * still counts for the provider state the next generation sees: the Nether's
 * and the End's depend only on the last chunk, which world_owed_last
 * generates; the overworld's are paid by the next world_generate_chunk. */
struct chunk *world_insert_chunk(struct world *w, int cx, int cz, const uint8_t *biome);

/* world_insert_chunk for a chunk the caller allocated (zeroed but for cx, cz
 * and the bytes it read in) and hands over; (cx, cz) must not be loaded. */
struct chunk *world_insert_chunk_at(struct world *w, struct chunk *c, const uint8_t *biome);

/* What world_generate_chunk would have left in c that recorded chunk bytes
 * do not carry: the provider's biome array and the queuedLightChecks the
 * dimension's new chunk starts with (4096, the Nether's 0). */
void world_chunk_generated_meta(struct world *w, struct chunk *c);

/* w's overworld provider, built from w->seed at its first use (its owed
 * flag, gen.owed, is read and written before without it). */
struct chunkgen *world_gen(struct world *w);
/* ExtendedBlockStorage.tickRefCount for every section, from the ids. */
void chunk_count_ticking(struct chunk *c);

/* The generator state the Nether or End provider would hold after generating
 * the last chunk world_insert_chunk inserted: that one generation, into
 * scratch. Nothing for the overworld (its debt is paid lazily). */
void world_owed_last(struct world *w);

/* ChunkProviderServer.unloadQueuedChunks' body for one chunk: onChunkUnload,
 * save, loadedChunks.remove, the map removal. 1 when the chunk was loaded. */
int world_unload_chunk(struct world *w, int cx, int cz);
struct chunk *world_take_chunk(struct world *w, int cx, int cz);
void world_put_chunk(struct world *w, struct chunk *c);

/* The loaded chunk at (cx, cz), or NULL; world_chunk_loaded is the
 * chunkExists check the light engine's guards use. */
/* A write to c that bypassed World.setBlock and the light setter (a client
 * copy's cells): its wseq moves, as every other write's does. */
static inline void world_note_write(struct world *w, struct chunk *c)
{
    c->wseq = ++w->wseq;
}

struct chunk *world_chunk(struct world *w, int cx, int cz);
int world_chunk_loaded(struct world *w, int cx, int cz);

/* World.setBlock(x, y, z, Block.getBlockById(id), meta, flags); 1 when the
 * block changed, exactly the Java return value. A flag-1 write runs
 * notifyBlockChange, so the six neighbours' onNeighborBlockChange fire, and
 * every stored block runs onBlockAdded (blockcb.c, the chest in world.c).
 * Flag 2 only marks the block for the clients and flag 4 only skips the
 * render half, so neither has a world effect. */
int world_set_block(struct world *w, int x, int y, int z, int id, int meta, int flags);

/* WorldClient.func_147492_c: a block packet's setBlock(x, y, z, block, meta,
 * 3) on a client world. Chunk.func_150807_a's client half (the store, the
 * height map and sky light, a replaced tile entity dropped; no breakBlock or
 * onBlockAdded) and World.setBlock's light pass. 0 when the chunk is not
 * loaded or the cell already held the block and metadata. The client's own
 * new tile entity is left to the demand lookup. */
int world_client_set_block(struct world *w, int x, int y, int z, int id, int meta);

/* World.setBlockMetadataWithNotify(x, y, z, meta, flags): the metadata only
 * path, without onBlockAdded and without lighting. A flag-1 call notifies the
 * neighbours like setBlock does. 1 when the metadata changed. */
int world_set_meta(struct world *w, int x, int y, int z, int meta, int flags);

/* world_set_meta for flags without bit 1 (2, 4): the store and the write
 * listener, no notification, so no callback can run. The block callbacks
 * (blockwl.h) call this for their quiet writes. */
int world_set_meta_quiet(struct world *w, int x, int y, int z, int meta);

/* World.getBlock / getBlockMetadata / getSavedLightValue / canBlockSeeTheSky,
 * in world coordinates. getBlock and canBlockSeeTheSky generate the chunk they
 * land in when it is missing, as vanilla does; getSavedLightValue reads the
 * default value for a chunk that is not loaded. */
int world_get_block_slow(struct world *w, int x, int y, int z);
int world_column_find_down(struct world *w, int x, int z, int ytop, int id);
int world_get_meta_slow(struct world *w, int x, int y, int z);
int world_get_light(struct world *w, int type, int x, int y, int z);

/* World.getBlock: inline when (x, z) is inside the world's bounds and its
 * chunk is the one in the map's near cache (world.c chunk_find), which is
 * loaded; world_get_block_slow otherwise. */
static inline int world_get_block(struct world *w, int x, int y, int z)
{
    if ((unsigned)x + 30000000u < 60000000u && (unsigned)z + 30000000u < 60000000u && (unsigned)y < 256u)
    {
        int cx = x >> 4, cz = z >> 4, k = (cx & 15) | (cz & 15) << 4;
        chunkref r = w->near[k].ref;

        if (r != 0 && w->near[k].key == WORLD_CKEY(cx, cz)) return chunk_xyz_id(chunk_ptr(r), x & 15, y, z & 15);
    }

    return world_get_block_slow(w, x, y, z);
}

/* World.getBlockMetadata, inline in the same case */
static inline int world_get_meta(struct world *w, int x, int y, int z)
{
    if ((unsigned)x + 30000000u < 60000000u && (unsigned)z + 30000000u < 60000000u && (unsigned)y < 256u)
    {
        int cx = x >> 4, cz = z >> 4, k = (cx & 15) | (cz & 15) << 4;
        chunkref r = w->near[k].ref;

        if (r != 0 && w->near[k].key == WORLD_CKEY(cx, cz)) return chunk_xyz_meta(chunk_ptr(r), x & 15, y, z & 15);
    }

    return world_get_meta_slow(w, x, y, z);
}

/* The loaded chunk at (cx, cz) when it is the one in the map's near cache
 * (which holds loaded chunks only), else NULL: a miss says nothing, ask
 * world_chunk. */
static inline struct chunk *world_chunk_near(const struct world *w, int cx, int cz)
{
    int k = (cx & 15) | (cz & 15) << 4;
    chunkref r = w->near[k].ref;

    return r != 0 && w->near[k].key == WORLD_CKEY(cx, cz) ? chunk_ptr(r) : NULL;
}

/* A loaded chunk's cell, local x and z, y in 0..255: what World.getBlock and
 * getBlockMetadata read there (air and 0 in a missing section). */
static inline int chunk_get_block(const struct chunk *c, int x, int y, int z)
{
    return chunk_xyz_id(c, x, y, z);
}

static inline int chunk_get_meta(const struct chunk *c, int x, int y, int z)
{
    return chunk_xyz_meta(c, x, y, z);
}
int world_can_block_see_the_sky(struct world *w, int x, int y, int z);

/* World.getFullBlockLightValue: the greater of the sky and block light at the
 * position (Chunk.getBlockLightValue with darkness 0), what BlockMushroom's
 * canBlockStay reads. */
int world_full_block_light_value(struct world *w, int x, int y, int z);

/* World.getTileEntity / Chunk.func_150806_e: the tile entity at a position,
 * made on demand when the block there is a tile-entity block and the store
 * has none yet, NULL otherwise (and NULL when the found one is invalid, which
 * also takes it off the chunk map). Demand creation appends the entity to the
 * world list, as World.setTileEntity does outside the walk.
 * World.setTileEntity puts it through Chunk.func_150812_a, which drops
 * whatever sat at the position. */
struct tile_entity *world_tile_entity(struct world *w, int x, int y, int z);

/* WorldServer.func_147452_c on a live server world (bev_on): the event
 * joins the tick's queue unless an equal one is queued. */
void world_block_event(struct world *w, int x, int y, int z, int block, int id, int param);
/* WorldServer.func_147488_Z: every queued event whose block still stands
 * reaches onBlockEventReceived (the chest's and ender chest's
 * receiveClientEvent: event 1 sets the player count), in queue order. */
void world_block_events_run(struct world *w);

/* World.setTileEntity: append to the world list (or to field_147484_a during
 * the walk, dropping the same-position entry) and Chunk.func_150812_a, which
 * validates and replaces. */
void world_set_tile_entity(struct world *w, int x, int y, int z, struct tile_entity *te);

/* World.removeTileEntity: during the walk the found entity invalidates and
 * leaves field_147484_a; otherwise it leaves both world lists and its chunk's
 * map. */
void world_remove_tile_entity(struct world *w, int x, int y, int z);

/* World.func_147457_a (Chunk.onChunkUnload's removal mark): the entity
 * invalidates and leaves the world list at the walk's tail. */
void world_mark_tile_entity_gone(struct world *w, struct tile_entity *te);

/* World.setBlockMetadataWithNotify(x, y, z, meta, flags): Chunk.setBlockMetadata
 * and the write listener, which sees id -1 the way netherite.oracle.Rows.onBlock
 * does. No light pass (vanilla has none here); the flags 2 and 1 halves are not
 * ported, and nothing on the population path observes them. */
int world_set_meta(struct world *w, int x, int y, int z, int meta, int flags);

/* World.doChunksNearChunkExist(x, y, z, d): every chunk in the d block box
 * around (x, y, z) is loaded. */
int world_do_chunks_near_chunk_exist(struct world *w, int x, int y, int z, int d);

/* World.checkChunksExist: every chunk the box spans is loaded, and the y ends
 * are inside the world. Entity.func_145775_I guards its cell loop with this. */
int world_check_chunks_exist(struct world *w, int x0, int y0, int z0, int x1, int y1, int z1);
/* world_check_chunks_exist(w, x - 32, 0, z - 32, x + 32, 0, z + 32), the
 * check World.updateEntityWithOptionalForce makes for every entity, answered
 * from a memo while the chunk map has not changed (map_ver) */
int world_entity_area_loaded(struct world *w, int x, int z);

/* World.notifyBlocksOfNeighborChange: the six neighbours of a position learn
 * it changed, onNeighborBlockChange each with `source` as the block argument.
 * The sweep a flag-1 setBlock runs, callable without a write. */
void world_notify_neighbors(struct world *w, int x, int y, int z, int source);

/* World.getCollidingBoundingBoxes' block scan for an entity, the box list
 * Entity.moveEntity sweeps against. The entity's own boxes and the other
 * entities in the world are not added: the probe world holds exactly one
 * entity, whose getCollisionBox returns null. Appends to l, which the caller
 * clears. */
void world_get_colliding_bounding_boxes(struct world *w, struct aabb box, struct collide_list *l);

/* ...isEmpty() over the same scan. */
int world_colliding_boxes_empty(struct world *w, struct aabb box);

/* World.func_147470_e: fire or lava anywhere in the box. */
int world_is_in_fire(struct world *w, struct aabb box);

/* World.getHeightValue: the chunk's heightMap, 0 for a chunk that is not
 * loaded and 64 outside the world's x/z range. The move probe's teleports read
 * it. */
int world_get_height_value(struct world *w, int x, int z);

/* World.getBiomeGenForCoords(x, z): the chunk's blockBiomeArray (chunk.biome,
 * filled by provide_chunk), or the biome layer at the column for a chunk that
 * is not loaded, as WorldChunkManager.getBiomeGenAt does. */
int world_get_biome(struct world *w, int x, int z);

/* World.getChunkHeightMapMinimum: the chunk's heightMapMinimum, 0 for a chunk
 * that is not loaded. */
int world_get_height_map_minimum(struct world *w, int x, int z);

/* World.getPrecipitationHeight -> Chunk.getPrecipitationHeight, memoized in the
 * chunk's precipitationHeightMap. 0 for a chunk that is not loaded. */
int world_get_precipitation_height(struct world *w, int x, int z);

/* World.getFullBlockLightValue -> Chunk.getBlockLightValue with
 * World.skylightSubtracted subtracted from the sky, 0 for a chunk that is not
 * loaded. */
int world_get_full_block_light_value(struct world *w, int x, int y, int z, int skylight_subtracted);

/* World.func_147451_t: the sky pass then the block light pass at one position. */
int world_check_light_at(struct world *w, int x, int y, int z);
/* Chunk.func_150811_f: column (x, z) of c (local) re-lit down from its
 * top section, 0 when a check fails (func_150809_p's column pass) */
int world_light_column_check(struct world *w, struct chunk *c, int x, int z);
/* World.updateLightByType (func_147463_c) itself, below the capture hook
 * (lightcap.h) and the profiler's sub-mark; 0 when the 17-block box around the
 * cell is not loaded */
int world_light_update_raw(struct world *w, int type, int x, int y, int z);

/* Chunk.func_150804_b(false): the gap re-check over a chunk whose
 * isGapLightingUpdated is set, then the light-populated pass over a
 * terrain-populated chunk. */
void chunk_tick_flags(struct world *w, struct chunk *c);

/* World.canBlockFreeze / World.func_147478_e (the snow check) and
 * Block.fillWithRain, over the cold biomes' ice and snow. */
int world_can_block_freeze(struct world *w, int x, int y, int z, int naturally);
int world_can_snow_at(struct world *w, int x, int y, int z, int check);
void world_fill_with_rain(struct world *w, jrand *rand, int x, int y, int z);

/* Chunk.func_150804_b's recheckGaps(false) and the enqueueRelightChecks cursor.
 * Both are chunk state the server tick advances. */
void chunk_recheck_gaps(struct world *w, struct chunk *c, int is_client);
void chunk_enqueue_relight_checks(struct world *w, struct chunk *c);

/* The client world's deferred light (lightdefer.h): before the host writes a
 * client world's chunks outside world.c (a chunk's cells copied or shared in),
 * the pending calls run. A no-op without a lightdefer or on a server world. */
void world_light_sync(struct world *w);
struct ld_op;
/* lightdefer.h's C executor: the operations run on w's own light engine */
int world_light_defer_run_c(struct world *w, const struct ld_op *ops, size_t n);
/* Chunk.generateHeightMap (a chunk packet's fillChunk ends in it). */
void chunk_generate_height_map(struct chunk *c);

/* The Chunk(World, Block[], byte[], int, int) constructor and
 * Chunk.generateSkylightMap, over chunkgen's raw block array. */
void chunk_construct(struct chunk *c, int cx, int cz, const uint16_t *ids, const uint8_t *metas);
void generate_skylight_map(struct chunk *c);

#endif
