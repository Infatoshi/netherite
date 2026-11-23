/* The shared structure block layer: StructureComponent's block helpers and
 * StructureStart.generateStructure, ported from
 * net.minecraft.world.gen.structure. A structure lane builds a start with
 * structure.c, then drives one struct sc_ctx per populated chunk step and
 * calls sc_generate; the pieces' addComponentParts live beside it (see the
 * seam note at the bottom). */
#ifndef NETHERITE_STRUCTURE_BLOCKS_H
#define NETHERITE_STRUCTURE_BLOCKS_H

#include <stddef.h>
#include <stdint.h>

#include "det.h"
#include "jrand.h"
#include "loot.h"
#include "structure.h"
#include "world.h"

/* EntityMinecartChest.getSizeInventory, the slots a corridor's chest cart owns. */
#define CART_SLOTS 27

/* The blocks this layer names, from Blocks. */
enum {
    SCB_AIR = 0, SCB_STONE = 1, SCB_DIRT = 3, SCB_PLANKS = 5, SCB_WEB = 30,
    SCB_TORCH = 50, SCB_OAK_STAIRS = 53, SCB_CHEST = 54, SCB_MOB_SPAWNER = 52,
    SCB_WOODEN_DOOR = 64, SCB_OAK_DOOR = 64, SCB_IRON_DOOR = 71, SCB_RAIL = 66,
    SCB_FENCE = 85, SCB_DISPENSER = 23
};

/* An entity the block generation constructed: a corridor's chest cart (built
 * and filled, never spawned while config.yaml keeps minecarts off), the item
 * drop of a torch (or rail) that lost its support, or a village house's
 * villager (built and spawned), or the witch a swamp hut spawns (EntityWitch,
 * whose constructor's Det draws and position are all a native run reproduces).
 * The numbers match the probe's entity kinds. */
enum { SC_CART = 1, SC_ITEM = 2, SC_VILLAGER = 3, SC_WITCH = 4 };

struct sc_ent {
    int kind, entity_id;
    uint64_t rand_state;       /* the entity's own Random, Det.newRandom's state */
    int64_t uuid_msb, uuid_lsb;
    /* an item drop: what its constructor sets, plus the stack it dropped */
    double x, y, z;
    float yaw, hover;
    double motion_x, motion_z;
    int item, damage, count;
    /* a container's breakBlock spill (a piece's write replacing a chest):
     * its own y motion and stack tag, and pickup delay 0 */
    int spilled, tag;
    double motion_y;
    /* a villager: the profession the constructor was built with (yaw holds
     * its constructor's random head yaw) */
    int profession;
    /* a chest cart: the 27 slots generateChestContents filled */
    struct loot_stack slots[CART_SLOTS];
    int nslots;
    /* a witch: the constructing role's Det streams just before its
     * constructor, from which the replay rebuilds it draw for draw */
    det_rng pre_seeder, pre_math;
    int32_t pre_next_id;
};

/* The environment one generation step runs in: the world, the chunk's 16x16
 * box at +8, the population Random seeded as populate seeds it, Det's OTHER
 * role streams for the entities the pieces construct, and World.rand. All but
 * w and box are caller-owned; ents is owned by the context. The piece_fn is
 * the structure type's own dispatch (one per block half, see the seam note). */
struct sc_ctx {
    struct world *w;
    struct bbox box;          /* the step chunk's 16x16 box at +8 */
    jrand *rand;              /* the population Random */
    det_state *det;           /* role OTHER; NULL and no entity is constructed */
    jrand *world_rand;        /* WorldServer.rand; NULL and no drop is modeled */
    int (*piece_fn)(struct sc_ctx *c, struct piece *p);
    struct sc_ent *ents;      /* the step's entities, in construction order */
    int nents, cap;
};

typedef int (*sc_piece_fn)(struct sc_ctx *c, struct piece *p);

void sc_free_ents(struct sc_ctx *c);
void sc_ent_free(struct sc_ent *e);

/* ---- StructureStart.generateStructure ----
 *
 * The components that meet the step's box, in list order; one whose
 * addComponentParts returns false is dropped for good. Returns how many
 * components ran. */
int sc_generate(struct sc_ctx *c, struct start *s);

/* ---- the StructureComponent helpers ---- */
int sc_x_with_offset(const struct piece *p, int tx, int tz);
int sc_y_with_offset(const struct piece *p, int ty);
int sc_z_with_offset(const struct piece *p, int tx, int tz);
/* func_151555_a: the metadata a block gets under this coord base mode. */
int sc_orient_meta(int id, int meta, int mode);

/* func_151550_a: setBlock(flags 2) inside the step's box, nothing outside. */
void sc_place(struct sc_ctx *c, const struct piece *p, int id, int meta, int tx, int ty, int tz);
/* func_151548_a: the block id, or air outside the step's box. */
int sc_block_at(struct sc_ctx *c, const struct piece *p, int tx, int ty, int tz);

/* fillWithBlocks (func_151549_a) and fillWithMetadataBlocks (func_151556_a):
 * `outer` and `inner` as block ids with their metas. */
void sc_fill(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY, int maxZ,
             int outer_id, int inner_id, int replace);
void sc_fill_meta(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY, int maxZ,
                  int outer_id, int outer_meta, int inner_id, int inner_meta, int replace);
/* fillWithAir. */
void sc_fill_air(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY, int maxZ);
/* randomlyFillWithBlocks (func_151551_a). */
void sc_fill_random(struct sc_ctx *c, const struct piece *p, float chance, int minX, int minY, int minZ,
                    int maxX, int maxY, int maxZ, int outer_id, int inner_id, int replace);
/* fillWithRandomizedBlocks: `select` is the StructureComponent.BlockSelector,
 * called per cell that passes the replace check with the on-face flag the
 * Java call passes, and returning the id and meta to write. */
typedef void (*sc_select_fn)(void *ud, int on_face, int *id, int *meta);
void sc_fill_randomized(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ,
                        int maxX, int maxY, int maxZ, int replace, sc_select_fn select, void *ud);
/* randomlyPlaceBlock (func_151552_a): the draw happens wherever the position
 * lands; the write only inside the step's box. */
void sc_random_place(struct sc_ctx *c, const struct piece *p, float chance, int tx, int ty, int tz, int id, int meta);
/* func_151547_a, the room's dome carve. */
void sc_fill_dome(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY, int maxZ,
                  int id, int replace);
/* func_151554_b: the column fill down to y 2. */
void sc_fill_down(struct sc_ctx *c, const struct piece *p, int id, int meta, int tx, int ty, int tz);
/* clearCurrentPositionBlocksUpwards. */
void sc_clear_upwards(struct sc_ctx *c, const struct piece *p, int tx, int ty, int tz);

/* isLiquidInStructureBoundingBox, over the step chunk box. */
int sc_is_liquid_in(const struct sc_ctx *c, const struct piece *p);

/* generateStructureChestContents: a chest block (flags 2), its tile entity
 * made on demand, and `count` draws from `table` into the 27 slots. */
int sc_chest_contents(struct sc_ctx *c, const struct piece *p, const struct loot_table *table,
                      const struct loot_book *book, int tx, int ty, int tz, int count);

/* generateStructureDispenserContents: a dispenser block (flags 2) facing
 * `dir`'s orientation under the piece's coordBaseMode, its tile entity made
 * on demand (whose constructor spends one Det seeder draw), and `count` draws
 * from `table` into its 9 slots. */
int sc_dispenser_contents(struct sc_ctx *c, const struct piece *p, const struct loot_table *table,
                          const struct loot_book *book, int tx, int ty, int tz, int dir, int count);

/* The witch a swamp hut spawns: EntityWitch(World)'s construction (its Det
 * draws) and the position setLocationAndAngles gives it, x + 0.5, y, z + 0.5. */
int sc_witch_new(struct sc_ctx *c, double x, double y, double z);

/* isVecInside over the step chunk box, the guard every helper carries. */
int sc_in_step_box(const struct sc_ctx *c, int x, int y, int z);

/* A corridor's chest cart: the Det draws of an EntityMinecartChest constructor
 * (entity id, rand, UUID), the position it was built at (the block + 0.5),
 * and the loot slots generateChestContents filled, which move in. */
int sc_cart_new(struct sc_ctx *c, double x, double y, double z, struct loot_stack *slots);

/* A village house's villager: the draws of an EntityVillager constructor, in
 * its order (the Entity id, rand and UUID, then EntityLivingBase's three
 * Det.mathRandom draws), the position setLocationAndAngles set, and the
 * profession it was built with. */
int sc_villager_new(struct sc_ctx *c, double x, double y, double z, int profession);

/* ---- the seam: generating a structure type's blocks ----
 *
 * A block half for a structure type is one file beside mineshaft_blocks.c
 * with a dispatch: `int <type>_blocks(struct sc_ctx *c, struct piece *p)`
 * returning the piece's addComponentParts result (1 placed, 0 the caller
 * drops it). The shared helpers above are all a piece needs. The step loop
 * is: seed the population Random as populate does, then sc_generate. */

#endif