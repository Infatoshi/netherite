#include "game/village_live.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int kind, variant, facing;
    GmVillageBox box;
} ResidentContext;

static uint16_t resident_get(void *opaque, int x, int y, int z) {
    (void)opaque; (void)x; (void)z;
    return y < 64 ? (uint16_t)(1 << 4) : 0;
}

static void resident_set(
        void *opaque, int x, int y, int z, uint16_t state) {
    (void)opaque; (void)x; (void)y; (void)z; (void)state;
}

static int resident_contains(void *opaque, int x, int y, int z) {
    (void)opaque; (void)x; (void)z;
    return y >= 0 && y < 256;
}

static int resident_top(void *opaque, int x, int z) {
    (void)opaque; (void)x; (void)z;
    return 64;
}

static void resident_emit(void *opaque, int x, int y, int z,
                          int profession, int zombie_infested) {
    ResidentContext *context = (ResidentContext *)opaque;
    if (zombie_infested) return;
    printf("V %d %d %d %d %d %d %d\n",
           context->kind, context->variant, context->facing,
           x - context->box.min_x, y - context->box.min_y,
           z - context->box.min_z, profession);
}

static int dimensions(int kind, int *sx, int *sy, int *sz) {
    switch (kind) {
        case GM_VILLAGE_TORCH: *sx=3; *sy=4; *sz=2; return 1;
        case GM_VILLAGE_HOUSE4_GARDEN: *sx=5; *sy=6; *sz=5; return 1;
        case GM_VILLAGE_CHURCH: *sx=5; *sy=12; *sz=9; return 1;
        case GM_VILLAGE_HOUSE1: *sx=9; *sy=9; *sz=6; return 1;
        case GM_VILLAGE_WOOD_HUT: *sx=4; *sy=6; *sz=5; return 1;
        case GM_VILLAGE_HALL: *sx=9; *sy=7; *sz=11; return 1;
        case GM_VILLAGE_FIELD1: *sx=13; *sy=4; *sz=9; return 1;
        case GM_VILLAGE_FIELD2: *sx=7; *sy=4; *sz=9; return 1;
        case GM_VILLAGE_HOUSE2: *sx=10; *sy=6; *sz=7; return 1;
        case GM_VILLAGE_HOUSE3: *sx=9; *sy=7; *sz=12; return 1;
        default: return 0;
    }
}

int main(void) {
    static const int kinds[] = {
        GM_VILLAGE_TORCH, GM_VILLAGE_HOUSE4_GARDEN,
        GM_VILLAGE_CHURCH, GM_VILLAGE_HOUSE1, GM_VILLAGE_WOOD_HUT,
        GM_VILLAGE_HALL, GM_VILLAGE_FIELD1, GM_VILLAGE_FIELD2,
        GM_VILLAGE_HOUSE2, GM_VILLAGE_HOUSE3
    };
    static const int facings[] = {
        GM_VILLAGE_NORTH, GM_VILLAGE_SOUTH,
        GM_VILLAGE_WEST, GM_VILLAGE_EAST
    };
    for (unsigned ki = 0; ki < sizeof kinds / sizeof kinds[0]; ++ki) {
        int kind = kinds[ki];
        int variants = kind == GM_VILLAGE_HOUSE4_GARDEN ? 2
            : kind == GM_VILLAGE_WOOD_HUT ? 6 : 1;
        for (int variant = 0; variant < variants; ++variant)
            for (unsigned fi = 0; fi < sizeof facings / sizeof facings[0]; ++fi) {
                int sx, sy, sz;
                int facing = facings[fi];
                GmVillagePiece piece;
                ResidentContext context;
                JavaRandom random;
                GmVillageAccess access;
                if (!dimensions(kind, &sx, &sy, &sz)) return 1;
                memset(&piece, 0, sizeof piece);
                piece.kind = kind;
                piece.facing = facing;
                piece.average_ground_lvl = -1;
                piece.extra[0] = kind == GM_VILLAGE_HOUSE4_GARDEN
                    ? variant != 0
                    : kind == GM_VILLAGE_WOOD_HUT ? variant / 3 != 0 : 0;
                piece.extra[1] = kind == GM_VILLAGE_WOOD_HUT
                    ? variant % 3 : 0;
                piece.box.min_x = piece.box.min_z = 0;
                piece.box.min_y = 64;
                piece.box.max_x = (facing == GM_VILLAGE_WEST
                        || facing == GM_VILLAGE_EAST ? sz : sx) - 1;
                piece.box.max_y = 64 + sy - 1;
                piece.box.max_z = (facing == GM_VILLAGE_WEST
                        || facing == GM_VILLAGE_EAST ? sx : sz) - 1;
                context = (ResidentContext){kind, variant, facing, piece.box};
                access = (GmVillageAccess){
                    &context, resident_get, resident_set, resident_contains,
                    resident_top, NULL, resident_emit
                };
                jrand_set(&random, 0x51eedL);
                if (!gm_village_place_piece(
                        &access, &piece, GM_VILLAGE_PLAINS, 0, &random))
                    return 1;
            }
    }
    return 0;
}
