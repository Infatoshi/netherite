/* The byte codec of the bytes that never leave the process: a parked
 * chunk's sections (chunk_pack), a spill record's struct and extra bytes and
 * a spilled blob (regionspill.c). Every one is read back byte for byte by
 * this same build, so the format is ours to change.
 *
 * An LZ77 of LZ4's block form (lane/spillfast, 2026-09-29): a sequence is a
 * token (the literal count in its high nibble, the match length less 4 in
 * its low, 15 continuing in bytes of 255 and a last one below), the
 * literals, a 16-bit little-endian offset (1 to 65535) and the match
 * length's continuation; the last sequence is literals alone. The
 * compressor is greedy over a 4,096-entry table of five-byte hashes, with
 * an immediate second match tried at each match's end. On 1,506 parked
 * chunks of a village, an exploring and a Nether run (34.6 KB each raw)
 * it takes 3.9 user instructions a byte to compress and 3.0 to decompress
 * at a ratio of 6.5, where zlib's raw deflate at level 1 took 19.5 and 7.8
 * at 10.5 (out/perf/spillfast.tsv).
 *
 * Both sides allocate nothing: the compressor's table is the caller's, and
 * the decompressor writes up to LZP_SLACK bytes past what it decodes (its
 * copies are 8 and 16 bytes wide), so its buffer has that much more room
 * than the longest output it accepts. Little-endian hosts only (x86-64,
 * arm64): the match search compares eight bytes at a time. */
#ifndef NETHERITE_LZPACK_H
#define NETHERITE_LZPACK_H

#include <stdint.h>

#define LZP_HASH_LOG 12
#define LZP_TABLE_BYTES (sizeof(uint32_t) << LZP_HASH_LOG)
#define LZP_SLACK 32

/* The most bytes lzp_compress writes for n bytes of input. */
#define LZP_BOUND(n) ((n) + (n) / 255 + 16 + LZP_SLACK)

/* n bytes of in compressed into out (LZP_BOUND(n) bytes of room); table is
 * LZP_TABLE_BYTES of scratch. The compressed length. */
uint32_t lzp_compress(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t *table);

/* in (zn bytes) decompressed into out, which has cap + LZP_SLACK bytes of
 * room: the decoded length, or -1 when in is not a whole stream of at most
 * cap bytes. */
int64_t lzp_decompress(const uint8_t *in, uint32_t zn, uint8_t *out, uint32_t cap);

#endif
