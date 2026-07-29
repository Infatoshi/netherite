/* spawner_activate: the RNG-FREE, oracle-faithful surface of MobSpawnerBaseLogic.updateSpawner.
 *
 * WHY THIS EXISTS (documented divergence): the spawn DRAW sequence in core/tile_entity_spawner.h
 * (spawn positions, per-reset spawnDelay values) uses the sim's hash-based, stateless RNG keyed on
 * (tick,x,y,z,purpose) -- SPEC.md rule 1, a MANDATORY sim-wide invariant that makes CPU==CUDA hold
 * by construction and is thread-schedule-independent. Vanilla instead threads one stateful
 * java.util.Random (world.rand.nextDouble/nextInt) through updateSpawner + resetTimer +
 * WeightedRandom, and SPEC.md explicitly disclaims matching that LCG call order at runtime. So the
 * spawn/reset RNG values are NOT bit-comparable to the oracle -- this is a sanctioned architectural
 * substitution, not a bug, and tile_entity_spawner stays CPU==CUDA-only for that surface.
 *
 * This driver bit-verifies the parts of updateSpawner that carry NO RNG and ARE faithful:
 *   1. isActivated()  == World.isAnyPlayerWithinRangeAt: getDistanceSq(center) < range*range
 *      (net/minecraft/world/World.java + MobSpawnerBaseLogic.java, both VERBATIM). Reuses the real
 *      tes_is_activated from core/tile_entity_spawner.h.
 *   2. The spawnDelay countdown: while activated and spawnDelay>0, updateSpawner does
 *      `--this.spawnDelay; return;` (RNG-free). Exercised via the real tes_update, with scenarios
 *      kept in the delay>0 regime so the RNG spawn loop is never entered.
 * Pure logic => Java == CPU == CUDA. */
#ifndef MC_SPAWNER_ACTIVATE_H
#define MC_SPAWNER_ACTIVATE_H

#include "mc.h"
#include "tile_entity_spawner.h"

#define SA_NPOS 12
#define SA_NCD  5
#define SA_OUT  (SA_NPOS + SA_NCD)

/* Fixed spawner at (8,65,8), activatingRangeFromPlayer = 16. */
MC_HD static inline void sa_run(TeSpawnerScene *s, u64 *out) {
    static const float pos[SA_NPOS][3] = {
        { 8.5f, 65.5f,  8.5f},   /* dist 0            -> activated   */
        {24.5f, 65.5f,  8.5f},   /* dx=16, dist^2=256 == r^2 -> NOT  */
        {23.5f, 65.5f,  8.5f},   /* dx=15, 225 < 256  -> activated   */
        {25.5f, 65.5f,  8.5f},   /* dx=17, 289 > 256  -> NOT         */
        { 8.5f, 65.5f, 24.5f},   /* dz=16 boundary    -> NOT         */
        { 8.5f, 81.5f,  8.5f},   /* dy=16 boundary    -> NOT         */
        {14.5f, 71.5f,  8.5f},   /* 36+36=72          -> activated   */
        {18.5f, 75.5f,  8.5f},   /* 100+100=200       -> activated   */
        {19.5f, 75.5f,  8.5f},   /* 121+100=221       -> activated   */
        {20.5f, 76.5f,  8.5f},   /* 144+121=265 > 256 -> NOT         */
        {1000.5f, 65.5f, 8.5f},  /* far away          -> NOT         */
        {-7.5f, 65.5f,  8.5f},   /* dx=-16 boundary   -> NOT         */
    };
    /* {activated, initial spawnDelay, nticks}; all stay in delay>0 (no RNG spawn loop). */
    static const i32 cd[SA_NCD][3] = {
        {1,  10,  3},   /* -> 7   */
        {1,  10,  9},   /* -> 1   */
        {0,  10,  5},   /* -> 10  (not activated: updateSpawner leaves delay untouched) */
        {1, 200, 50},   /* -> 150 */
        {1,  50, 49},   /* -> 1   */
    };
    int o = 0, i, t;

    for (i = 0; i < SA_NPOS; ++i) {
        s->sx = 8; s->sy = 65; s->sz = 8; s->activate_range = 16;
        s->player_x = pos[i][0]; s->player_y = pos[i][1]; s->player_z = pos[i][2];
        out[o++] = (u64)(u32)(tes_is_activated(s) ? 1 : 0);
    }

    for (i = 0; i < SA_NCD; ++i) {
        s->sx = 8; s->sy = 65; s->sz = 8; s->activate_range = 16;
        if (cd[i][0]) { s->player_x = 8.5f; s->player_y = 65.5f; s->player_z = 8.5f; }
        else          { s->player_x = 1000.0f; s->player_y = 65.5f; s->player_z = 8.5f; }
        s->spawn_delay = cd[i][1];
        s->min_spawn_delay = 200; s->max_spawn_delay = 800;
        s->max_nearby = 6; s->spawn_range = 4; s->nearby_count = 0;
        s->spawn_count = 0;              /* defensive: no spawn even if delay 0 were reached */
        s->world.seed = 0; s->world.tick = 0;
        for (t = 0; t < cd[i][2]; ++t) { s->world.tick = t; tes_update(s); }
        out[o++] = (u64)(u32)s->spawn_delay;
    }
}

#endif /* MC_SPAWNER_ACTIVATE_H */
