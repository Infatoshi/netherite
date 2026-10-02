/* WorldClient.rand from the recording. A row's d.cw is the client world
 * Random's 48-bit seed times 31 plus World.updateLCG (Rows.java), and the
 * client never steps updateLCG (World.func_147467_a's mood sound is
 * server-only), so two rows of one client world differ by 31 times the
 * seed's advance. The snapshot does not carry the client world's Random;
 * this recovers it from two consecutive rows: the one seed s0 and step
 * count K (1..max_steps LCG steps) with step^K(s0) - s0 = (cw1 - cw0) / 31
 * whose LCG value cw0 - 31 s0 is an int, confirmed by a third row cw2
 * (some K2 steps past s1 lands on cw2). */
#ifndef NETHERITE_CWRAND_H
#define NETHERITE_CWRAND_H

#include <stdint.h>

/* 1 with *seed (the Random's 48-bit state after the cw0 row), *lcg and *steps
 * set when exactly one candidate holds; 0 when none or several do, or the
 * rows do not move (a paused tick). */
int cwrand_recover(int64_t cw0, int64_t cw1, int64_t cw2, int max_steps, uint64_t *seed, int32_t *lcg,
                   int *steps);

/* The client world's Random at a snapshot's bind from its tape's rows: from
 * rows t0 - 1, t0 and t0 + 1 (the snapshot's own state) when row t0 - 1 has
 * a client world, else from rows t0 .. t0 + 2 (the state after row t0's
 * tick). *from is t0 or t0 + 1, the tick after which the Random is the
 * recovered one, or -1 when neither recovers. */
void cwrand_from_tape(const char *path, int64_t t0, uint64_t *seed, int32_t *lcg, int64_t *from);

/* The row's d.cw of a seed and LCG. */
static inline int64_t cwrand_digest(uint64_t seed, int32_t lcg)
{
    return (int64_t)seed * 31 + (int64_t)lcg;
}

#endif
