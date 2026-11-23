/* A checkpoint start's save directory (lane/divlib).
 *
 * A checkpoint start replays from the checkpoint's own save (session.c
 * resolve_checkpoint: sr->save_dir, the AnvilChunkLoader region files every
 * chunk the snapshot does not hold is loaded from). Before lane/divlib the
 * replay took CPDIR/save on trust: a library whose checkpoints kept the save
 * packed (CPDIR/save.tar.zst, lane/killlib's harvest.sh --pack) had no
 * region files there, and every chunk it should have loaded was generated
 * afresh. Seed 42's End start left the Java client at its 95th tick: six
 * overworld chunks a path search loads, generated with none of their seven
 * saved entities, the seeder seven draws behind, the dragon's flight
 * another by t 1,567.
 *
 * cpsave_resolve gives CPDIR/save where it is a directory, and refuses
 * otherwise: the error names the path, and with CPDIR/save.tar.zst there
 * says it is packed and how to unpack it. Only a start that names a
 * checkpoint (its tape header's start.kind "checkpoint") reaches this: a
 * seed or join snapshot (start.kind spawn, join, absent) has no checkpoint
 * save and builds its region store from the seed world as before. 0 with
 * out set, or -1 with err. */
#ifndef NETHERITE_CPSAVE_H
#define NETHERITE_CPSAVE_H

#include <stddef.h>

int cpsave_resolve(const char *cpdir, char *out, size_t n, char *err, size_t en);

#endif
