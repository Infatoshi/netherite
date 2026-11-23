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
/* tickUpdates' re-schedule (delay 0) of n entries of a server world whose
 * cells checkChunksExist refuses (no chunk, or y outside 0..255): with the
 * immediate flag down each takes an entry id and is refused again, so this
 * takes the n ids and returns 1; 0 (nothing done) with the flag up */
int ticks_reschedule_absent(int n);

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
 * set orders entries on (time, priority, entry), which is the TreeSet's own
 * total order, and keeps a position+block set for the HashSet dedupe. The
 * block (an id under 4096) and the priority (a diode's -3 to 0) are 16 bits:
 * 32 bytes an entry, not 40 (a start's backlog holds 154k; lane/envhot). */
struct tick_entry { int x, y, z; int16_t block, priority; int64_t time; int64_t entry; };
_Static_assert(sizeof(struct tick_entry) == 32, "a pending tick is 32 bytes");

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
/* ticks_pop_due up to max times into out[0..), the number popped */
int ticks_pop_due_n(struct tick_entry *out, int max, int64_t latest);
/* the same, with bix[i] the base index of out[i] (-1 for a column's entry):
 * the base is ticks_base_current()'s before the call */
int ticks_pop_due_nb(struct tick_entry *out, int32_t *bix, int max, int64_t latest);
/* the same entries, in place when they are one run of the base: the run
 * (*n of them, base index *b0 on) is returned and nothing is copied;
 * otherwise out and bix as ticks_pop_due_nb fills them, *b0 -1. The base
 * is read only, so a batch in place stays as it is while it runs. */
const struct tick_entry *ticks_pop_due_view(struct tick_entry *out, int32_t *bix, int max, int64_t latest, int *n,
                                            int *b0);
const struct ticks_base *ticks_base_current(void);
#ifdef TICKS_VERIFY
/* the verify build's count of a check (ticks.c), for the callers' own */
void ticks_verify(int ok, const char *what);
#endif
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
/* one chunk column's entries (x >> 4, z >> 4), ascending in the set's order,
 * e[head] to e[head + n - 1]; at its node in the set's heap, -1 for none; n
 * -1 marks an empty slot of the table */
struct ticks_col {
    int64_t key;
    struct tick_entry *e;
    int head, n, cap, at;
    /* the column's (x, y, z, block) membership (the HashSet's dedupe), one
     * word a key in open addressing, kcap slots (0 or a power of two) */
    uint32_t *keys;
    int kcap;
};
/* a node of the set's heap: a column's first entry's order and the column */
struct ticks_head {
    int64_t time, entry;
    int32_t priority, col;
};
struct ticks_set {
    /* the entries by chunk column, in open addressing (a key stays at count
     * 0 until the table is rebuilt), each column in the set's order, and a
     * min-heap of the columns by their first: a pop takes the root's first
     * (the set's smallest is its column's first), and ticks_chunk_updates
     * merges the four columns its box reaches instead of walking the set (a
     * snapshot's worldgen backlog is 154,039 falling blocks, and every
     * reset's unloads walked it; lane/envhot). Use ticks_set_count,
     * ticks_set_first and ticks_set_part, not the fields. */
    struct ticks_col *cols;
    size_t ncol, used_col;
    struct ticks_head *order;
    int norder, caporder, count;
    int immediate, fall_instantly;
    int64_t total_time;
    jrand *world_rand;
    /* WorldServer.pendingTickListEntriesThisTick while tickUpdates runs it:
     * the batch and the index of the entry being ticked (the iterator
     * removed it and every earlier one); NULL outside the batch */
    const struct tick_entry *batch;
    int batch_i, batch_n;
    /* a start's entries (ticks_load_all): base->e[bcur..base->n) are the
     * set's beside the columns'; NULL for none */
    const struct ticks_base *base;
    int bcur;
    /* one more at every push and pop and at a load or clear: a set whose
     * count equals the one a start kept holds the start's entries
     * (ticks_set_mods, lane/resetfirst) */
    uint32_t mods;
    /* a schedule's chunk test (sched_body: checkChunksExist over its cell),
     * the last one kept while ce_w's chunk map holds (ce_ver its map_ver + 1;
     * lane/resetfirst) */
    const struct world *ce_w;
    uint32_t ce_ver;
    int ce_cx, ce_cz, ce_ok;
#ifdef TICKS_VERIFY
    /* the verify build: a frozen set keeps the set it was beside it, every
     * operation runs on both and the results are compared */
    struct ticks_set *shadow;
#endif
};

/* A start's pending set frozen (lane/resettick): its entries in the set's
 * order, read only, in process memory outside every env image, shared by the
 * start and every env reset from it. A spawn start holds a 154,039-entry
 * worldgen backlog that the first 154 ticks of every episode drain, 1,000 a
 * tick; from the base a pop is a copy and a cursor's step, where the columns
 * paid a heap sift, a membership delete and its table's shrinks. Nothing
 * but a pop takes an entry out of the set (ticks_chunk_updates copies a
 * box's, as getPendingBlockUpdates(chunk, false) does) and every later
 * schedule joins the columns, so the base's live entries are always the
 * suffix from bcur.
 * mem is the HashSet's dedupe for them: index + 1 by position and block, 0
 * empty, nmem slots; col and ix list each chunk column's indices ascending.
 * refs is the pool's (csrc/runtime/pool.c): the start, and each env and
 * saved start whose image points here, hold one. quiet (servertick.c
 * servertick_base_quiet, NULL for none) is what each entry's tick did on the
 * start's world: TICKS_QUIET_NONE no updateTick (another block there, or
 * air), TICKS_QUIET_STAY a falling block's updateTick that finds it cannot
 * fall, 0 anything else; wseq the world's write sequence then. */
/* a column's indices: all n from ix[at], and those a neighbour's box takes
 * (ticks_chunk_updates' box reaches two blocks into the columns west and
 * north of a chunk): part[0] x & 15 at least 14, part[1] z & 15 at least
 * 14, part[2] both, each {start in six, count} */
struct ticks_bcol { int64_t key; int32_t at, n; int32_t part[3][2]; };
enum { TICKS_QUIET_NONE = 1, TICKS_QUIET_STAY = 2 };
struct ticks_base {
    int refs;
    int n;
    struct tick_entry *e;
    uint32_t *mem;
    uint32_t nmem, ncol;
    struct ticks_bcol *col;
    int32_t *ix, *six;
    uint8_t *quiet;
    uint64_t wseq;
    /* with quiet: qrun[i] the quiet entries from i on in i's chunk, in a row
     * (at most 65535; 0 where quiet[i] is 0), qstay[i] the first of them at
     * or after i that is TICKS_QUIET_STAY, i + qrun[i] for none */
    uint16_t *qrun;
    int32_t *qstay;
};
/* A snapshot's set into the current one (its loader's loop): ticks_load_entry
 * for each, or, under ticks_env's freeze_min, straight into a new base
 * (refs 1, made[]), so the columns never hold the backlog and the start's
 * image never holds their blocks (9 MB of the spawn start's 32 MB, which
 * every reset copied). 0 when a twin's entries are not its adopt's. */
int ticks_load_all(const struct tick_entry *v, int n);
void ticks_base_free(struct ticks_base *b);

void ticks_use(struct ticks_set *set);
/* the set's size; its smallest entry (NULL when empty); its entries as
 * ticks_set_parts(s) spans in no order (a column each, some empty, and the
 * base's live entries last) */
int ticks_set_count(const struct ticks_set *s);
/* the set's pushes, pops, loads and clears so far (struct ticks_set mods) */
uint32_t ticks_set_mods(const struct ticks_set *s);
const struct tick_entry *ticks_set_first(const struct ticks_set *s);
int ticks_set_parts(const struct ticks_set *s);
const struct tick_entry *ticks_set_part(const struct ticks_set *s, int part, int *n);
void ticks_set_free(struct ticks_set *s);
/* the set's slab blocks, as the slab's classes round them (envmem.h) */
size_t ticks_set_bytes(const struct ticks_set *s);

/* The environment's part (env.h): its own set, the current one, the
 * nextTickEntryID counter every world shares, and the Random(1) of a probe
 * world that never set World.rand. */
struct ticks_env {
    struct ticks_set own, *cur;
    int64_t next_entry_id;
    jrand own_rand;
    int own_ready;
    /* a start's load (pool.c, lane/resettick): with freeze_min set, a
     * snapshot's set of at least that many loads straight into a base
     * (ticks_load_all), made[] in load order, or, where adopt[k] is set (a
     * twin's load), onto adopt[k], adopt_bad when its entries differ. Zero
     * outside the load. */
    int freeze_min, nmade, adopt_bad;
    struct ticks_base *made[3];
    const struct ticks_base *adopt[3];
};

/* The current set moved aside with the counter, and put back. */
struct ticks_backup;
struct ticks_backup *ticks_save(void);
void ticks_restore(struct ticks_backup *b);

#endif
