/* The entity runtime state beside NBT (Snapshot.java's "rt"): see snap_runtime.c. */
#ifndef NETHERITE_SNAP_RUNTIME_H
#define NETHERITE_SNAP_RUNTIME_H

struct serverreplay;
struct snapshot;

/* Every loaded living's fields, data watcher and AI from its "rt" block,
 * after the whole entity list exists. 0 (sr->err set) when the native task
 * list does not match the snapshot's. */
int sr_apply_runtime(struct serverreplay *sr, const struct snapshot *snap);

/* The player twin's chunk stamp from the snapshot's slice index (after
 * serverreplay_bind_player). */
void sr_runtime_player_stamp(struct serverreplay *sr);

#endif
