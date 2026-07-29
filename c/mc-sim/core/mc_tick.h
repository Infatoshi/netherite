/* mc_tick.h - the tick loop skeleton + the double-buffer rule made concrete. TRUNK schema.
 *
 * THE determinism rule (SPEC rule 3): a tick reads ONLY the 'now' snapshot and writes ONLY 'next';
 * never read your own writes mid-tick. Then swap. This is what lets every env/chunk/cell update in
 * any thread order (CUDA) and still equal the CPU scalar order -> CPU==CUDA holds. Sequential MC
 * systems (fluid, light) become iterate-to-fixpoint cellular automata over this buffer (wave 4).
 *
 * Runtime randomness MUST be the stateless hash RNG keyed on (tick,x,y,z,purpose) (mc_rng.h), never
 * a stateful per-something LCG, or thread order would change results. */
#ifndef MC_TICK_H
#define MC_TICK_H

#include "mc_world.h"
#include "mc_entity.h"

/* One env's mutable runtime state. Two world buffers ping-pong each tick. */
typedef struct {
    World    a, b;        /* double buffer; 'now'/'next' alternate */
    Entities ent;
    int      cur;         /* 0 -> now=a,next=b ; 1 -> swapped */
} Env;

MC_HD static inline World *mc_now(Env *e)  { return e->cur ? &e->b : &e->a; }
MC_HD static inline World *mc_next(Env *e) { return e->cur ? &e->a : &e->b; }
MC_HD static inline void   mc_swap(Env *e) { e->cur ^= 1; }

/* Tick skeleton. Order within a tick mirrors MC's WorldServer.tick coarse phases. Subagents fill
 * each phase (block random ticks, fluid CA, light fixpoint, entity update, spawning) as their
 * oracle lands; each phase reads now, writes next. */
MC_HD static inline void mc_tick_env(Env *e) {
    World *now = mc_now(e), *next = mc_next(e);
    (void)now; (void)next;
    /* 1) copy now->next baseline (or write-through unchanged cells)            [trunk: TODO]
     * 2) scheduled + random block ticks (mc_blocks tickers)                    [oracle 2]
     * 3) fluid flow CA                                                         [oracle 4]
     * 4) light propagation fixpoint                                            [oracle 5]
     * 5) entities: for i in ent: mc_entity_update(&e->ent, i, now, next, tick) [oracle 6/7]
     * 6) mob spawning (hash RNG)                                               [oracle 6]
     */
    e->ent.count = e->ent.count; /* placeholder */
    next->tick = now->tick + 1;
    mc_swap(e);
}

#endif /* MC_TICK_H */
