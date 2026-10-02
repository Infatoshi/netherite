/* The native replay's whole server state against an oracle snapshot of the
 * same tick (a keyframe, or one the oracle wrote on demand with --snap-at),
 * grouped the way a divergence is read: world scalars and Det's streams,
 * entities by id (missing, extra, and each differing NBT path), chunks by
 * position (the cells and maps that differ, tile entities, inhabitedTime) and
 * each world's pending block ticks.
 *
 * The snapshot at tick T is the state at the head of tick T, which is the
 * native state after row T - 1: the caller runs the diff after its last
 * simulated row. */
#ifndef NETHERITE_STATEDIFF_H
#define NETHERITE_STATEDIFF_H

#include <stdio.h>

struct snapshot;
struct serverreplay;
struct server_player;

struct statediff_sum {
    int scalars, det, ents_missing, ents_extra, ents_differ, ents_order, chunks_missing, chunks_extra,
        chunks_differ, tiles_differ, ticks_differ;
    int first_entity;          /* the first differing entity's id (missing, extra or NBT), -1 none */
};

/* Loads DIR and compares; prints at most `lines` detail lines per group to
 * out (0: the group counts only). start is the snapshot the replay began
 * from (sr_entry_nbt reads the player's id there). Returns the number of
 * differing items, 0 when identical, -1 when DIR does not load. */
int statediff_run(const struct snapshot *start, struct serverreplay *sr, const struct server_player *sp,
                  const char *dir, int lines, FILE *out, struct statediff_sum *sum);

/* Every path at which two canonical NBT trees differ, one "path: want X got
 * Y" line each into out (at most max lines, prefix before each); returns the
 * number of differing leaves. */
int statediff_nbt(const void *want, const void *got, const char *prefix, int max, FILE *out);

#endif
