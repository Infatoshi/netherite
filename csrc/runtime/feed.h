/* The render feed (pool.h struct pool_feed): per env, what changed in the
 * client's world since the last feed, as one block of plain data. */
#ifndef NETHERITE_RUNTIME_FEED_H
#define NETHERITE_RUNTIME_FEED_H

#include <stddef.h>
#include <stdint.h>

struct pool_obs;
struct session;
struct env;
struct pool_feed_state;

struct pool_feed_state *feed_new(const struct pool_obs *obs);
void feed_free(struct pool_feed_state *f);
/* after a reset: the next feed carries everything in range (reset 1) */
void feed_reset(struct pool_feed_state *f, struct session *ss, struct env *e);
/* after each tick of a step */
void feed_tick(struct pool_feed_state *f, struct session *ss);
/* the step's feed: a block of *bytes at the returned address, valid until the
 * next feed_finish or feed_reset */
const void *feed_finish(struct pool_feed_state *f, struct session *ss, size_t *bytes);

/* A section as the feed carries it (pool.h struct pool_feed_section), and
 * its content hash (0 for an empty one): what the gate's feed check
 * rebuilds the client's world from. */
struct pool_feed_section;
struct chunk;
void feed_section_fill(struct pool_feed_section *o, const struct chunk *c, int s);
uint64_t feed_section_hash(const struct pool_feed_section *o);

#endif
