/* The random block ticks of 1.7.10, one function per block class and a
 * dispatch by block id: what Block.updateTick does when the server picks the
 * block in a chunk section's 3 random-tick draws (WorldServer.func_147456_g,
 * the updateLCG picks the cell, this file does the rest). The oracle half is
 * netherite.oracle.TickProbe: a scene built around the ticked block, a case
 * seed for World.rand and a fresh Random per case, every write recorded, a
 * 3x3 hash and World.rand's 48-bit state after the case.
 *
 * Each tick runs against the shared world core (world_set_block,
 * world_set_meta, the blockcb callbacks) exactly as vanilla's updateTick
 * calls World.setBlock, flags and all: a grass block dying to dirt writes
 * flags 3, a crop growing one stage writes the metadata with flags 2, a leaf
 * that fell writes air with flags 3 and the write triggers the leaves'
 * breakBlock marking in world.c. The drops a tick performs (a decayed leaf's
 * sapling, a cactus func_147480_a) spend their World.rand draws through the
 * same helpers the recorded state checks; the spawned entities are not part
 * of the world's block state and are not reproduced (see the report).
 *
 * Settings: randomtick_set_skylight gives World.skylightSubtracted, which
 * Chunk.getBlockLightValue subtracts from the sky light; the probe records it
 * and a fresh world reads 0. */
#ifndef NETHERITE_RANDOMTICK_H
#define NETHERITE_RANDOMTICK_H

#include "det.h"
#include "jrand.h"
#include "world.h"

/* Block.field_149783_u, the block whose stored light is not the light the world
 * reports (Block.registerBlocks' rule); World.getBlockLightValue_do takes the
 * maximum of the five neighbours for such a block. */
int rt_light_diffuse(int id);

/* World.skylightSubtracted for the tick light reads. */
void randomtick_set_skylight(int subtracted);

/* The World.rand stream of the tick in progress, published for the block
 * callbacks' drop draws (randomtick_callback_drop); NULL outside a tick. */
void randomtick_tick_rand(jrand *wr);

/* The stream randomtick_tick_rand published, for a callback that needs a draw
 * of its own (BlockFire.onBlockAdded's re-schedule roll); NULL outside a
 * tick. */
jrand *randomtick_tick_rand_stream(void);

/* The tick in progress publishes Det's streams (the drop's EntityItem
 * constructors draw the entity id, its per-entity Random and its UUID, then
 * four Math.random values) and the sink every dropped item goes to: the
 * server tick's spawn recorder. Outside a tick both are NULL. */
void randomtick_tick_det(det_state *det, int role);
void randomtick_tick_drop_sink(void (*sink)(void *ctx, double x, double y, double z, int item, int damage,
                                            int count), void *ctx);

/* The published tick environment as one value, for a nested pass (a chunk
 * populated in the middle of another world's work) to set its own and put
 * the caller's back. */
struct randomtick_env {
    jrand *wr;
    det_state *det;
    int role;
    void (*sink)(void *ctx, double x, double y, double z, int item, int damage, int count);
    void *sink_ctx;
};
void randomtick_tick_save(struct randomtick_env *e);
void randomtick_tick_load(const struct randomtick_env *e);

/* Block.dropBlockAsItemWithChance(..., 1.0F, 0) for the block being replaced
 * at (x, y, z): its quantity, chance and getItemDropped draws, then
 * dropBlockAsItem_do's three nextFloat offsets and one sink call per item.
 * World.rand is the stream published by randomtick_tick_rand. A block whose
 * class spends nothing (air, a liquid, a stone) draws nothing. */
void randomtick_drop(struct world *w, int x, int y, int z, int id, int meta);
/* Block.dropBlockAsItem_do on the published tick stream: the three nextFloat
 * offsets and one sink call, for a caller that already knows the stack (a
 * skull's or a flower pot's breakBlock). Nothing without a stream. */
void randomtick_drop_stack_do(int x, int y, int z, int item, int damage);
/* The same over a caller's World.rand into a caller's sink. */
void randomtick_drop_into(jrand *wr, void (*sink)(void *, double, double, double, int, int, int), void *ctx,
                          int x, int y, int z, int id, int meta);

/* The same draws with no sink and no position: the per-class draw count the
 * probes check (blockcb.c's callbacks and features_springs.c's flow). */
void randomtick_callback_drop(int id, int meta);

/* BlockLiquid.func_149799_m from a block callback: the two World.rand floats
 * and the sixteen Math.random draws, through the stream randomtick_tick_rand
 * published. */
void randomtick_callback_fizz(void);

/* BlockLiquid.func_149799_m, the fizz a lava flow-into plays: two World.rand
 * nextFloat in the sound's arguments and eight pairs of Math.random for the
 * smoke particles. No world state. */
void randomtick_fizz(jrand *r, int x, int y, int z);

/* Block.updateTick for the block id, with wr as World.rand and cr as the
 * fresh java.util.Random the tick was handed. Dispatches on id & 4095:
 * grass, mycelium, leaves and leaves2, wheat, sapling, cactus, reeds, vine,
 * the two stems, cocoa, nether wart, the two mushrooms, ice, snow layer,
 * farmland, redstone ore, lit redstone ore and fire (fire.c). Everything else
 * is Block's empty updateTick. */
void randomtick_update_tick(struct world *w, int id, jrand *wr, jrand *cr, int x, int y, int z);

/* ------------------------------------------------------------ bonemeal
 *
 * ItemDye.func_150919_a's three IGrowable calls, for the blocks with a
 * bonemeal body (grass, sapling, tall grass, the crops, the two stems, cocoa,
 * the two mushrooms and the double plant). The growth bodies live with the
 * random-tick code above so the two paths share them. World.rand is the
 * caller's stream, so a probe that records the state after the use checks the
 * exact draw sequence.
 *
 * BlockDoublePlant's own bonemeal drops its item (an entity the world core
 * does not spawn): the draws are spent through the drop helper, the entity is
 * not, like every other drop in this file. */
int randomtick_is_igrowable(int id);
int randomtick_bonemeal_can(struct world *w, int id, int x, int y, int z);
int randomtick_bonemeal_chance(struct world *w, int id, jrand *wr, int x, int y, int z);
void randomtick_bonemeal_grow(struct world *w, int id, jrand *wr, int x, int y, int z);

#endif
