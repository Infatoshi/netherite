/* MapGenStronghold: the three ring positions and StructureStrongholdPieces, one
 * piece of the shared structure layer (structure.h). Registered in
 * structure_types. */
#ifndef NETHERITE_STRONGHOLD_H
#define NETHERITE_STRONGHOLD_H

#include "structure.h"

extern const struct structure_type structure_stronghold;

/* MapGenStronghold's ring: SH_COUNT chunk positions. */
#define SH_COUNT 3
struct sh_pos { int cx, cz; };
/* StructureStrongholdPieces.pieceWeightArray's length. */
#define SH_WEIGHT_N 11
struct world;

/* MapGenStronghold.getCoordList fallback used by findClosestStructure. */
void stronghold_nearest(int64_t seed, int x, int y, int z, int *tx, int *ty, int *tz);
void stronghold_nearest_loaded(const struct world *w, int x, int y, int z,
                               int *tx, int *ty, int *tz);
void stronghold_note_chunk(struct world *w, int cx, int cz);
/* A start the snapshot's structureMap holds at chunk (cx, cz): it counts as
 * generated; linked 0 (read back from data/Stronghold.dat, its Stairs2 has
 * no portal room) points func_151545_a at the staircase's center. */
void stronghold_note_start(struct world *w, int cx, int cz, int linked);

/* The stronghold pieces, one kind per class in StructureStrongholdPieces.
 * SHS_RIGHTTURN is only the weight's dispatch key and NBT shape: the
 * placement it runs is LeftTurn's, so no piece carries the kind. */
enum sh_kind {
    SHS_STRAIGHT, SHS_PRISON, SHS_LEFTTURN,
    SHS_RIGHTTURN,
    SHS_ROOMCROSSING, SHS_STAIRSSTRAIGHT, SHS_STAIRS, SHS_CROSSING,
    SHS_CHESTCORRIDOR, SHS_LIBRARY, SHS_PORTALROOM, SHS_CORRIDOR
};

enum sh_door { DOOR_OPENING, DOOR_WOOD, DOOR_GRATES, DOOR_IRON };

/* Everything a piece's func_143012_a writes beyond BB, O, GD, id. The block
 * half (stronghold_blocks.c) reads the door and the kind's own fields. */
struct sh_state {
    int kind;
    int door;
    /* Straight */
    int left, right;
    /* Corridor */
    int steps;
    /* Crossing */
    int cross_ll, cross_lh, cross_rl, cross_rh;
    /* ChestCorridor */
    int chest;
    /* RoomCrossing */
    int room_type;
    /* Library */
    int tall;
    /* Stairs */
    int source;
    /* PortalRoom */
    int mob;
};

/* The state a stronghold piece owns (its p->owned, as stronghold.c keeps it). */
const struct sh_state *stronghold_state(const struct piece *p);

/* The pieces' block half (stronghold_blocks.c): the sc_piece_fn dispatch. */
struct sc_ctx;
int stronghold_blocks(struct sc_ctx *c, struct piece *p);

#endif
