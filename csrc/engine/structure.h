/* The shared structure layer: bounding boxes, components, structure starts and
 * the candidate walk, ported from net.minecraft.world.gen.structure. A start
 * (which chunk it begins in and its tree of pieces) is a pure function of the
 * world seed and the chunk, so nothing here touches a world; blocks come later,
 * during population, and are not in this lane.
 *
 * Adding a type (the stronghold and temple lanes do this):
 *   1. one file beside mineshaft.c, defining a `const struct structure_type`;
 *   2. one entry in structure_types[] below (its definition is in structure.c);
 *   3. `make -C csrc` and `make -C csrc test`, which runs test_structures on
 *      every directory in out/java/structures.
 * The type's `name` is also the structure start's NBT id, since
 * MapGenStructureIO.func_143033_a returns the MapGenStructure.func_143025_a
 * name ("Mineshaft", "Stronghold", "Temple"). A type that needs state the walk
 * cannot give it (the stronghold's ring is precomputed from the seed and the
 * world's biomes, lazily, on the first spawn test) keeps it in its own file and
 * fills it in `begin` or in `can_spawn`; the walk only hands over the seed and
 * the per-candidate Random. */
#ifndef NETHERITE_STRUCTURE_H
#define NETHERITE_STRUCTURE_H

#include <stdint.h>

#include "det.h"
#include "jrand.h"
#include "nbtjson.h"

/* StructureBoundingBox. */
struct bbox { int minX, minY, minZ, maxX, maxY, maxZ; };

struct bbox bbox_new_empty(void);                              /* getNewBoundingBox */
struct bbox bbox_make(int minX, int minY, int minZ, int maxX, int maxY, int maxZ);
void bbox_expand_to(struct bbox *b, const struct bbox *o);     /* expandTo */
void bbox_offset(struct bbox *b, int dx, int dy, int dz);      /* offset */
int bbox_intersects(const struct bbox *a, const struct bbox *b);
int bbox_xsize(const struct bbox *b);
int bbox_ysize(const struct bbox *b);
int bbox_zsize(const struct bbox *b);
/* getComponentToAddBoundingBox: a box placed relative to an origin, one of the
 * four coordinate base modes. Mineshaft pieces place themselves directly, but
 * the village, stronghold and temple pieces all go through this. */
struct bbox bbox_component_to_add(int x, int y, int z, int offX, int offY, int offZ,
                                  int sizeX, int sizeY, int sizeZ, int coordBaseMode);

/* One StructureComponent. `id` is the MapGenStructureIO name func_143010_b
 * writes ("MSCorridor", "MSCrossing", "MSRoom", "MSStairs", "TeDP", "TeJP",
 * "TeSH", and the stronghold ids in stronghold.c); `coord_base_mode` is "O" and `component_type` is "GD". The union
 * holds what each kind's func_143012_a writes beyond BB, O, GD, id. */
enum temple_kind { TEMPLE_DESERT_PYRAMID, TEMPLE_JUNGLE_PYRAMID, TEMPLE_SWAMP_HUT };
/* StructureVillagePieces' pieces, one kind per class: the well (the start
 * itself, StructureVillagePieces.Start extends Well), the roads, the houses
 * and the fields. */
enum village_kind {
    V_WELL, V_ROAD, V_HOUSE1, V_HOUSE2, V_HOUSE3, V_HOUSE4,
    V_FIELD1, V_FIELD2, V_TORCH, V_WOODHUT, V_CHURCH, V_HALL
};
/* StructureVillagePieces.PieceWeight: one entry of the weighted piece list. */
struct village_weight { int class_kind, weight, spawned, limit; };

/* StructureNetherBridgePieces' classes, one kind for each: the start (a
 * Crossing3 with the weighted lists), the corridors, the crossings, the dead
 * end, the entrance, the room the nether wart grows in, the stairs and the
 * throne room. See fortress.c. */
enum fortress_kind {
    F_START, F_STRAIGHT, F_CORRIDOR, F_CORRIDOR2, F_CORRIDOR3, F_CORRIDOR4,
    F_CORRIDOR5, F_CROSSING, F_CROSSING2, F_CROSSING3, F_END, F_ENTRANCE,
    F_NETHER_STALK, F_STAIRS, F_THRONE
};

/* StructureNetherBridgePieces.PieceWeight, the fortress copy of the village's
 * weighted entry: the class it builds, its weight, how many it has spawned and
 * its limit (0 = no limit), plus whether it may be picked twice in a row
 * (field_78825_e). */
struct fortress_weight { int kind, weight, spawned, limit, repeat; };

enum piece_kind {
    PIECE_CORRIDOR, PIECE_CROSSING, PIECE_ROOM, PIECE_STAIRS,
    PIECE_TEMPLE,   /* each temple component is a single piece, no children */
    PIECE_VILLAGE,  /* well, roads, house, field, torch: see village.c */
    PIECE_STRONGHOLD, /* every stronghold piece; its own state is in `owned` */
    PIECE_FORTRESS  /* the nether bridge pieces: see fortress.c */
};

struct piece {
    const char *id;
    enum piece_kind kind;
    struct bbox bb;
    int coord_base_mode;
    int component_type;
    union {
        struct { int has_rails, has_spiders, spawner_placed, section_count; } corridor;
        struct { int corridor_direction, multiple_floors; } crossing;
        struct { struct bbox *v; int n, cap; } room;   /* roomsLinkedToTheRoom */
        /* ComponentScatteredFeaturePieces.Feature plus the three subclasses:
         * the size triple and the height the piece settled at (HPos, -1 until
         * the population pass places it), then one field group per kind. */
        struct {
            enum temple_kind kind;
            int size_x, size_y, size_z, hpos;
            union {
                struct { int has_placed_chest[4]; } desert;
                struct { int placed_main_chest, placed_hidden_chest, placed_trap1, placed_trap2; } jungle;
                struct { int has_witch; } swamp;
            } v;
        } temple;
        /* StructureVillagePieces.Village plus every subclass's own fields: the
         * base's HPos, VCount and Desert, then one field group per kind. The
         * well subclass Start also holds the weighted piece list and its two
         * pending queues; see village.c. */
        struct {
            enum village_kind kind;
            int hpos, vcount, desert;
            int length;        /* Road.averageGroundLevel */
            int terrace;       /* House4Garden.isRoofAccessible */
            int tall_house, table;             /* WoodHut */
            int chest;                         /* House2.hasMadeChest */
            int ca, cb, cc, cd;                /* Field1 / Field2 crops */
            int terrain_type;  /* Start.terrainType */
            int valid;         /* Start.hasMoreThanTwoComponents, the "Valid" key */
            struct village_weight *weights;
            int nweights;
            struct village_weight *last_weight;   /* structVillagePieceWeight */
            struct village_weight removed_weight; /* the entry last dropped from the list */
            struct piece_list { struct piece **v; int n, cap; } pending_house, pending_road;
        } village;
        /* StructureNetherBridgePieces.Piece plus every subclass's own fields:
         * the chest flag of the two corridors, the dead end's fill seed and
         * the throne's spawner flag, then, for the start only, the two
         * weighted lists (Java's primaryComponents / secondaryComponents
         * copies), the weight used last (theNetherBridgePieceWeight) and the
         * pending queue field_74967_d. The list entries point into `store`. */
        struct {
            enum fortress_kind kind;
            int chest, fill_seed, has_spawner;
            struct fortress_weight store[13];
            struct fortress_weight *primary[6];
            int nprimary;
            struct fortress_weight *secondary[7];
            int nsecondary;
            struct fortress_weight *last;
            struct piece_list pending;
        } fortress;
    } u;
    /* A type whose per-piece state does not fit any listed kind (the stronghold
     * has twelve) owns it here and frees it in start_free. */
    void *owned;
};

/* A StructureStart: the chunk it begins in, its pieces in the order they were
 * built, and the union of their boxes. */
struct start {
    const char *id;                        /* "Mineshaft", "Stronghold", "Temple" */
    int chunk_x, chunk_z;
    struct bbox bb;
    struct piece **pieces;
    int n, cap;
};

void start_init(struct start *s, const char *id, int cx, int cz);
void start_add(struct start *s, struct piece *p);              /* list.add */
void start_update_bb(struct start *s);                         /* updateBoundingBox */
void start_mark_available_height(struct start *s, jrand *rand, int k);
void start_set_random_height(struct start *s, jrand *rand, int lo, int hi);
struct piece *start_find_intersecting(const struct start *s, const struct bbox *b);
nbt *start_nbt(const struct start *s);                         /* func_143021_a */
void start_free(struct start *s);

/* A component's own NBT, func_143010_b. */
nbt *piece_nbt(const struct piece *p);

/* A stronghold piece's own keys (EntryDoor and its kind's field), written from
 * the type's own state (stronghold.c). */
void stronghold_piece_nbt(nbt *c, const struct piece *p);
void stronghold_piece_set_flags(struct piece *p, int chest, int mob);

/* buildComponent; parent is the component the caller passes on, as Java does
 * (the mineshaft room is every piece's parent, since
 * getNextMineShaftComponent forwards its own parent argument). */
void piece_build_component(struct piece *p, struct piece *parent, struct start *s, jrand *rand);

/* ---- the seam: adding a structure type ----
 *
 * A type is one file beside mineshaft.c holding three things: its
 * `can_spawn` test, its `make_start`, and a `struct structure_type` with its
 * name, then one line in structure_types (structure.c); csrc/Makefile picks up
 * the new file by itself. Nothing else changes: the candidate walk, the
 * bounding boxes, the piece and start NBT and the loader are all here.
 *
 * A new kind of piece adds an entry to enum piece_kind, its own fields to the
 * union in struct piece, its id string and its own keys in piece_nbt, and its
 * buildComponent in its own file's dispatch.
 *
 * `begin` is optional and runs once per walk, before any candidate: a type
 * whose RNG stream is not the per-type Random (the stronghold draws its three
 * positions once from the world seed) fills its own state there. */
struct structure_type {
    const char *name;
    /* Optional, once per walk. */
    void (*begin)(int64_t seed);
    int (*can_spawn)(jrand *rand, int cx, int cz);
    void (*make_start)(jrand *rand, int cx, int cz, struct start *s);
};

extern const struct structure_type *const structure_types[];
const struct structure_type *structure_type_by_name(const char *name);

/* World.rand, for the two can_spawn bodies that call World.setRandomSeed
 * (MapGenVillage and MapGenScatteredFeature, see populate.h): the reseed and
 * the two draws after it land in the world's stream when the caller has one,
 * else on a private Random. */

/* The Det streams the pieces' entity constructions draw from; see populate.h. */

/* Called when a piece hands over an entity it constructed (World.
 * spawnEntityInWorld's point in the Java piece), before the step's own list is
 * freed; see populate.h. */
struct sc_ent;

struct start_list { struct start **v; int n, cap; };

/* MapGenBase.func_151539_a over the region widened by 8 (every candidate chunk
 * the game tests for a run that generated exactly x0..x1, z0..z1) plus
 * MapGenStructure.func_151538_a: per candidate, seed the Random from its own
 * coordinates, draw the nextInt() before the spawn test, and build the start.
 * One candidate is tested once, and a start is never rebuilt. out comes back
 * sorted by chunk x then z. */
void structure_walk(const struct structure_type *t, int64_t seed,
                    int x0, int z0, int x1, int z1, struct start_list *out);
void start_list_free(struct start_list *l);

#endif