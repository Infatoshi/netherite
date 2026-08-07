#include "stronghold_loot.h"

#include <stdio.h>

static const i64 seeds[] = {
    0, 42, 12345, -6024556974586992056LL
};

static void emit_slots(const TecStack *slots, int count) {
    int nonempty = 0;
    for (int slot = 0; slot < count; ++slot) {
        const TecStack *s = &slots[slot];
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

int main(void) {
    for (int si = 0; si < (int)(sizeof seeds / sizeof seeds[0]); ++si) {
        TeChest chest;
        TecStack dispenser[9];
        tec_init(&chest);
        for (int i = 0; i < 9; ++i) dispenser[i] = tec_empty();
        shl_fill_chest(&chest, SHL_JUNGLE_TEMPLE, seeds[si]);
        shl_fill_inventory(dispenser, 9,
            SHL_JUNGLE_TEMPLE_DISPENSER, seeds[si]);
        emit_slots(chest.slots, TEC_SLOTS);
        emit_slots(dispenser, 9);
    }
    return 0;
}
