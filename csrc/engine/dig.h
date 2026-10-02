/* The server-side block-breaking state machine, vanilla 1.7.10: the
 * ItemInWorldManager fields and methods (onBlockClicked, updateBlockRemoving
 * with its durability progress and the delayed break, cancelDestroyingBlock,
 * uncheckedTryHarvestBlock, tryHarvestBlock, removeBlock), what a break does
 * (onBlockHarvested, setBlockToAir, onBlockDestroyedByPlayer, harvestBlock
 * with its drops and XP, the held stack's damage through
 * ItemStack.func_150999_a and breaking stacks, the exhaustion), and the
 * neighbour notifications the break's write runs.
 *
 * The machine runs on the real native world (world.c): the setBlockToAir
 * write is world_set_block with flag 3, so the replaced block's breakBlock
 * (the leaves and log decay marking), the tile entity removal and the six
 * neighbours' onNeighborBlockChange are the world core's own, and the door
 * pop is blockcb.c's BlockDoor handler. The chest's contents are dropped by
 * this port (BlockChest.breakBlock, the chest's own Random) before the write,
 * because world.c's block_break does not drop container contents.
 *
 * The block's own Random consumers hand the world Random over as a seed, the
 * way harvestdrops.c does: every dropBlockAsItem call runs through drops.c
 * with the current state passed in and the final state written back through
 * drop_case.out_seed, so the stream continues exactly. The two direct
 * dropBlockAsItem_do shapes (the silk-touch stack and the double plant's
 * sheared pair) draw their three position floats from the same stream here.
 *
 * The spawned entities are recorded, not ticked: the oracle's dig probe takes
 * them out of the world at the end of every case, and nothing on the dig path
 * ticks an entity. Construction is bit for bit: the three world-Random
 * offsets, the EntityItem constructor's four Math.random draws, and the Det
 * boilerplate (per-role id, the entity's Random seed and UUID) in the same
 * order as blockcb.c's drop_as_item.
 *
 * Creative mode is out of scope: the dig probe is a survival player, and the
 * creative paths need a NetHandlerPlayServer (tryHarvestBlock's creative
 * branch sends an S23 packet). Adventure mode is out of scope for the same
 * reason. extinguishFire is a no-op: the dig region holds no fire. The
 * redstone ore's onBlockClicked (which lights the ore) and the door and bed
 * neighbour pops are ported; the ore's particle half has no world effect. */
#ifndef NETHERITE_DIG_H
#define NETHERITE_DIG_H

#include <stdint.h>

#include "det.h"
#include "drops.h"
#include "jrand.h"
#include "world.h"

/* The drive ops, the dig probe's op bytes. */
enum { DIG_OP_TICK = 0, DIG_OP_CANCEL = 1, DIG_OP_FINISH = 2 };

/* The ItemInWorldManager fields. */
typedef struct {
    int is_destroying, initial_damage, part_x, part_y, part_z;
    int curblock_damage;
    int finish, pos_x, pos_y, pos_z, initial_block;
    int durability;
    /* the block as the machine clicked it: the replay's client removes its
     * own target a tick before the server's break lands (the oracle's
     * WorldClient prediction), so the harvest reads these when the live
     * cell reads air */
    int initial_id, initial_meta;
} dig_mgr;

/* The player the machine digs with: the held stack, the enchant levels, the
 * pose, the food stats' exhaustion and the entity's own Random (the stack
 * damage draws). haste and fatigue are amplifier + 1, 0 = not active. */
typedef struct {
    int held_item, held_damage, held_count;
    int held_was;       /* the item the held stack broke as, this dig tick */
    int eff, unbr, silk, fortune;
    int on_ground, in_water;
    int haste, fatigue;
    float exhaustion;
    jrand prand;
    int entity_id;
    int stat_mine, stat_use, stat_break;
    int stat_mine_block;    /* the block whose harvestBlock fired stat_mine */
} dig_player;

/* One spawned entity, as the oracle's dig probe records it. item 0xffff is
 * the null stack a block without an item leaves. */
typedef struct {
    int kind;            /* DROP_ITEM / DROP_XP */
    int item, damage, count, xp;
    int entity_id;
    int64_t uuid_msb, uuid_lsb;
    uint64_t rand_state;
    float hover;
    double x, y, z, mx, my, mz;
    float yaw;
    /* a silverfish record: the role's Math.random stream as its constructor
     * found it (EntityLivingBase's three draws are spent at the record, in
     * Java's order ahead of the harvest's own drops) */
    det_rng fish_math;
    int spill;           /* a breakBlock spill: delayBeforeCanPickup 0, not
                          * dropBlockAsItem_do's 10 */
    int tag;             /* a spill's stack tag (itemtag.h), 0 for none */
} dig_ent;

#define DIG_MAX_ENTS DROPS_MAX_ENTS

/* What one case leaves behind, compared against the oracle's caseout record. */
typedef struct {
    int held_item, held_damage, held_count;
    float exhaustion;
    int stat_mine, stat_use, stat_break;
    uint64_t wr_state, math_state, seeder_state, chest_state;
    int32_t next_id;
    int broke;
} dig_out;


typedef struct dig_env {
    struct world *w;
    int role;            /* the Det role the machine's draws land on: DET_OTHER
                          * for the probe's own thread, DET_SERVER for the tape
                          * replay (the dig runs inside the server's network
                          * tick). Case setup only reseeds the OTHER math
                          * stream; the server role's continues. */
    det_state *det;
    jrand wr;            /* the case's world Random */
    det_rng chest;       /* BlockChest.field_149955_b, with its own Gaussian cache */
    int block_rands;     /* the chest spills from block_rand (spill.h), not
                          * from chest: the live replay sets it */
    int net_ops;         /* the live replay: the ops are the network tick's
                          * packets, after the world tick's updateBlockRemoving
                          * (DIG_OP_TICK), and run none of their own; the probe
                          * ticks the machine after each op */
    dig_player p;
    dig_mgr m;
    int x, y, z;         /* the target */
    int case_index;      /* the progress rows' case */

    dig_ent ents[DIG_MAX_ENTS];
    int nents;
    int broke;           /* the drive's own air detection, the oracle's broke flag */

    /* One destroyBlockInWorldPartially call, in call order. */
    void (*partial)(void *ctx, int casei, int entid, int x, int y, int z, int stage);
    void *partial_ctx;
} dig_env;

/* det must already carry the run's OTHER-role state (seeder, math, entity
 * ids). chest_seed is Det.state(BlockChest.field_149955_b) at the run start;
 * the chest's Random carries across the cases, exactly as the oracle's one
 * instance does. */
void dig_init(dig_env *d, struct world *w, det_state *det, uint64_t chest_seed);

/* The case record's fields: the target, the held stack and its enchants, the
 * pose, the two potion amplifiers, the world Random's seed, the player
 * Random's seed and the chest Random's state (48-bit, the seed field). */
void dig_case_begin(dig_env *d, int casei, int x, int y, int z, int held_item, int held_damage,
                    int held_count, int eff, int unbr, int silk, int fortune, int on_ground,
                    int in_water, int haste, int fatigue, int64_t opseed, int64_t prand_seed);

/* One drive op at (x, y, z): DIG_OP_TICK runs updateBlockRemoving alone,
 * DIG_OP_CANCEL cancels first (and ends the case), DIG_OP_FINISH sends the
 * finish packet first. Tick 0 is the caller's dig_op_click below. */
void dig_op(dig_env *d, int op, int x, int y, int z);

/* Tick 0: onBlockClicked at (x, y, z, side), then updateBlockRemoving. */
void dig_op_click(dig_env *d, int x, int y, int z, int side);

/* The case's records. broke is 1 when the target is air. */
void dig_case_end(dig_env *d, dig_out *out);

/* Point blockcb's environment (the pops' World.rand draws and item sink) at
 * the machine, as dig_init does; a caller that keeps the machine across
 * ticks rebinds it for each op and restores its own environment after. */
void dig_bind_blockcb(dig_env *d);

#endif
