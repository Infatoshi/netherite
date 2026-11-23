/* The region stores' parked chunks on disk: vanilla's save files.
 *
 * A chunk that left the view goes to its dimension's region store
 * (serverreplay.h sr_dimstate.saved), packed between rows (chunk_pack), and
 * comes back byte for byte at its next load. Held in memory, the stores grow
 * with every chunk a player ever walked past (lane/envmem, 2026-09-29:
 * 5.6 KB a chunk, 1.7 MB a second an exploring env at N 8, and the
 * environment's 131,072 chunk slots full in about seven minutes). So at the
 * park a packed chunk without tile entities leaves memory for the
 * environment's spill file: its struct (compressed, lzpack.h) and its packed blob as one
 * record, appended, and its slot goes back to the pool. The store's index
 * (a map per dimension from the chunk's coordinates to its record, 24 bytes
 * a chunk) stays; a load takes the record back (rspill_take) into a new slot,
 * unpacked, the struct as it was parked.
 *
 * The file is one per environment: made at its first spill in the directory
 * of rspill_flag (default ~/.cache/netherite-spill) and unlinked at once, so
 * it goes with the process. Records a load took back are garbage: past
 * RSPILL_COMPACT_MB of it, and more garbage than live records, the park
 * copies the live records to a new file. A copy of the environment (the
 * pool's reset from its start image) owns none of the start's file:
 * rspill_adopt copies the records it names into a file of its own.
 *
 * Only the park (between rows) writes; a load (in the tick) reads into the
 * buffer the writes sized, allocating nothing. */
#ifndef NETHERITE_REGIONSPILL_H
#define NETHERITE_REGIONSPILL_H

#include <stddef.h>
#include <stdint.h>

#include "nbtjson.h"

struct chunk;

#define RSPILL_COMPACT_MB 64
#define RSPILL_BEHIND_MB 4

struct rspill_rec {
    int32_t cx, cz;
    uint64_t off;             /* the record's offset in the file */
    uint32_t len;             /* its bytes */
    uint32_t state;           /* 0 empty, 1 a record, 2 taken back (a tombstone) */
};

struct rspill_map {
    struct rspill_rec *v;
    uint32_t cap, n, dead;    /* slots, records, tombstones */
    uint32_t nticks;          /* records holding a saved-ticks entry (the caller's count) */
};

struct region_spill {
    int mode;                 /* rspill_flag: 0 on, 1 off */
    const char *dir;          /* NULL: ~/.cache/netherite-spill */
    int fd1;                  /* the file's descriptor plus one; 0 none */
    uint64_t end, live;       /* the file's bytes; those of records still indexed */
    uint8_t *buf;             /* a record, read or written */
    uint32_t bufcap;
    uint8_t *xbuf;            /* its struct and extra bytes, decompressed (with
                               * the decoder's LZP_SLACK) */
    uint32_t xbufcap;
    uint32_t *ztab;           /* the compressor's table (lzpack.h) */
    uint8_t *sbuf;            /* an NBT string or int array read back */
    uint32_t sbufcap;
    struct rspill_map dims[3]; /* by sr_dim_index */
    uint64_t puts, takes, compactions;
    /* the blob file (rspill_blob_put): append-only, a blob's bytes garbage
     * once it is read back */
    int fd2;                  /* its descriptor plus one; 0 none, -1 failed */
    uint64_t end2, live2;
    /* each file's bytes written back and dropped from the page cache, and
     * those handed to writeback (the page cache counts against the run's
     * memory: every RSPILL_BEHIND_MB the writer hands the new stretch to
     * writeback and drops the one before it, without waiting on the disk) */
    uint64_t behind1, behind2, started1, started2;
};

/* --region-spill VALUE for the current environment: on, off or an absolute
 * directory (kept by pointer: argv's). 0 for anything else. */
int rspill_flag(const char *v);

/* Whether the store of dimension index dim has (cx, cz) on disk. */
int rspill_has(int dim, int cx, int cz);
/* Whether the parked chunk c can go to the file: spilling on, the file not
 * failed, c packed (no bands). Its tile entities are the caller's to write
 * into the extra bytes and free; the take leaves tes.v NULL (n and cap as
 * they were) for the caller to fill. */
int rspill_ok(const struct chunk *c);
/* The parked chunk c (packed, in no world) to the file with extra_n bytes
 * of the caller's (the store's records of the chunk: its tile entities,
 * saved entities and ticks): 1, and c freed (its tile entity array, not the
 * entities); 0 when it cannot go (spilling off, the file not made or not
 * written, unpacked bands), c untouched. */
int rspill_put(int dim, struct chunk *c, const void *extra, uint32_t extra_n);
/* The chunk (cx, cz) of dimension index dim back from the file, in a new
 * slot and unpacked, and its extra bytes (valid until the next call here);
 * NULL when the file does not hold it. */
struct chunk *rspill_take(int dim, int cx, int cz, const uint8_t **extra, uint32_t *extra_n);

/* Bytes of any owner (a structure start's pieces, populate.c) to the blob
 * file, compressed, outside the tick: the place to read them back (nonzero),
 * or 0 when they cannot go. get (in the tick, allocating nothing) decompresses
 * them into the spill's buffer, valid until the next call here, and marks
 * them garbage; *n their length. */
uint64_t rspill_blob_put(const void *p, uint32_t n);
const uint8_t *rspill_blob_get(uint64_t at, uint32_t *n);

/* The extra bytes' stream: a growing buffer (the park's) and the take's
 * reads, which stop the run on a short record. An NBT tree is its nodes in
 * pre-order (type, payload, a container's count; a compound's keys before
 * their values), written and read back without recursion, every compound's
 * fields in the order they were put. */
struct rspill_out {
    uint8_t *p;
    size_t n, cap;
};
void rspill_out_put(struct rspill_out *o, const void *p, size_t n);
void rspill_nbt_out(struct rspill_out *o, const nbt *root);
void rspill_in(const uint8_t **p, const uint8_t *end, void *out, size_t n);
uint32_t rspill_in_u32(const uint8_t **p, const uint8_t *end);
nbt *rspill_nbt_in(const uint8_t **p, const uint8_t *end);
/* The current environment's spill state as a copy of another's (the pool's
 * reset copied the start's image): its records into a file of its own. */
void rspill_adopt(void);
/* The file closed and the state cleared (an environment's end, or before
 * the pool's reset writes over it). */
void rspill_close(struct region_spill *s);
/* The index's bytes in memory, and the records' count. */
size_t rspill_index_bytes(const struct region_spill *s);
uint64_t rspill_records(const struct region_spill *s);

#endif
