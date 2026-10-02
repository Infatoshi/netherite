/* The region store's seed-world build, once per distinct input.
 *
 * A replay that joins a seed world (no checkpoint) fills its region store
 * from the seed world's spawn-area build (seedworld.h): 625 chunks
 * generated and populated, then one entity pass over the spawn creatures.
 * That is 0.8 to 1.7 s and 14 to 16 G instructions, and it is the same
 * for every replay with the same inputs. The region cache keeps the
 * build's result on disk as an image and every later replay with the same
 * key maps it instead of building.
 *
 * The key is a SHA-1 of everything that decides the build and nothing
 * else: the build hash (the Makefile's hash of csrc/engine, the compiler
 * and its flags, REGIONCACHE_BUILD), the struct sizes the image stores,
 * the seed, the quiet-join pin (whether the creatures take their entity
 * pass), the run_config (the negative checks change generation), and the
 * env state the build starts from and reads: the big-tree state (zero
 * since lane/bigtree, kept in the key all the same).
 *
 * The image is what the build leaves: every chunk of the seed world in its
 * slot order (the chunk struct, its sections as chunk_pack's deflated blob,
 * its tile entities, its pending ticks as ticks_chunk_updates returns
 * them), the creatures after their pass, and the env state the build
 * changes: the big-tree state, the entity chunk stamp's advance, and the
 * item tags it interned, as the texts of every tag
 * it looked up or made in first-use order (itemtag.c's journal), so that
 * interning them again on top of any tag table makes exactly the entries
 * the build would have made there; a tile entity's stack tags are stored
 * as positions in that list. What stays behind is scratch (the block
 * worklist's frames, the pack buffer), allocator state (the pool free
 * chains, which only decide addresses) and the light-cap statistics. The
 * liquid springs' shared arrays (BlockDynamicLiquid's instance fields,
 * rewritten before every read) are the caller's to put back after a build:
 * the replay's own liquid updates left them, as the Java server's did.
 *
 * A miss builds, captures the image and applies it exactly as a hit does,
 * so both run one path after the build. Entries live in
 * ~/.cache/netherite-regioncache (one file per key, written to a temporary
 * name and renamed; a flock per key makes a concurrent miss of the same
 * key wait and then hit), least recently used evicted past
 * REGIONCACHE_CAP_MB, every lookup logged to stats.tsv there (unix time,
 * hit, miss or verify, the key's first 12 hex digits, the milliseconds of
 * the lookup or of the build and its capture, the image's bytes).
 *
 * The switch is the environment's (regioncache_flag, the binaries' flag
 * --region-cache): on (the default), off (build every time, nothing on
 * disk), verify (build every time and stop, exit 3, when an existing
 * entry's bytes differ from the build's), an absolute directory (on, the
 * cache there) or verify:DIRECTORY.
 */
#ifndef NETHERITE_REGIONCACHE_H
#define NETHERITE_REGIONCACHE_H

#include <stddef.h>
#include <stdint.h>

#include "seedworld.h"
#include "ticks.h"
#include "world.h"

#define REGIONCACHE_CAP_MB 4000

struct rc_image {
    uint8_t key[20];
    int mode;                /* 0 off, 1 on, 2 verify */
    int fresh;               /* built by this process: the env holds the after-state */
    int lock_fd;             /* the key's flock while building, else -1 */
    char dir[1024];
    uint8_t *buf;            /* the image bytes */
    size_t n;
    int mapped;
    int64_t t0_ns, ns;        /* the lookup's start; the lookup or the build and capture */
    uint64_t stamp_before;   /* entity chunk stamp at the build's start */
    int bigtree_after[256];  /* the build's big-tree state (before the caller restores the snapshot's) */
    /* the parsed image */
    int nchunks, nents, njournal;
    size_t *chunk_off;       /* each chunk record's offset in buf */
    size_t ents_off;
    int *tagmap;             /* journal position -> this env's tag index */
    uint8_t *env_before;     /* verify: the env's bytes before the build */
};

/* --region-cache VALUE for the current environment: on, off, verify,
 * verify:DIR or an absolute DIR (kept by pointer: argv's). 0 for anything
 * else. */
int regioncache_flag(const char *v);

/* Look the build up (img zeroed by the caller, off any stack frame). 1: a hit, img holds the image (apply it with
 * regioncache_apply_env). 0: build now, then regioncache_capture: the key's
 * lock is held and the tag journal is on. */
int regioncache_begin(struct rc_image *img, int64_t seed, int quiet_join);

/* On a miss, right after seedworld_build: the big-tree state it left. */
void regioncache_note_bigtree(struct rc_image *img);

/* On a miss, after the creatures' pass: the image of sw (every chunk is
 * packed), stored or verified, the lock released. sw is the caller's to
 * free. */
void regioncache_capture(struct rc_image *img, const struct seedworld *sw);

/* On a hit: the env state the build would have left (the big-tree state,
 * the chunk stamp). Then, either way,
 * regioncache_tags: the journal's tags interned in order. */
void regioncache_apply_env(const struct rc_image *img);
void regioncache_tags(struct rc_image *img);

/* The image's chunks in the seed world's slot order. */
void regioncache_chunk_pos(const struct rc_image *img, int i, int *cx, int *cz);
/* A new chunk (chunk_new) with chunk i's struct, packed sections and tile
 * entities, in no world. */
struct chunk *regioncache_chunk(const struct rc_image *img, int i);
/* Chunk i's pending ticks (ticks_chunk_updates' order), copied to out
 * (malloc'd by the call, NULL for none); their count. */
int regioncache_ticks(const struct rc_image *img, int i, struct tick_entry **out);
/* Entity k of the creatures and items the build spawned. */
void regioncache_entity(const struct rc_image *img, int k, struct sw_entity *out);

/* Release the image; logs the lookup. After a build in verify mode it also
 * audits the env: any byte the build left changed outside what the image
 * carries (the big-tree state, the chunk stamp, the tags) and what only
 * decides addresses or is scratch (the pools, the block worklist, the pack
 * buffer, the NBT pools and memo, the stronghold's biome-box memo, the
 * light-cap and phase counters) stops
 * the run (exit 3): state a hit would not restore. The caller ends the
 * image after it has put back what it saved around the build. */
void regioncache_end(struct rc_image *img);

#endif
