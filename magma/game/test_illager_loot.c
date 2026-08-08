#include "game/mob_live.h"

#include <inttypes.h>
#include <stdio.h>

static const uint64_t seeds[] = {
    0, 2, 95, 402, (UINT64_C(1) << 48) - 1
};

int main(void) {
    const int types[] = {EW_TYPE_VINDICATOR, EW_TYPE_EVOKER};
    const char *names[] = {"vindicator", "evoker"};
    for (int type = 0; type < 2; ++type) {
        for (int si = 0; si < (int)(sizeof seeds / sizeof seeds[0]); ++si) {
            for (int fixture = 0; fixture < 3; ++fixture) {
                int looting = fixture == 1 ? 3 : 0;
                int killed = fixture != 2;
                uint64_t cursor = seeds[si];
                GmHostileLootOutcome loot;
                if (!gm_mobs_generate_hostile_loot(
                        types[type], 1, &cursor, looting, killed, &loot))
                    return 1;
                printf("%s %" PRIu64 " %d %d %d", names[type],
                       seeds[si], looting, killed, loot.count);
                for (int i = 0; i < loot.count; ++i)
                    printf(" %d:%d:%d", loot.item[i],
                           loot.quantity[i], loot.meta[i]);
                printf(" %" PRIu64 "\n", cursor);
            }
        }
    }
    return 0;
}
