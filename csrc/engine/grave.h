/* Dead livings, released once no memory refers to them.
 *
 * A living that dies leaves its world's list (an_tick_one), but Java keeps
 * the object while anything still points at it: another mob's attack or
 * revenge target, a rider, an arrow's shooter, a saved chunk's live
 * instance. The native engine keeps those pointers the same way, so a dead
 * living cannot be freed when it leaves the list; before this every one
 * stayed allocated (42 KB each, 153,452 of them in swamp-night-s2).
 *
 * grave_bury takes the living and its list entry. grave_collect, called at a
 * quiet point (between rows, with no entity update on the stack), frees the
 * buried ones that nothing points at once they pass a size threshold: a
 * conservative scan of every writable anonymous mapping, the program's data
 * and the stack for a word that points anywhere inside one (the embedded
 * struct entity included), then of every buried one kept, to a fixed point.
 * No pointer to a freed one exists anywhere, so no later comparison or read
 * can tell. Off until grave_enable (the whole-server replay harness, Linux
 * x86-64 only); single-threaded use only. */
#ifndef NETHERITE_GRAVE_H
#define NETHERITE_GRAVE_H

#include <stddef.h>
#include <stdint.h>

struct living;
struct an_ent;
struct env;

/* One environment's buried livings (struct env's grave): each with its
 * list entry, and the sweep's trigger. The records are a fixed array of the
 * environment's (arena.h fixed_array: its image's, for an image). */
struct grave_rec {
    uint64_t livh;             /* living pool slot and generation, resolved in the current image */
    uint32_t enth;             /* list-entry slot + 1 (0 none) */
    uint32_t engen;            /* its generation when buried */
};
struct grave_env {
    struct grave_rec *recs;
    size_t nrecs, caprecs;
    size_t buried_bytes, next_sweep;
    int hold;                  /* no sweep of this image while set (grave_sweep_joint ends it) */
};

/* Start burying, sweeping each time an environment's buried bytes pass
 * threshold. A sweep frees the current environment's buried livings. */
void grave_enable(size_t threshold);
/* grave_enable for environments that are images (image.h) ticking on
 * several threads at once (csrc/runtime's pool), sweeping each time an
 * environment has buried that many livings: a sweep reads the current
 * environment's image alone (its struct env, pools, list slab and heap: all
 * the tick keeps; the pages written, by /proc/self/pagemap), with the
 * thread's own scratch. */
void grave_enable_images(size_t livings);
/* The pool's count: a village env buries a living about every seven ticks
 * (hostiles despawned, sheep with their unloading chunks), and a sweep of its
 * image's written pages takes about 12 ms (2026-09-29, lane/entmem: 140 ms
 * when it read the heap's whole reservation), so every 256 burials costs
 * half what the 32 MB threshold (about 1,600 livings) did and the livings
 * pool's high-water mark falls from about 1,650 slots to about 550. */
#ifndef GRAVE_IMAGE_LIVINGS   /* a stress build's (lane/livfault: -DGRAVE_IMAGE_LIVINGS=4, a sweep every 4 burials) */
#define GRAVE_IMAGE_LIVINGS 256
#endif
/* Keep the grave off for the process (grave_enable does nothing after it):
 * test_interleave's two environments on two threads share its records, whose
 * livings go back to the pools of whichever environment sweeps, and a sweep
 * reads every thread stack, whose guard pages (MADV_GUARD_INSTALL) are
 * listed as rw-p in /proc/self/maps and fault. */
void grave_forbid(void);
/* The harnesses' threshold: a sweep reads the process's writable memory
 * (about 220 M instructions for gold-g15-s42), so it waits for about 1,400
 * buried livings; livings that leave with an unloading chunk are buried too. */
#define GRAVE_THRESHOLD ((size_t)32 << 20)
/* Hold sweeps off (on 1) while another thread may map or unmap memory a
 * sweep reads between its /proc/self/maps read and its scan: play's device
 * renderer opening on a thread of its own (dlopen, the device's context).
 * Burials go on; the first grave_collect past the hold (on 0, from any
 * thread) sweeps. */
void grave_hold(int on);
void grave_bury(struct living *l, struct an_ent *en);
/* Memory whose words are not references, for the scan to skip: a cache
 * keyed by an address that re-checks every hit by content (nbtjson's
 * interned-key cache holds names inside livings). At most 8. */
void grave_ignore(const void *p, size_t len);
void grave_collect(void);
/* Two images built to hold the same state (a start and its twin, the two
 * replays of a save: csrc/runtime/pool.c reads their pointers off the words
 * that differ between them, which needs every pool slot to hold the same
 * thing in both). A sweep keeps what a stale word happens to name, and the
 * stale words differ from one image to the other, so independent sweeps
 * free different livings and the free lists diverge (lane/crash2). Such an
 * image is built with grave_hold_env(1) (its sweeps wait, its burials go
 * on); grave_sweep_joint then sweeps both once: a record is freed only if
 * neither image's scan reaches it, in the same order in both, and ends the
 * hold. Images that are not both held with the same records are left alone
 * (their hold ended, nothing freed). */
void grave_hold_env(int on);
void grave_sweep_joint(struct env *a, struct env *b);
/* The same sweep while the pair is still being built, for images that hold (grave_hold_env) and are stepped
 * in step: call it between the same steps of both; once the pair's burials pass what one image's sweep waits for
 * it sweeps the two together and keeps the hold. What is buried at a time is bounded by that threshold, not by the
 * length of the build. */
void grave_pair_collect(struct env *a, struct env *b);

#endif
