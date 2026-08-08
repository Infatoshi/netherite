#include "stronghold_loot.h"

#include <stdio.h>

static const i64 seeds[] = {
    0, 42, 12345, -6024556974586992056LL
};

int main(void) {
    for (int si = 0; si < (int)(sizeof seeds / sizeof seeds[0]); ++si) {
        TeChest chest;
        int nonempty = 0;
        tec_init(&chest);
        shl_fill_chest(&chest, SHL_END_CITY_TREASURE, seeds[si]);
        for (int slot = 0; slot < TEC_SLOTS; ++slot) {
            const TecStack *s = &chest.slots[slot];
            printf("%08x\n", (unsigned)s->item);
            printf("%08x\n", (unsigned)s->count);
            printf("%08x\n", (unsigned)s->meta);
            printf("%08x\n", (unsigned)s->n_enchants);
            printf("%08x\n", (unsigned)(s->n_enchants > 0 ? s->enchants[0].id : 0));
            printf("%08x\n", (unsigned)(s->n_enchants > 0 ? s->enchants[0].level : 0));
            printf("%08x\n", (unsigned)(s->n_enchants > 1 ? s->enchants[1].id : 0));
            printf("%08x\n", (unsigned)(s->n_enchants > 1 ? s->enchants[1].level : 0));
            if (!tec_is_empty(s)) ++nonempty;
        }
        printf("%08x\n", (unsigned)nonempty);
    }
    return 0;
}
