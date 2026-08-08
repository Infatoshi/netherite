/* CPU reference driver for player_vitals. Args: [seed [nticks [jump_amp]]].
 * Runs the deterministic exhaustion tape and dumps one line per tick:
 *   foodLevel saturation exhaustion foodTimer health  (%d %.6f %.6f %d %.6f). */
#include <stdio.h>
#include <stdlib.h>
#include "../core/player_vitals.h"

int main(int argc, char **argv) {
    i64 seed   = (argc > 1) ? strtoll(argv[1], 0, 10) : 1LL;
    i32 nticks = (argc > 2) ? (i32)strtol(argv[2], 0, 10) : 400;
    i32 jump_boost_amplifier =
        (argc > 3) ? (i32)strtol(argv[3], 0, 10) : -1;
    PvStats s;
    pv_init(&s);
    for (i32 t = 0; t < nticks; ++t) {
        pv_tape_tick_effect(&s, seed, t, jump_boost_amplifier);
        printf("%d %.6f %.6f %d %.6f\n",
               s.foodLevel, s.saturation, s.exhaustion, s.foodTimer, s.health);
    }
    return 0;
}
