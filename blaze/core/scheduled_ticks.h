/* scheduled_ticks.h - vanilla scheduled-tick queue (P0 item 1, PORT_MATRIX C/P0).
 * PORT TARGET: world/NextTickListEntry.java + WorldServer.java pendingTickListEntries*
 * (scheduleUpdate/updateBlockTick insert path + the tickUpdates(false) drain).
 *
 * Vanilla keeps THREE containers: a TreeSet ordered by NextTickListEntry.compareTo
 * (scheduledTime, then priority, then insertion id), a HashSet for the (pos, block)
 * dedup membership check, and a per-tick List `pendingTickListEntriesThisTick` that
 * holds the batch being executed (so a block re-scheduling ITSELF during its own
 * updateTick passes the dedup check - the entry left the HashSet when it was drained).
 * Here: a binary min-heap over a fixed pool (compareTo is a total order via the id
 * tiebreak, so heap pop order == TreeSet first()+remove order), an open-addressing
 * hash table for the dedup set, and a fixed this_tick array for the executing batch.
 *
 * ALLOCATE-ONCE CONTRACT: the struct is plain bytes sized by STQ_CAP; the caller
 * allocates it once at init (malloc/cudaMalloc) and ticking only mutates bytes. No
 * malloc anywhere in here. Pool overflow asserts loudly on host and bumps a sticky
 * `overflow` counter on both host and device.
 *
 * The scheduledUpdatesAreImmediate populate-time branch of updateBlockTick is ALREADY
 * ported in populate.h (immediateBlockTick) and is intentionally NOT duplicated here.
 *
 * CAPACITY: STQ_CAP default 131072 = 2x the vanilla 65536 per-tick drain cap. Vanilla's
 * TreeSet is unbounded, but tickUpdates never executes more than 65536 entries per tick;
 * a backlog past 2x that cap means the sim already left the vanilla behavior envelope
 * (and our fixed regions cannot legitimately produce it). Batched-RL builds can lower
 * STQ_CAP per TU before including this header.
 *
 * CPU==CUDA: everything below is MC_HD and branch-deterministic (no floats, no RNG);
 * the queue is per-world sequential state, so device use is one thread per world. */
#ifndef MC_SCHEDULED_TICKS_H
#define MC_SCHEDULED_TICKS_H

#include "mc.h"

#ifndef __CUDA_ARCH__
#include <assert.h>
#include <stdio.h>
#define STQ_FAIL(msg) do { fprintf(stderr, "scheduled_ticks: %s\n", msg); assert(0 && msg); } while (0)
#else
#define STQ_FAIL(msg) do { } while (0)
#endif

#ifndef STQ_CAP
#define STQ_CAP 131072                    /* pending pool: 2x the per-tick drain cap */
#endif
#define STQ_HASH_CAP (STQ_CAP * 2)        /* open addressing, max load 0.5 */
#define STQ_MAX_PER_TICK 65536            /* WorldServer.tickUpdates: `if (i > 65536) i = 65536` */

#define STQ_SLOT_EMPTY (-1)
#define STQ_SLOT_TOMB  (-2)

/* NextTickListEntry: position, block, scheduledTime, priority, tickEntryID. */
typedef struct {
    i32 x, y, z;
    i32 block;          /* vanilla stores the Block instance; legacy id here */
    i64 time;           /* scheduledTime */
    i32 priority;
    i64 id;             /* tickEntryID (global insertion counter) */
} StqEntry;

typedef struct McScheduledTicks {
    StqEntry pool[STQ_CAP];
    i32      heap[STQ_CAP];               /* min-heap of pool indices */
    i32      heap_n;
    i32      free_list[STQ_CAP];          /* stack of free pool slots */
    i32      free_n;
    i32      hash[STQ_HASH_CAP];          /* pool index / EMPTY / TOMB */
    StqEntry this_tick[STQ_MAX_PER_TICK]; /* pendingTickListEntriesThisTick */
    i32      this_n;
    i64      next_entry_id;               /* NextTickListEntry.nextTickEntryID */
    i32      overflow;                    /* sticky: schedules dropped on pool overflow */
} McScheduledTicks;

/* NextTickListEntry.compareTo: scheduledTime, then priority, then tickEntryID.
 * Total order (ids are unique), so heap order == vanilla TreeSet iteration order. */
MC_HD static inline int stq_cmp(const StqEntry *a, const StqEntry *b) {
    if (a->time != b->time) return a->time < b->time ? -1 : 1;
    if (a->priority != b->priority) return a->priority < b->priority ? -1 : 1;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    return 0;
}

/* Dedup-set key: vanilla equals() is position AND block (hashCode is position only;
 * we mix the block in too - bucket choice only, semantics unchanged). */
MC_HD static inline u32 stq_key_hash(i32 x, i32 y, i32 z, i32 block) {
    u64 h = (u64)(u32)x * 0x9E3779B97F4A7C15ULL;
    h = mc_hash64(h ^ (u64)(u32)y * 0xC2B2AE3D27D4EB4FULL);
    h = mc_hash64(h ^ (u64)(u32)z * 0x165667B19E3779F9ULL);
    h = mc_hash64(h ^ (u64)(u32)block);
    return (u32)(h >> 32);
}

MC_HD static inline void stq_init(McScheduledTicks *q) {
    i32 i;
    q->heap_n = 0;
    q->this_n = 0;
    q->next_entry_id = 0;
    q->overflow = 0;
    for (i = 0; i < STQ_CAP; ++i) q->free_list[i] = STQ_CAP - 1 - i;   /* pop 0 first */
    q->free_n = STQ_CAP;
    for (i = 0; i < STQ_HASH_CAP; ++i) q->hash[i] = STQ_SLOT_EMPTY;
}

/* ---- hash-set membership (pendingTickListEntriesHashSet) ---- */

/* Returns the hash-table slot holding (pos, block), or -1. */
MC_HD static inline i32 stq_hash_find(const McScheduledTicks *q, i32 x, i32 y, i32 z, i32 block) {
    u32 slot = stq_key_hash(x, y, z, block) & (u32)(STQ_HASH_CAP - 1);
    i32 probes;
    for (probes = 0; probes < STQ_HASH_CAP; ++probes) {
        i32 pi = q->hash[slot];
        if (pi == STQ_SLOT_EMPTY) return -1;
        if (pi >= 0) {
            const StqEntry *e = &q->pool[pi];
            if (e->x == x && e->y == y && e->z == z && e->block == block) return (i32)slot;
        }
        slot = (slot + 1) & (u32)(STQ_HASH_CAP - 1);
    }
    return -1;
}

MC_HD static inline void stq_hash_insert(McScheduledTicks *q, i32 pool_idx) {
    const StqEntry *e = &q->pool[pool_idx];
    u32 slot = stq_key_hash(e->x, e->y, e->z, e->block) & (u32)(STQ_HASH_CAP - 1);
    while (q->hash[slot] >= 0) slot = (slot + 1) & (u32)(STQ_HASH_CAP - 1);
    q->hash[slot] = pool_idx;               /* fills EMPTY or reuses TOMB */
}

/* ---- min-heap (pendingTickListEntriesTreeSet) ---- */

MC_HD static inline void stq_heap_push(McScheduledTicks *q, i32 pool_idx) {
    i32 i = q->heap_n++;
    q->heap[i] = pool_idx;
    while (i > 0) {
        i32 p = (i - 1) / 2;
        if (stq_cmp(&q->pool[q->heap[i]], &q->pool[q->heap[p]]) >= 0) break;
        { i32 t = q->heap[i]; q->heap[i] = q->heap[p]; q->heap[p] = t; }
        i = p;
    }
}

MC_HD static inline i32 stq_heap_pop(McScheduledTicks *q) {
    i32 top = q->heap[0];
    i32 i = 0;
    q->heap[0] = q->heap[--q->heap_n];
    for (;;) {
        i32 l = 2 * i + 1, r = l + 1, m = i;
        if (l < q->heap_n && stq_cmp(&q->pool[q->heap[l]], &q->pool[q->heap[m]]) < 0) m = l;
        if (r < q->heap_n && stq_cmp(&q->pool[q->heap[r]], &q->pool[q->heap[m]]) < 0) m = r;
        if (m == i) break;
        { i32 t = q->heap[i]; q->heap[i] = q->heap[m]; q->heap[m] = t; }
        i = m;
    }
    return top;
}

/* ---- vanilla API ---- */

/* WorldServer.isUpdateScheduled: HashSet membership of (pos, block). The probe
 * NextTickListEntry construction consumes a tickEntryID in vanilla; mirrored so id
 * VALUES stay trace-comparable (relative order of inserted entries is unaffected). */
MC_HD static inline int stq_is_update_scheduled(McScheduledTicks *q, i32 x, i32 y, i32 z, i32 block) {
    q->next_entry_id++;
    return stq_hash_find(q, x, y, z, block) >= 0;
}

/* WorldServer.isBlockTickPending: membership in pendingTickListEntriesThisTick. */
MC_HD static inline int stq_is_block_tick_pending(McScheduledTicks *q, i32 x, i32 y, i32 z, i32 block) {
    i32 i;
    q->next_entry_id++;                     /* probe entry construction, as above */
    for (i = 0; i < q->this_n; ++i) {
        const StqEntry *e = &q->this_tick[i];
        if (e->x == x && e->y == y && e->z == z && e->block == block) return 1;
    }
    return 0;
}

/* WorldServer.updateBlockTick runtime path (the scheduledUpdatesAreImmediate populate
 * branch lives in populate.h). Vanilla:
 *   - constructs the entry FIRST (consumes an id even when deduped or unloaded),
 *   - gates on isBlockLoaded(pos)          -> caller passes pos_loaded,
 *   - sets time/priority only when the block material != AIR (else both stay 0)
 *                                          -> caller passes block_is_air,
 *   - inserts only if the (pos, block) key is not already in the HashSet.
 * scheduleUpdate(pos, block, delay) == stq_update_block_tick(..., delay, 0, ...). */
MC_HD static inline void stq_update_block_tick(McScheduledTicks *q, i32 x, i32 y, i32 z, i32 block,
                                               i32 delay, i32 priority, i64 total_world_time,
                                               int block_is_air, int pos_loaded) {
    i64 id = q->next_entry_id++;            /* NextTickListEntry ctor */
    if (!pos_loaded) return;
    if (stq_hash_find(q, x, y, z, block) >= 0) return;   /* dedup */
    if (q->free_n <= 0) {
        q->overflow++;
        STQ_FAIL("pending scheduled-tick pool overflow (raise STQ_CAP)");
        return;
    }
    {
        i32 pi = q->free_list[--q->free_n];
        StqEntry *e = &q->pool[pi];
        e->x = x; e->y = y; e->z = z; e->block = block;
        e->time = block_is_air ? 0 : total_world_time + (i64)delay;
        e->priority = block_is_air ? 0 : priority;
        e->id = id;
        stq_hash_insert(q, pi);
        stq_heap_push(q, pi);
    }
}

/* tickUpdates "cleaning" phase: pop at most 65536 entries whose scheduledTime <=
 * totalWorldTime (all of them when tick_all, vanilla p_72955_1_==true) out of the
 * TreeSet+HashSet into pendingTickListEntriesThisTick. Returns the batch size; the
 * caller then walks stq_this(q)[0..n) doing the per-entry area-loaded / block-match
 * check and updateTick dispatch (during which stq_update_block_tick may be called -
 * the drained entries are out of the hash set, so self-reschedule passes dedup,
 * exactly as in vanilla), then calls stq_end_tick. */
MC_HD static inline i32 stq_begin_tick(McScheduledTicks *q, i64 total_world_time, int tick_all) {
    i32 budget = q->heap_n;
    i32 j;
    if (budget > STQ_MAX_PER_TICK) budget = STQ_MAX_PER_TICK;
    q->this_n = 0;
    for (j = 0; j < budget; ++j) {
        const StqEntry *top = &q->pool[q->heap[0]];
        i32 pi, hslot;
        if (!tick_all && top->time > total_world_time) break;
        pi = stq_heap_pop(q);
        hslot = stq_hash_find(q, q->pool[pi].x, q->pool[pi].y, q->pool[pi].z, q->pool[pi].block);
        if (hslot >= 0) q->hash[hslot] = STQ_SLOT_TOMB;
        q->this_tick[q->this_n++] = q->pool[pi];
        q->free_list[q->free_n++] = pi;
    }
    return q->this_n;
}

MC_HD static inline const StqEntry *stq_this(const McScheduledTicks *q) { return q->this_tick; }

/* tickUpdates tail: pendingTickListEntriesThisTick.clear(). */
MC_HD static inline void stq_end_tick(McScheduledTicks *q) { q->this_n = 0; }

/* tickUpdates return value: `!this.pendingTickListEntriesTreeSet.isEmpty()`. */
MC_HD static inline int stq_has_pending(const McScheduledTicks *q) { return q->heap_n > 0; }

#endif /* MC_SCHEDULED_TICKS_H */
