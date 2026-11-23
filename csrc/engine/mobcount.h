#ifndef NETHERITE_MOBCOUNT_H
#define NETHERITE_MOBCOUNT_H

/* A count build (-DMOBAI_COUNT in RL_NCFLAGS, lane/mobai): the mobs' AI and
 * tracking work a tick, counted process-wide with relaxed atomics and printed
 * at exit as MOBCOUNT lines (living.c's destructor). The verify build
 * (-DMOBAI_VERIFY) counts its checks with MCA too. Compiles to nothing
 * otherwise. */
#if defined(MOBAI_COUNT) || defined(MOBAI_VERIFY)
#include <stdatomic.h>
#define MCA(a, n) atomic_fetch_add_explicit(&(a), (unsigned long long)(n), memory_order_relaxed)
#endif

#ifdef MOBAI_COUNT
enum {
    MC_TICK_AN,                         /* livings an_tick_one updates */
    MC_FINISH, MC_FINISH_ENTRIES, MC_FINISH_LIVE, MC_FINISH_HAS, /* combat_server_finish: calls, entries, live ids, has_int */
    MC_TRACK_ONE, MC_ENTRY_TICK, MC_ENTRY_PKTS, MC_TRY_WATCH,
    MC_SPTRACK, MC_SPTRACK_HIT,
    MC_COLLECT, MC_COLLECT_VISIT, MC_COLLECT_HIT, MC_COLLECT_CHUNKS, MC_COLLECT_VITEM,
    MC_TASKS, MC_SHOULD, MC_CONTINUE, MC_START, MC_UPDATE,
    MC_N
};
#define MC_KINDS 128
#define MC_TASKCLS 64
#define MC_CALLERS 8
extern _Atomic unsigned long long mc_count[MC_N];
extern _Atomic unsigned long long mc_kind[MC_KINDS];          /* livings ticked, by kind */
extern _Atomic unsigned long long mc_should[MC_TASKCLS];      /* shouldExecute calls, by task class */
extern _Atomic unsigned long long mc_collect[MC_CALLERS][3];  /* an_collect by kind arg + 2: calls, visited, hits */
#define MC(i) atomic_fetch_add_explicit(&mc_count[i], 1, memory_order_relaxed)
#define MCN(i, n) atomic_fetch_add_explicit(&mc_count[i], (unsigned long long)(n), memory_order_relaxed)
#else
#define MC(i) ((void)0)
#define MCN(i, n) ((void)0)
#endif

#endif
