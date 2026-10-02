/* The scheduled-update path (WorldServer.scheduleBlockUpdate and its
 * func_147454_a) with World.scheduledUpdatesAreImmediate, which
 * WorldGenLiquids turns on around the updateTick it runs at once. With the
 * flag up, every tick a dynamic liquid schedules runs immediately, so one
 * spring feature is a complete flow simulation.
 *
 * The pending list is the NextTickListEntry set (deduplicated by position and
 * block, in insertion order); the probe world never consumes it, but the
 * server tick port will, so scheduleBlockUpdate keeps filling it exactly as
 * vanilla does. */
#ifndef NETHERITE_TICKS_H
#define NETHERITE_TICKS_H

#include "jrand.h"
#include "world.h"

/* World.scheduledUpdatesAreImmediate. */
void ticks_set_immediate(int on);

/* BlockFalling.fallInstantly, set while a chunk provider populates. */
void ticks_set_fall_instantly(int on);

/* WorldServer.scheduleBlockUpdate -> func_147454_a(x, y, z, block, delay, 0).
 * Under the immediate flag a func_149698_L block (every registered block but
 * BlockFire, so every liquid) runs updateTick at once from the world's own
 * Random; anything else is delayed to 1 and joins the pending list. */
void ticks_schedule_block_update(struct world *w, int x, int y, int z, int block, int delay);

/* ticks_schedule_block_update from a block callback's worklist step
 * (blockwl.h): in place when nothing is pending and it cannot run an update
 * at once, otherwise emitted as the step's next call. */
void ticks_bwl_sched(struct world *w, int x, int y, int z, int block, int delay);

/* WorldServer.func_147454_a with its priority: the schedule a diode's
 * func_149897_b makes (-3, -2, -1 or 0; lower ticks first among entries due
 * at the same time). ticks_bwl_sched_priority is its worklist form. */
void ticks_schedule_priority(struct world *w, int x, int y, int z, int block, int delay, int priority);
void ticks_bwl_sched_priority(struct world *w, int x, int y, int z, int block, int delay, int priority);

/* WorldServer.func_147477_a: an entry equal to (x, y, z, block) still waits
 * in the batch tickUpdates is running (NextTickListEntry.equals: the
 * position and Block.isEqualTo, which pairs a diode's powered and unpowered
 * ids and the two redstone torches). 0 outside the batch. */
int ticks_scheduled_this_tick(int x, int y, int z, int block);

struct tick_entry;
/* servertick.c's tick_updates around its batch: the entries it took, then
 * the index of the one being ticked, then the end. */
void ticks_batch_begin(const struct tick_entry *batch, int n);
void ticks_batch_at(int i);
void ticks_batch_end(void);

/* Block.func_149738_a on the liquid blocks: water 5, lava 30 (the overworld
 * provider's hasNoSky is false; the nether's 10 is not ported yet). */
int ticks_liquid_tick_rate(const struct world *w, int block);

/* The same, reading BlockLiquid.func_149738_a's provider branch: lava is 10
 * where the provider isHellWorld (dim -1), 30 everywhere else. */
int ticks_liquid_tick_rate_world(const struct world *w, int block);

/* One NextTickListEntry: the position, the block, the scheduled time (the
 * world's total time the delay was added to), the priority and the entry id
 * NextTickListEntry.nextTickEntryID hands out, which breaks ties. The pending
 * set is a min-heap on (time, priority, entry), which is the TreeSet's own
 * total order, plus a position+block set for the HashSet dedupe. */
struct tick_entry { int x, y, z, block; int64_t time; int priority; int64_t entry; };

/* WorldServer's pendingTickListEntriesTreeSet / HashSet: cleared and the entry
 * id counter set (a snapshot load restores both). */
void ticks_reset(int64_t next_entry_id);

/* One entry a snapshot load hands back: the pending set with no scheduling
 * rule applied, exactly the entry the recording holds. */
void ticks_load_entry(const struct tick_entry *e);
int64_t ticks_next_entry_id(void);
void ticks_set_next_entry_id(int64_t id);   /* the static counter, shared by every world */

/* World.rand and worldInfo.getWorldTotalTime() as WorldServer.func_147454_a
 * reads them: the immediate branch's updateTick and the delay's base. The
 * servertick sets them once per tick. */
void ticks_set_rand(jrand *r);
jrand *ticks_get_rand(void);
void ticks_set_total_time(int64_t t);

/* The pending set. count is the TreeSet's size; peek writes the first n
 * entries in tree order (0..n-1, n < count) and returns how many; pop removes
 * the smallest and writes it, 0 when empty; walk calls cb over every entry in
 * tree order. */
int ticks_pending_count(void);
/* a peek of up to TICKS_PEEK_MAX entries allocates nothing */
#define TICKS_PEEK_MAX 16
int ticks_peek(int n, struct tick_entry *out);
/* ticks_peek(1, out): the smallest entry, 1; 0 when there is none */
int ticks_first(struct tick_entry *out);
int ticks_pop(struct tick_entry *out);
int ticks_pop_due(struct tick_entry *out, int64_t latest);
void ticks_walk(void (*cb)(void *ctx, const struct tick_entry *e), void *ctx);
void ticks_load_chunk_entry(int x, int y, int z, int block, int delay, int priority);
int ticks_chunk_updates(int cx, int cz, struct tick_entry *out, int max_out);
/* at least ticks_chunk_updates(cx, cz, NULL, 0), without a walk of the set:
 * the entries of the four chunk columns its box reaches */
int ticks_chunk_bound(int cx, int cz);

/* One world's pending set and the world values func_147454_a reads with it
 * (World.rand, the total time, scheduledUpdatesAreImmediate,
 * BlockFalling.fallInstantly). Each dimension of a replay owns one
 * (serverreplay.h); ticks_use makes it the one every function above works on,
 * NULL the environment's own. Zeroed is empty. */
struct tick_slot;
/* one run of entries already in order: a ring of cap entries, n of them
 * from e[head] on (wrapping), taken from the head */
#define TICKS_RUNS 8
struct ticks_run {
    struct tick_entry *e;
    int head, n, cap;
};
struct ticks_set {
    /* the entries: a min-heap, and beside it runs already in order. An entry
     * joins the run whose last it follows most closely (an empty run when
     * none), the heap when no run takes it: a snapshot's backlog loaded in
     * order (150,000 falling blocks) and the schedules of each delay (now +
     * 0, + 2, + 5 ...: time and entry id both rise) pop in sequential walks
     * instead of a heap descent a pop. Use ticks_set_count, ticks_set_first
     * and ticks_set_part, not nheap. */
    struct tick_entry *heap;
    int nheap, capheap;
    struct ticks_run runs[TICKS_RUNS];
    struct tick_slot *slots;
    size_t nslots, used_slots;
    /* the pending entries per chunk column (x >> 4, z >> 4): keys and counts
     * in open addressing (a key stays at count 0 until the table is rebuilt
     * from the heap), so ticks_chunk_updates skips the heap when none of
     * its columns has one */
    int64_t *ckeys;
    int32_t *ccounts;
    size_t ncol, used_col;
    int immediate, fall_instantly;
    int64_t total_time;
    jrand *world_rand;
    /* WorldServer.pendingTickListEntriesThisTick while tickUpdates runs it:
     * the batch and the index of the entry being ticked (the iterator
     * removed it and every earlier one); NULL outside the batch */
    const struct tick_entry *batch;
    int batch_i, batch_n;
};
void ticks_use(struct ticks_set *set);
/* the set's size; its smallest entry (NULL when empty); its entries as
 * TICKS_PARTS spans in no order (part 0 the heap, then each run's two) */
int ticks_set_count(const struct ticks_set *s);
const struct tick_entry *ticks_set_first(const struct ticks_set *s);
#define TICKS_PARTS (2 * TICKS_RUNS + 1)
const struct tick_entry *ticks_set_part(const struct ticks_set *s, int part, int *n);
void ticks_set_free(struct ticks_set *s);
/* sizeof the membership table's slot (envmem.h) */
size_t ticks_slot_size(void);

/* The environment's part (env.h): its own set, the current one, the
 * nextTickEntryID counter every world shares, and the Random(1) of a probe
 * world that never set World.rand. */
struct ticks_env {
    struct ticks_set own, *cur;
    int64_t next_entry_id;
    jrand own_rand;
    int own_ready;
};

/* The current set moved aside with the counter, and put back. */
struct ticks_backup;
struct ticks_backup *ticks_save(void);
void ticks_restore(struct ticks_backup *b);

#endif
