/* Minecraft's binary NBT, the bytes netherite.oracle.Rows.nbtBytes hands to
 * SHA-1: CompressedStreamTools.write(NBTTagCompound, DataOutput), i.e. the tag
 * byte 10, an empty name and NBTTagCompound.write's payload, which walks the
 * compound's HashMap. The iteration order of a JDK 8 HashMap is bucket index
 * ascending with insertion order inside a bucket, and the bucket is
 * (hash ^ hash >>> 16) & (capacity - 1) over String.hashCode, with the
 * capacity 16 doubled while size > capacity * 0.75. Both halves are needed, so
 * the writer reads the tree's fields in the order they were put.
 */
#ifndef NETHERITE_NBTBIN_H
#define NETHERITE_NBTBIN_H

#include <stddef.h>
#include <stdint.h>
#include "nbtjson.h"

typedef struct nbtbin {
    uint8_t *p;
    size_t n, cap;
} nbtbin;

/* One nbtbin_long_memo slot: the scratch bytes and root of the build it last
 * hashed (raw NULL when that build was not wholly scratch), the bytes it
 * wrote, the SHA-1 state after each whole 64-byte block of them (st[k] after
 * k blocks, k = 0 .. n / 64) and their value. */
struct nbtbin_memo_slot {
    uint8_t *raw;
    size_t rawn, rawcap;
    const struct nbt *root;
    uint8_t *p;
    size_t n, cap;
    uint32_t (*st)[5];
    size_t stcap;
    uint64_t v;
};
#define NBTBIN_MEMO_SLOTS 4096

/* What an nbtbin keeps allocated past its bytes (nbtbin.c's reserve): nbtw's
 * binary output copies in whole 16-byte pieces, which may write (and read)
 * up to 15 bytes past what it counts. */
#define NBTBIN_SLACK 64

/* One field of a compound nbtw.h's binary output is writing: where its tag
 * byte is and its key, for the HashMap order when the compound closes. */
struct nbtw_field {
    size_t off;
    const struct nbt_key *key;
};

/* A compound nbtw.h's binary output closed, for its fields' order: its
 * field records from first in the closed records, n of them, and where its
 * last field ends. */
struct nbtw_closed {
    int first, n;
    size_t end;
};

/* The HashMap order of one sequence of keys, which a compound written the
 * same way row after row repeats (nbtbin.c's order cache). */
#define NBTW_ORDER_MAX 64
#define NBTW_ORDER_SLOTS 512
struct nbtw_order {
    uint64_t sig;
    int n, moved;
    const struct nbt_key *keys[NBTW_ORDER_MAX];
    uint8_t order[NBTW_ORDER_MAX];
};

void nbtbin_init(nbtbin *b);
void nbtbin_free(nbtbin *b);

/* The whole entity tag: 0x0A, writeUTF(""), then the compound's payload. */
void nbtbin_write_root(const nbt *root, nbtbin *b);

/* Rows.nbtLong: the first eight bytes of SHA-1 over nbtbin_write_root, read
 * big-endian. */
uint64_t nbtbin_long(const nbt *root);

/* nbtbin_long through a memo of the last root hashed under key (a slot picked
 * by key's low bits, so any int works; an entity id per entity). A root built
 * wholly in the nbt scratch region from the same scratch bytes and at the
 * same address as the slot's last one is the same tree (nbtjson.h) and
 * returns the slot's value; otherwise the tree is written, bytes equal to the
 * slot's last return its value, and else SHA-1 resumes from the slot's state
 * after the longest run of leading 64-byte blocks equal to those. The value
 * is always the SHA-1 of this root's bytes; the key only decides what it is
 * compared with. */
uint64_t nbtbin_long_memo(int key, const nbt *root);

#endif