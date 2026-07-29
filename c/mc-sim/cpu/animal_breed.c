/* CPU reference driver for animal_breed. Args: [seed [nticks]] (seed unused; deterministic tape).
 * Dumps one line per tick, three animals x (age inLove isChild):
 *   age0 inLove0 isChild0 age1 inLove1 isChild1 age2 inLove2 isChild2
 * Absent slot emits 0 0 0. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/animal_breed.h"

int main(int argc, char **argv) {
    i64 seed   = (argc > 1) ? strtoll(argv[1], 0, 10) : 1LL;
    i32 nticks = (argc > 2) ? (i32)strtol(argv[2], 0, 10) : 200;
    AbState s;
    ab_init(&s);
    for (i32 t = 0; t < nticks; ++t) {
        ab_tape_tick(&s, seed, t);
        for (i32 i = 0; i < AB_SLOTS; ++i) {
            if (i) putchar(' ');
            if (s.a[i].present)
                printf("%d %d %d", s.a[i].growingAge, s.a[i].inLove, ab_is_child(&s.a[i]));
            else
                printf("0 0 0");
        }
        putchar('\n');
    }
    return 0;
}
