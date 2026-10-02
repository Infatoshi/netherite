/* The tile entity half of World.updateEntities (tileticks.c) plus each tick
 * class's updateEntity, exact against the oracle's TileTickProbe
 * (oracle/harness/netherite/oracle/TileTickProbe.java):
 *
 *   - the walk: field_147481_N up, updateEntity on every entity whose block
 *     still exists, iterator removal of the invalid ones (through
 *     Chunk.removeTileEntity), the tail merge of field_147484_a (appended to
 *     field_147482_g when absent, then func_150812_a), then field_147481_N
 *     down;
 *   - TileEntityFurnace.updateEntity: the burn countdown, the ignition (fuel
 *     spent, the container item in), the cook counter at 200, the smelting
 *     result through smelting.c, and the lit/unlit block swap with its
 *     metadata restore (func_149931_a's flag dance);
 *   - TileEntityDaylightDetector: every 20th tick the block metadata follows
 *     the sky light through the celestial angle (both recorded per tick);
 *   - TileEntityHopper.updateEntity: the transfer cooldown decrements and
 *     resets to 0; with config.yaml redstone off the push and pull return
 *     false before touching anything;
 *   - TileEntityChest / TileEntityEnderChest: the player count recount, the
 *     lid fields and the chest's neighbor scan; with a player the recount
 *     sees its open window (tt_env) and the lid's open and close sounds
 *     draw World.rand (tileticks_set_world_rand);
 *   - TileEntityNote, TileEntityEnchantmentTable: the updateEntity bodies the
 *     walk runs (no NBT fields move);
 *   - TileEntityMobSpawner: updateSpawner through tt_env (the whole-server
 *     replay's, serverreplay.c); the probe keeps every spawner out of a
 *     player's range, so it returns before touching anything;
 *   - TileEntityBeacon: every 80th tick the levels recomputed from the
 *     pyramid scan (canBlockSeeTheSky and the emerald/gold/diamond/iron
 *     layers);
 *   - TileEntityBrewingStand: with config.yaml brewing off the ingredient
 *     checks are false, so only the metadata recompute runs;
 *   - TileEntityPiston: the progress advance to 1.0, then the finish: the
 *     entity invalidates itself, the pushed block replaces the extension with
 *     a flag-3 write, and the walk removes it.
 *
 * Nothing here draws from a Random without the world Random set (the probe
 * draws none).
 */
#ifndef NETHERITE_TILETICKS_H
#define NETHERITE_TILETICKS_H

#include <stdint.h>

#include "det.h"
#include "jrand.h"
#include "world.h"

/* One tick of the tile entity half of World.updateEntities. world_time and
 * total_time are World.getWorldTime / getTotalWorldTime; skylight_subtracted
 * and cos_table are the recorded per-tick values the detector's computation
 * reads (glibc's cos is not fdlibm's, so the oracle records them). */
void tileticks_set_time(int64_t world_time, int64_t total_time, int skylight_subtracted, const float *cos_table);

/* World.rand for the chest's open and close sounds; the probe leaves it
 * NULL (no player, so no draw). */
void tileticks_set_world_rand(jrand *world_rand);

/* The pass itself: the walk plus the merge. */
void tileticks_pass(struct world *w);

/* TileEntityEnchantmentTable.updateEntity's step for the table at x, z:
 * near is getClosestPlayer's hit within 3 blocks (px, pz its position), and
 * the class's static Random is the role's stream (the client's copy of a
 * table draws DET_CLIENT's). */
struct te_enchant;
void tileticks_enchant_step(struct te_enchant *e, int x, int z, int near, double px, double pz, det_state *det,
                            int role);

/* What the whole-server loop adds for the player-facing bodies:
 * MobSpawnerBaseLogic.updateSpawner over the replay's entity list and Det
 * streams, and the chest's periodic recount of the players whose open
 * ContainerChest shows it. The probe leaves it unset (NULL): no player, so
 * no spawn and a recount of 0. */
struct tt_env {
    void *ctx;
    void (*spawner)(void *ctx, struct world *w, struct tile_entity *te);
    int (*chest_viewers)(void *ctx, struct world *w, struct tile_entity *te);
    /* World.getClosestPlayer(x, y, z, dist): 1 with the player's posX and
     * posZ when one is nearer than dist (the enchanting table's book) */
    int (*closest_player)(void *ctx, struct world *w, double x, double y, double z, double dist, double *px,
                          double *pz);
    /* the Det the static Randoms draw from (the book's field_145923_r), on
     * the server role */
    det_state *det;
};
void tileticks_set_env(const struct tt_env *env);

/* BlockFurnace.func_149931_a's swap flag, read by world.c's breakBlock. */
int tileticks_furnace_swapping(void);

/* TileEntityFurnace.func_145952_a: the fuel value of an item id (0 when it
 * burns nothing) */
int furnace_fuel_value(int item);

/* The fields TileEntityChest and TileEntityEnderChest's updateEntity move
 * (numPlayersUsing, lidAngle and prevLidAngle) as one canonical NBT line with
 * the tick, for the probe's lid.jsonl.gz comparison: none of them is in
 * writeToNBT. NULL for every other kind. The caller frees. */
char *tileticks_lid_render(const struct tile_entity *te, int tick);

#endif
