/* SHA-1, the hash netherite.oracle.Rows.nbtLong takes the first eight bytes
 * of: a tape's d.ents and d.sp digests are built from it. */
#ifndef NETHERITE_SHA1_H
#define NETHERITE_SHA1_H

#include <stddef.h>
#include <stdint.h>

/* The digest of n bytes into out[20]. */
void sha1(const uint8_t *data, size_t n, uint8_t out[20]);

/* The same digest in steps, for a caller that keeps the state after a prefix
 * of whole 64-byte blocks: init, any number of sha1_blocks, then sha1_final
 * over the rem < 64 bytes left, with n the message's whole length. */
void sha1_init(uint32_t h[5]);
void sha1_blocks(uint32_t h[5], const uint8_t *data, size_t nblocks);
void sha1_final(uint32_t h[5], const uint8_t *tail, size_t rem, uint64_t n, uint8_t out[20]);

#endif
