/* batchtick.h: the batched fast-mode tick (batchtick.c), the model behind
 * `engine.tick_scope: batched` (prototyped 2026-10-24, DEVLOG). One thread steps many
 * worlds in flat arrays; its covered features and gaps are in batchtick.c's header. */
#ifndef NETHERITE_RUNTIME_BATCHTICK_H
#define NETHERITE_RUNTIME_BATCHTICK_H

/* The gate: 0 when every physics value (walk, sprint, jump, post, drop) matches vanilla's
 * arithmetic to 1e-12 relative, the dig rate is vanilla's, and the whole per-world state
 * hashes the same on 1 and 32 threads after worlds_init. */
int nwbt_selftest(void);

/* The recorded comparison: the model and the engine from one start state on
 * stored recordings under snaps (out/java/snapshots), the engine's own gate
 * binary engine_bin (out/native/test_snapshots) proving the per-tick rows,
 * rows ticks of each. Prints the per-tick divergence; 0 when every replay ran
 * and the engine's gate passed (batchtick.c's replay section names exactly
 * what is compared). */
int nwbt_compare(const char *snaps, const char *engine_bin, int rows);

/* The bench kept from the prototype: --selftest, --n N, --mode 0|1|2, --target s (a table
 * of worlds x ticks/s, its counters a world-tick and the determinism hashes). */
int nwbt_main(int argc, char **argv);

#endif
