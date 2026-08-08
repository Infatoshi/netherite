#include "game/mansion_live.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/mansion_templates.h"

static uint64_t add(uint64_t hash, int value) {
    hash ^= (uint32_t)value;
    return hash * UINT64_C(0x100000001b3);
}

static uint64_t add_string(uint64_t hash, const char *value) {
    while (*value) {
        hash ^= (unsigned char)*value++;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static int one(long long seed, int rotation) {
    GmMansion mansion;
    if (!gm_mansion_build(seed, 104, 71, -200, rotation, &mansion)) return 0;
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    int rooms = 0;
    for (int i = 0; i < mansion.count; ++i) {
        const GmMansionPiece *piece = &mansion.pieces[i];
        const char *name = GM_MANSION_TEMPLATES[piece->template_index].name;
        hash = add_string(hash, name);
        hash = add(hash, piece->x); hash = add(hash, piece->y);
        hash = add(hash, piece->z); hash = add(hash, piece->rotation);
        hash = add(hash, piece->mirror);
        hash = add(hash, piece->min_x); hash = add(hash, piece->min_y);
        hash = add(hash, piece->min_z); hash = add(hash, piece->max_x);
        hash = add(hash, piece->max_y); hash = add(hash, piece->max_z);
        if (!strncmp(name, "1x", 2) || !strncmp(name, "2x", 2)) ++rooms;
        if (getenv("MANSION_VERBOSE"))
            printf("P %s %d %d %d %d %d %d %d %d %d %d %d\n",
                name, piece->x, piece->y, piece->z, piece->rotation,
                piece->mirror, piece->min_x, piece->min_y, piece->min_z,
                piece->max_x, piece->max_y, piece->max_z);
    }
    printf("%lld %d %d %d %016llx\n", seed, rotation, mansion.count,
           rooms, (unsigned long long)hash);
    return 1;
}

int main(void) {
    return one(0, 0) && one(1, 1) && one(123456789, 2)
        && one(-99887766, 3) && one(0x5eed5eed, 3) ? 0 : 1;
}
