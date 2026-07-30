/* CPU reference driver for player_death. Args: [seed [nticks]] (seed unused; deterministic tape).
 * Runs the death/respawn tape and dumps one line per tick:
 *   foodLevel saturation exhaustion foodTimer health dead deaths death_time
 *   (%d %.6f %.6f %d %.6f %d %d %d). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/player_death.h"

int main(int argc, char **argv) {
    i64 seed   = (argc > 1) ? strtoll(argv[1], 0, 10) : 1LL;
    i32 nticks = (argc > 2) ? (i32)strtol(argv[2], 0, 10) : 700;
    PdState s;
    pd_init(&s);
    for (i32 t = 0; t < nticks; ++t) {
        pd_tape_tick(&s, seed, t);
        printf("%d %.6f %.6f %d %.6f %d %d %d\n",
               s.pv.foodLevel, s.pv.saturation, s.pv.exhaustion, s.pv.foodTimer,
               s.pv.health, s.dead, s.deaths, s.death_time);
    }
    return 0;
}
