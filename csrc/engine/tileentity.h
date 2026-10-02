/* The tile entity store: Chunk's chunkTileEntityMap (a per-chunk array), the
 * world's field_147482_g list (tileticks.c walks it), and the canonical NBT
 * text each kind's writeToNBT renders. The tick classes carry their fields as
 * the Java classes do; the two population features (the chest and the spawner)
 * keep their old shape.
 *
* Every kind the store holds lives here: the five the population features
 * create (the chest, the mob spawner, the furnace, the dispenser and the
 * flower pot, the blocks Block.ITileEntityProvider marks in blocks.h) and the
 * tick classes the tile tick probe walks. A chest carries the 27 slots
 * generatorChestContents fills, a dispenser the 9 its func_150706_a fills, a
 * spawner MobSpawnerBaseLogic's mobID, which WorldGenDungeons sets through
 * setMobID, and a flower pot the Item and Data BlockFlowerPot
 * .createNewTileEntity picked from the block's metadata.
 *
 * A tile entity is one malloc'd struct (the world list holds pointers to it),
 * keyed by position in its chunk's store. The kind comes from the block id on
 * demand creation (Chunk.func_150806_e) or from the NBT's "id" on load.
 */
#ifndef NETHERITE_TILEENTITY_H
#define NETHERITE_TILEENTITY_H

#include "itemtag.h"
#include <stdint.h>

#include "loot.h"
#include "nbtjson.h"

/* TileEntity registry names: the five the population features create first
 * (numbers as master had them), then the tick classes the probe walks. */
enum {
    TE_CHEST = 1, TE_MOB_SPAWNER = 2, TE_FURNACE = 3, TE_DISPENSER = 4, TE_FLOWER_POT = 5,
    TE_ENDER_CHEST = 6, TE_NOTE = 7, TE_DAYLIGHT_DETECTOR = 8, TE_HOPPER = 9, TE_BEACON = 10,
    TE_BREWING_STAND = 11, TE_ENCHANT_TABLE = 12, TE_PISTON = 13,
    /* the kinds the placement probe stores: the block ids te_kind_of_block
     * maps onto these */
    TE_SIGN = 14, TE_SKULL = 15, TE_COMPARATOR = 16, TE_COMMAND_BLOCK = 17,
    TE_JUKEBOX = 18, TE_END_PORTAL = 19,
};

/* The blocks whose block_def.tile_entity flag is set (blocks.h): the five the
 * population features create, then the tick classes' blocks. */
enum {
    BLK_CHEST = 54, BLK_MOB_SPAWNER = 52, BLK_FURNACE = 61, BLK_DISPENSER = 23, BLK_FLOWER_POT = 140,
    BLK_LIT_FURNACE = 62, BLK_ENDER_CHEST = 130, BLK_NOTE = 25, BLK_DAYLIGHT_DETECTOR = 151,
    BLK_HOPPER = 154, BLK_BEACON = 138, BLK_BREWING_STAND = 117, BLK_ENCHANT_TABLE = 116,
    BLK_PISTON_MOVING = 36, BLK_TRAPPED_CHEST = 146, BLK_JUKEBOX = 84,
    BLK_UNPOWERED_COMPARATOR = 149, BLK_POWERED_COMPARATOR = 150, BLK_DROPPER = 158,
};

/* TileEntityChest.getSizeInventory: the slots generateChestContents draws land
 * in (the array it holds is 36 long, but nothing beyond 26 is ever set). */
#define CHEST_SLOTS 27

/* TileEntityDispenser.getSizeInventory: the slots func_150706_a draws land in. */
#define DISPENSER_SLOTS 9

/* The flower pot's TileEntityFlowerPot fields: the Item and Data
 * BlockFlowerPot.createNewTileEntity picked from the block's metadata. */
struct te_pot {
    int item, data;
};

/* The dungeon probe's record kinds (FeatureProbeDungeons): 1 chest, 2 spawner,
 * unrelated to the TE kinds above. The test maps one to the other. */
enum { FE_REC_CHEST = 1, FE_REC_SPAWNER = 2 };

/* One inventory stack: an ItemStack the fields carry, and its tag in the item
 * tag store (itemtag.h, 0 for none). */
struct te_stack {
    int item;      /* -1 for Java's null */
    int damage;
    int count;
    int tag;       /* the field order matches struct loot_stack so the dungeon
                    * feature can hand its chest slots straight to
                    * loot_generate_contents */
};

/* TileEntityFurnace: the three slots (0 input, 1 fuel, 2 output), the burn
 * and cook times and the fuel total the ignition set. */
struct te_furnace {
    struct te_stack slots[3];
    int burn_time;         /* field_145956_a */
    int fuel_total;        /* field_145963_i */
    int cook_time;         /* field_145961_j */
};

/* TileEntityChest and TileEntityEnderChest bookkeeping. The chest's 27 slots
 * the loot features fill; the ender chest has none. The lid fields are not
 * NBT but the walk's behavior reads them. */
struct te_chest {
    struct te_stack slots[27];
    int players_using;     /* field_145987_o / field_145973_j */
    int tick_counter;      /* field_145983_q / field_145974_k */
    float lid, prev_lid;   /* field_145989_m/field_145986_n, field_145972_a/field_145975_i */
    /* func_145979_i's cache: field_145984_a (the neighbour scan ran), then
     * whether a chest of this chest's kind sat at z-1, z+1, x+1, x-1
     * (field_145992_i, field_145988_l, field_145990_j, field_145991_k) */
    int adjacent_checked;
    int adj[4];
};

/* TileEntityHopper: five slots and the transfer cooldown, the only fields
 * redstone-off ticking touches. */
struct te_hopper {
    struct te_stack slots[5];
    int cooldown;          /* field_145901_j, -1 until the NBT says otherwise */
};

/* MobSpawnerBaseLogic: the fields writeToNBT stores. */
struct te_spawner {
    char *mob_id;          /* owned, "Pig" until set */
    int spawn_delay;
    int desc_delay;        /* spawn_delay before the latest update: the Delay a
                            * description packet (S35) built this tick carries,
                            * as the chunk flush and the chunk sends run before
                            * the tile entities (not saved) */
    int min_spawn_delay, max_spawn_delay, spawn_count, max_nearby_entities;
    int required_player_range, spawn_range;
};

/* TileEntityBeacon. */
struct te_beacon {
    int primary, secondary, levels;   /* NBT: Primary, Secondary, Levels */
};

/* TileEntityBrewingStand: four slots (0..2 bottles, 3 ingredient) and the
 * brew time. With config.yaml brewing off the potion paths stay dead. */
struct te_brewing {
    struct te_stack slots[4];
    int brew_time;         /* field_145946_k, NBT BrewTime */
    int filled_slots;      /* field_145943_l: the last bottle mask written as
                            * metadata; 0 on construction, not in the NBT */
};

/* TileEntityNote: one note byte, NBT "note". */
struct te_note {
    int note;
};

/* TileEntityEnchantmentTable: floats only, no NBT beyond the base. */
struct te_enchant {
    float book_spread, book_rotation, prev_book_rotation;
    float book_rotation_2, tRot, flip, oFlip, flipT, oFlipT, otilde, ot;
};

/* TileEntityDispenser: the 9 slots func_150706_a fills (the same head of the
 * chest's array in master's flat layout; the pointer model gives it its own). */
struct te_dispenser {
    struct te_stack slots[DISPENSER_SLOTS];
};

/* TileEntityPiston: the pushed block, its facing and progress. */
struct te_piston {
    int block_id, block_meta, facing;
    int extending;         /* field_145875_k */
    float progress, last_progress;   /* field_145873_m, field_145870_n */
};

/* BlockJukebox.TileEntityJukebox: the one ItemStack, item -1 for Java's null
 * (an empty jukebox, its block metadata 0). */
struct te_jukebox {
    int item, damage, count;
};

/* TileEntityComparator: the OutputSignal writeToNBT stores. */
struct te_comparator {
    int out_signal;
};

struct tile_entity {
    int x, y, z;
    int kind;
    int block;             /* the block id the entity sits at: a dropper's
                            * dispenser entity writes "Dropper", not "Trap" */
    char *raw;             /* replayed entries: the oracle's canonical NBT
                            * verbatim, in place of generated state */
    uint8_t invalid;       /* TileEntity.tileEntityInvalid */
    int block_metadata;    /* blockMetadata, -1 until read */
    int skull_type, skull_rot;  /* skull: func_152107_a / func_145903_a */
    char sign_text[4][48];      /* sign: field_145915_a, at most 15 chars a
                                 * line (UTF-8); blank when placed, since
                                 * sign_editing is off */
    union {
        struct te_furnace furnace;
        struct te_chest chest;
        struct te_hopper hopper;
        struct te_spawner spawner;
        struct te_beacon beacon;
        struct te_brewing brewing;
        struct te_note note;
        struct te_enchant enchant;
        struct te_piston piston;
        struct te_dispenser dispenser;
        struct te_pot pot;
        struct te_jukebox jukebox;
        struct te_comparator comparator;
    } u;
};

/* Chunk.chunkTileEntityMap flattened: pointers, in creation order. */
struct te_store {
    struct tile_entity **v;
    int n, cap;
};

void te_store_free(struct te_store *s);

/* Chunk.func_150806_e / addTileEntity / removeTileEntity. te_find is the map
 * lookup; te_put stores `kind` fresh under the position, with whatever sat
 * there handed back to the caller (not freed); te_remove hands the entity
 * back too. Ownership: every entity the world ever made is freed once, from
 * the world's registry (world.c). */
struct tile_entity *te_find(struct te_store *s, int x, int y, int z);
struct tile_entity *te_put(struct te_store *s, int x, int y, int z, int kind);
struct tile_entity *te_remove(struct te_store *s, int x, int y, int z);

/* A tile entity a replayed structure call carries: kind is the oracle's kind
 * byte and text is the oracle's canonical NBT, stored verbatim so te_render
 * reproduces it. name is the TileEntity class name. */
void te_set_raw(struct tile_entity *te, int kind, const char *name, const char *text);

/* The kind a demand creation makes for the block id, 0 for none
 * (BlockPistonMoving's createNewTileEntity returns null). */
int te_kind_of_block(int id);

/* A fresh tile entity of the kind, every field at the Java constructor's
 * default; free with te_free. */
struct tile_entity *te_new(int kind);

/* Release the owned stacks and the entity itself. */
void te_free(struct tile_entity *te);

/* The tile entity classes whose constructors draw a seeder Random:
 * TileEntityDispenser carries field_146021_j = Det.newRandom(), one seeder
 * draw every time a fresh one is constructed. The demand creation spends it;
 * the machine installs the draw, tileentity.c itself stays RNG-free. */

/* writeToNBT as canonical NBT text (nbtjson), malloc'd: the kind's fields
 * plus the id and the position TileEntity.writeToNBT adds. */
char *te_render(const struct tile_entity *te);

/* readFromNBT over a parsed canonical tree: the kind's fields off the tags,
 * the position and kind off id/x/y/z (the caller usually knows both). */
void te_load(struct tile_entity *te, const nbt *tag);

#endif
