#ifndef MAGMA_GAME_RUNTIME_H
#define MAGMA_GAME_RUNTIME_H

#include "game/config.h"
#include "game/fluid_live.h"
#include "game/live_sim.h"
#include "game/player_ctl.h"
#include "game/furnace_live.h"
#include "game/chest_live.h"
#include "game/mob_live.h"
#include "game/dragon_live.h"
#include "game/container_live.h"
#include "mc_gamerules.h"
#include "mc_rng.h"

#include <stdint.h>

#define GM_RUNTIME_FURNACES 16
/* Growable chest TE table: starts at this capacity, doubles when full.
 * Never evicts a live TE while its block 54 still exists. */
#define GM_RUNTIME_CHESTS_INITIAL 64
/* Mob-spawner tile entities ingested from Anvil TileEntities / set_tile_entity.
 * Renderer-only: discover_spawners must not invent these. */
#define GM_RUNTIME_SPAWNERS 64
#define GM_RUNTIME_PROJECTILES 32
#define GM_RUNTIME_GHOSTS 16
#define GM_RUNTIME_GHOST_VIEWS 32  /* REC_ENT_MAX in the qrl recorder */
#define GM_RUNTIME_FIREBALL_TRACKS 8
/* Compat alias for tests that still reference the old fixed size. */
#define GM_RUNTIME_CHESTS GM_RUNTIME_CHESTS_INITIAL
/* Client sound seam ring. 256 events is one tick's worth many times over:
 * the consumer drains it every frame and a drop is counted, never silent. */
#define GM_RUNTIME_SOUND_EVENTS 256
/* SoundCategory ordinals (net.minecraft.util.SoundCategory). */
enum {
    GM_SOUND_CATEGORY_MASTER = 0,
    GM_SOUND_CATEGORY_MUSIC,
    GM_SOUND_CATEGORY_RECORDS,
    GM_SOUND_CATEGORY_WEATHER,
    GM_SOUND_CATEGORY_BLOCKS,
    GM_SOUND_CATEGORY_HOSTILE,
    GM_SOUND_CATEGORY_NEUTRAL,
    GM_SOUND_CATEGORY_PLAYERS,
    GM_SOUND_CATEGORY_AMBIENT,
    GM_SOUND_CATEGORY_VOICE
};
enum {
    GM_SOUND_CHICKEN_HURT = 1,
    GM_SOUND_CHICKEN_DEATH,
    GM_SOUND_PIG_HURT,
    GM_SOUND_PIG_DEATH,
    GM_SOUND_COW_HURT,
    GM_SOUND_COW_DEATH,
    GM_SOUND_SHEEP_HURT,
    GM_SOUND_SHEEP_DEATH,
    GM_SOUND_SHEEP_SHEAR,
    GM_SOUND_CHICKEN_EGG,
    GM_SOUND_ITEM_BUCKET_FILL,
    GM_SOUND_ITEM_ARMOR_EQUIP_GENERIC,
    GM_SOUND_PIG_SADDLE,
    GM_SOUND_LIGHTNING_THUNDER,
    GM_SOUND_LIGHTNING_IMPACT,
    GM_SOUND_FIREWORK_LAUNCH,
    GM_SOUND_FIREWORK_BLAST,
    GM_SOUND_FIREWORK_BLAST_FAR,
    GM_SOUND_FIREWORK_LARGE_BLAST,
    GM_SOUND_FIREWORK_LARGE_BLAST_FAR,
    GM_SOUND_FIREWORK_TWINKLE,
    GM_SOUND_FIREWORK_TWINKLE_FAR,
    GM_SOUND_BLOCK_WOOD_BREAK,
    GM_SOUND_BLOCK_GRAVEL_BREAK,
    GM_SOUND_BLOCK_GRASS_BREAK,
    GM_SOUND_BLOCK_STONE_BREAK,
    GM_SOUND_BLOCK_METAL_BREAK,
    GM_SOUND_BLOCK_GLASS_BREAK,
    GM_SOUND_BLOCK_CLOTH_BREAK,
    GM_SOUND_BLOCK_SAND_BREAK,
    GM_SOUND_BLOCK_SNOW_BREAK,
    GM_SOUND_BLOCK_LADDER_BREAK,
    GM_SOUND_BLOCK_ANVIL_BREAK,
    GM_SOUND_BLOCK_SLIME_BREAK,
    GM_SOUND_BLOCK_WOOD_PLACE,
    GM_SOUND_BLOCK_GRAVEL_PLACE,
    GM_SOUND_BLOCK_GRASS_PLACE,
    GM_SOUND_BLOCK_STONE_PLACE,
    GM_SOUND_BLOCK_METAL_PLACE,
    GM_SOUND_BLOCK_GLASS_PLACE,
    GM_SOUND_BLOCK_CLOTH_PLACE,
    GM_SOUND_BLOCK_SAND_PLACE,
    GM_SOUND_BLOCK_SNOW_PLACE,
    GM_SOUND_BLOCK_LADDER_PLACE,
    GM_SOUND_BLOCK_ANVIL_PLACE,
    GM_SOUND_BLOCK_SLIME_PLACE,
    GM_SOUND_BLOCK_WOOD_HIT,
    GM_SOUND_BLOCK_GRAVEL_HIT,
    GM_SOUND_BLOCK_GRASS_HIT,
    GM_SOUND_BLOCK_STONE_HIT,
    GM_SOUND_BLOCK_METAL_HIT,
    GM_SOUND_BLOCK_GLASS_HIT,
    GM_SOUND_BLOCK_CLOTH_HIT,
    GM_SOUND_BLOCK_SAND_HIT,
    GM_SOUND_BLOCK_SNOW_HIT,
    GM_SOUND_BLOCK_LADDER_HIT,
    GM_SOUND_BLOCK_ANVIL_HIT,
    GM_SOUND_BLOCK_SLIME_HIT,
    GM_SOUND_BOBBER_SPLASH,
    GM_SOUND_DISPENSER_DISPENSE,
    GM_SOUND_DISPENSER_FAIL,
    GM_SOUND_DISPENSER_LAUNCH,
    GM_SOUND_ENDEREYE_LAUNCH,
    GM_SOUND_FIREWORK_SHOOT,
    GM_SOUND_IRON_DOOR_OPEN,
    GM_SOUND_WOODEN_DOOR_OPEN,
    GM_SOUND_WOODEN_TRAPDOOR_OPEN,
    GM_SOUND_FENCE_GATE_OPEN,
    GM_SOUND_FIRE_EXTINGUISH,
    GM_SOUND_IRON_DOOR_CLOSE,
    GM_SOUND_WOODEN_DOOR_CLOSE,
    GM_SOUND_WOODEN_TRAPDOOR_CLOSE,
    GM_SOUND_FENCE_GATE_CLOSE,
    GM_SOUND_GHAST_WARN,
    GM_SOUND_GHAST_SHOOT,
    GM_SOUND_ENDERDRAGON_SHOOT,
    GM_SOUND_BLAZE_SHOOT,
    GM_SOUND_ZOMBIE_ATTACK_DOOR_WOOD,
    GM_SOUND_ZOMBIE_ATTACK_IRON_DOOR,
    GM_SOUND_ZOMBIE_BREAK_DOOR_WOOD,
    GM_SOUND_WITHER_BREAK_BLOCK,
    GM_SOUND_WITHER_SHOOT,
    GM_SOUND_BAT_TAKEOFF,
    GM_SOUND_ZOMBIE_INFECT,
    GM_SOUND_ZOMBIE_VILLAGER_CONVERTED,
    GM_SOUND_ANVIL_DESTROY,
    GM_SOUND_ANVIL_USE,
    GM_SOUND_ANVIL_LAND,
    GM_SOUND_PORTAL_TRAVEL,
    GM_SOUND_CHORUS_FLOWER_GROW,
    GM_SOUND_CHORUS_FLOWER_DEATH,
    GM_SOUND_BREWING_STAND_BREW,
    GM_SOUND_IRON_TRAPDOOR_CLOSE,
    GM_SOUND_IRON_TRAPDOOR_OPEN,
    GM_SOUND_SPLASH_POTION_BREAK,
    GM_SOUND_ENDERDRAGON_FIREBALL_EXPLODE,
    GM_SOUND_END_GATEWAY_SPAWN,
    GM_SOUND_ENDERDRAGON_GROWL,
    GM_SOUND_VILLAGER_YES,
    GM_SOUND_VILLAGER_NO,
    GM_SOUND_RECORD_STOP,
    GM_SOUND_RECORD_13,
    GM_SOUND_RECORD_CAT,
    GM_SOUND_RECORD_BLOCKS,
    GM_SOUND_RECORD_CHIRP,
    GM_SOUND_RECORD_FAR,
    GM_SOUND_RECORD_MALL,
    GM_SOUND_RECORD_MELLOHI,
    GM_SOUND_RECORD_STAL,
    GM_SOUND_RECORD_STRAD,
    GM_SOUND_RECORD_WARD,
    GM_SOUND_RECORD_11,
    GM_SOUND_RECORD_WAIT,
    GM_SOUND_COUNT
};
/* One resolved client sound. Simulation producers append identity, category,
 * source, volume, and pitch; playback (game/audio_live.c) is a pure consumer
 * and never writes back. `seq` is monotone across ring wrap so a consumer that
 * misses a frame can count what it dropped instead of silently skipping. */
typedef struct {
    uint64_t seq;
    int sound, category, eid, dimension;
    int relative, delay_ticks;
    double x, y, z;
    float volume, pitch;
} GmRuntimeSoundEvent;

typedef struct {int active,type,age;double x,y,z,vx,vy,vz;} GmRuntimeProjectile;
typedef struct {
    int active, wx, wy, wz;
    FurnaceLive state;
} GmRuntimeFurnace;
typedef struct {
    int active, wx, wy, wz;
    ChestLive state;
} GmRuntimeChest;
typedef struct {
    int active, dim, wx, wy, wz, entity_type;
    float mob_rotation;
} GmRuntimeSpawnerTE;
/* Layout matches GmSpawnerView field-for-field except the names. Filled by
 * gm_runtime_spawner_views; the mapper stays out of this translation unit. */
typedef struct {
    int wx, wy, wz, entity_type;
    float mob_rotation;
} GmRuntimeSpawnerView;

typedef struct GmRuntime {
    GmWorld *world;
    GmWorld *worlds[3]; /* index dimension+1: Nether, Overworld, End */
    Chunk *window;
    McSinTable sin_table;
    PsvPlayer player;
    PvStats vitals;
    McGameRules gamerules;
    GmWorldClock clock;
    GmLiveSim entities;
    GmMobLive mobs;
    GmDragonLive dragon;
    GmRuntimeProjectile projectiles[GM_RUNTIME_PROJECTILES];
    /* Magma-only EntityArrow inGround/arrowShake/pickupStatus. PlProj layout
     * stays blaze-identical; these ride beside the shared projectile slots. */
    int proj_in_ground[GM_RUNTIME_PROJECTILES];
    int proj_shake[GM_RUNTIME_PROJECTILES];
    int proj_pickup[GM_RUNTIME_PROJECTILES]; /* 0 DISALLOWED 1 ALLOWED 2 CREATIVE_ONLY */
    int proj_ground_ticks[GM_RUNTIME_PROJECTILES];
    unsigned parity_proj_hits;
    unsigned parity_ex_blasts;
    unsigned parity_ex_destroyed;
    unsigned parity_ex_drop_n;
    uint64_t parity_ex_drop_ids;
    float parity_ex_damage;
    double parity_ex_kb_x, parity_ex_kb_y, parity_ex_kb_z;
    uint64_t parity_ex_rays;
    double parity_ex_last_x, parity_ex_last_y, parity_ex_last_z;
    float parity_ex_last_size;
    int bow_ticks,bow_drawing;
    int player_fire_ticks; /* Entity.fire, setFire(seconds) stores seconds*20 */
    int ccx, ccz;
    int ox, oz;
    /* Feet position (world coords) as of the ENTRY of the last gm_runtime_tick:
     * server pose packets have landed, this tick's own movement has not. That
     * is the position EntityRenderer.updateRenderer samples for the fogColor1
     * light term - see gm_runtime_tick_entry_feet. */
    double te_x, te_y, te_z;
    int te_valid;
    /* physics-window fill memo: refill only on recenter / world switch / block
     * mutation (gm_world_block_gen). The unconditional per-tick refill was 94%
     * of a physics-only tape replay (find_chunk+light_state, perf 2026-07-10). */
    const GmWorld *win_world;
    int win_ccx, win_ccz;
    long long win_gen;
    int dead, deaths, won, credits;
    int score;                 /* EntityPlayer.getScore (GuiGameOver line) */
    int death_screen_ticks;    /* GuiGameOver.enableButtonsTimer */
    int quit_to_title;         /* Title Screen confirmed / episode end */
    int dimension;
    int portal_time, portal_cooldown;
    long long seed;
    long long tick;
    /* World.rand (World.java:108). Unseeded in Java; live cursor is the
     * snapshot v5 trailer. Tape-exact values are Class C. */
    JavaRandom world_rand;
    /* World.updateLCG (World.java:95). Unseeded nextInt() in the World ctor;
     * live default 0 so magma and blaze share it. Snapshot v6. */
    i32 update_lcg;
    int weather_enabled;
    int elytra_kit;              /* --elytra on: chest 443 after snapshot-in */
    /* Tape getRainStrength(1)/getThunderStrength(1). Live stays 0: magma has
     * WorldInfo raining flags but no rainingStrength fade. */
    float rain_strength, thunder_strength;
    int mobs_enabled; /* --mobs off skips AI/spawn; spine still ticks */
    int natural_spawn; /* WorldEntitySpawner MONSTER cycle; default 0 */
    int natural_spawn_passive; /* WorldEntitySpawner CREATURE; default 0 */
    /* Live/window random block ticks (game/randtick.c). Default ON for interactive
     * play and unit tests; script/tape replay sets 0 so the unseedable oracle
     * world RNG is not approximated here. */
    int randtick_enabled;
    int randtick_radius; /* Chebyshev chunk radius around player for the pass */
    int container; /* 0 player 2x2, 1 crafting table, 2 furnace, 3 chest */
    int container_wx, container_wy, container_wz;
    int active_furnace;
    int active_chest;
    ICStack craft_grid[9]; /* live craft matrix (container_live slot ids 36..44) */
    /* Authoritative parity-visible protocol history. Counters advance only
     * when the represented transition is attempted/completed. */
    unsigned int parity_craft_attempts, parity_craft_successes;
    unsigned int parity_container_opens;
    ICStack parity_last_craft;
    GmRuntimeFurnace furnaces[GM_RUNTIME_FURNACES];
    GmRuntimeChest *chests; /* growable; capacity in chests_cap */
    int chests_cap;
    GmRuntimeSpawnerTE spawners[GM_RUNTIME_SPAWNERS];
    GmFluidLive fluids;    /* live water/lava flow region (game/fluid_live.c) */
    /* Tape-replay ghost pushers: recorded oracle entity boxes (world coords,
     * feet y, full width/height) injected per tick; gm_runtime_tick applies
     * the vanilla applyEntityCollision player push from them after the player
     * update and clears the list. Live mob pushes are NOT routed here. */
    struct { double x, y, z, w, h; } ghosts[GM_RUNTIME_GHOSTS];
    int nghosts;
    /* Tape-replay RENDERABLE ghost entities (divergence #10): recorded oracle
     * entities (mapped type id + pose) held for the frame captured after this
     * tick. Render-only - never touches physics/progression; the pusher list
     * above keeps its own separate, physics-verified semantics. The script
     * loop clears this at the top of every tick. */
    GmEntityView ghost_views[GM_RUNTIME_GHOST_VIEWS];
    int nghost_views;
    /* Exact-double EntityBoat pose from the current tape row. Unlike the
     * render ghost above, this drives the recorded local player's riding
     * relationship: mount/dismount packets take effect one client tick after
     * the input, then EntityBoat.updatePassenger pins the player's feet at
     * boat y + getMountedYOffset() + EntityPlayer.getYOffset(), evaluated
     * through EntityBoat's float local, is y - 0.44999998807907104. */
    struct { int valid, ent_id; double x, y, z, yaw; } tape_boat;
    int tape_boat_ride_id;       /* -1 while the recorded player is on foot */
    int tape_boat_mount_pending; /* entity id; activates next client tick */
    int tape_boat_dismount_pending;
    int tape_boat_mount_message_ticks;
    float tape_boat_paddle[2];
    double tape_boat_prev_yaw;
    int tape_boat_prev_yaw_valid;
    /* Tape rows carry a nearby EntityLargeFireball removal but not its
     * SPacketExplosion. Retain enough trajectory to reconstruct the first
     * renderable ParticleExplosionLarge puff on a player-hit removal. */
    struct { int ent_id; float x, y, z, dx, dy, dz; }
        tape_large_fireballs[GM_RUNTIME_FIREBALL_TRACKS];
    int ntape_large_fireballs;
    struct { int active, ent_id, age; float x, y, z; }
        tape_fireball_impacts[GM_RUNTIME_FIREBALL_TRACKS];
    /* Tape-replay open GUI screen (divergence #9): render-only. When set,
     * frame capture draws gm_screen_draw after the HUD using this container
     * kind + ScaledResolution mouse coords. Cleared each tick like ghost_views.
     * Does NOT mutate r->container (physics/close distance stay untouched). */
    int gui_view_active;     /* 1 if a mapped gui_view event landed this tick */
    int gui_view_container;  /* 0 player, 1 workbench, 2 furnace, 3 chest */
    int gui_view_mx, gui_view_my; /* vanilla ScaledResolution mouse coords */
    /* Exact post-tick container render truth. Unlike the live container, these
     * slots/cursor/progress never participate in click or furnace simulation.
     * They are cleared with gui_view at the start of every replay tick. */
    ICStack tape_gui_slots[GMC_SLOT_COUNT];
    unsigned char tape_gui_slot_active[GMC_SLOT_COUNT];
    ICStack tape_gui_cursor;
    int tape_gui_cursor_active;
    int tape_furnace_active;
    int tape_furnace_burn, tape_furnace_current_burn;
    int tape_furnace_cook, tape_furnace_total_cook;
    /* Post-tick oracle inventory used only for this tick's hand/HUD/GUI. The
     * replay separately re-anchors the live inventory before the next tick so
     * current-tick actions still consume their true pre-tick stacks. */
    IsrInv tape_inv;
    int tape_inv_active;
    int tape_xp_active, tape_xp_level;
    float tape_xp_frac;
    int tape_air;
    float tape_portal;
    int tape_portal_frame, tape_portal_phase, tape_loading;
    int tape_texture_animations_pinned;
    int tape_fire, tape_creative, tape_hurt_time, tape_max_hurt_time;
    float tape_hurt_yaw, tape_attack_cooldown;
    int tape_potion_count;
    GmPotionEffectView tape_potions[GM_MAX_POTION_EFFECTS];
    /* Recorded ForgeHooks.getTotalArmorValue. -1 = the tape did not carry it
     * (pre-2026-07-29 schema); the view then keeps the item-derived guess. */
    int tape_armor_points;
    /* Ordered, allocation-free client sound seam. Simulation producers append
     * resolved sound identity, category, source, volume, and pitch; playback
     * only reads. Nothing in this block feeds back into a simulation decision,
     * so a build with audio compiled out is bit-identical (game/audio_live.c). */
    GmRuntimeSoundEvent sound_events[GM_RUNTIME_SOUND_EVENTS];
    int sound_event_head, sound_event_count;
    uint64_t sound_event_next_seq, sound_event_dropped;
    /* EntityPlayer.sleeping / sleepTimer / bedLocation / spawnChunk.
     * Not in blaze_snapshot.h (RL has no sleep; resumegate owns that file). */
    int player_sleeping;
    int sleep_timer;
    int bed_head_x, bed_head_y, bed_head_z;
    int spawn_chunk_set;
    int spawn_chunk_x, spawn_chunk_y, spawn_chunk_z;
    int spawn_forced;
    int world_spawn_x, world_spawn_y, world_spawn_z;
} GmRuntime;

int  gm_runtime_init(GmRuntime *r, const GmConfig *cfg, char *err, int err_cap);
void gm_runtime_destroy(GmRuntime *r);
/* The only authoritative survival transition used by interactive and harness play. */
void gm_runtime_tick(GmRuntime *r, GmAction action);
void gm_runtime_view(const GmRuntime *r, GmPlayerView *out);
/* Test-hook pose mutation. It changes travel state only, never progression state. */
void gm_runtime_set_pose(GmRuntime *r, double x, double y, double z,
                         float yaw, float pitch);
void gm_runtime_set_pose_state(GmRuntime *r, double x, double y, double z,
                               float yaw, float pitch, double vx, double vy,
                               double vz, int on_ground, float fall_distance);
void gm_runtime_set_velocity(GmRuntime *r, double x, double y, double z);
void gm_runtime_set_packet_velocity(GmRuntime *r, double x, double y, double z);
void gm_runtime_add_velocity(GmRuntime *r, double x, double y, double z);
/* Tape/live equipment bridge for EntityEquipmentSlot.CHEST == Items.ELYTRA. */
void gm_runtime_set_elytra(GmRuntime *r, int equipped);
/* Enable tape-authoritative flag-7 metadata timing and apply an observed
 * player metadata value before the current tick. */
void gm_runtime_set_elytra_flag7(GmRuntime *r, int flying);
/* Absolute camera rotation only (tape replay of recorded mouse look). */
void gm_runtime_set_look(GmRuntime *r, float yaw, float pitch);
/* Tape replay: register a recorded oracle entity box (world coords, feet y,
 * width w, height h) as a ghost pusher for the NEXT gm_runtime_tick. */
void gm_runtime_ent_box(GmRuntime *r, double x, double y, double z,
                        double w, double h);
/* Tape replay: apply EntityDragon.collideWithEntities / attackEntitiesInList
 * damage only when the recorded part query box overlaps the live player. */
int gm_runtime_dragon_contact(GmRuntime *r, double min_x, double min_y,
                              double min_z, double max_x, double max_y,
                              double max_z, float damage);
/* Tape replay: register a recorded oracle entity for RENDERING at this tick's
 * frame capture (type = EW_TYPE_* model id). Render-only; no physics effect.
 * ent_id is the tape entity id (for hurtTime/limbSwing continuity); pass -1
 * if unknown. */
void gm_runtime_ent_view(GmRuntime *r, const GmEntityView *view);
/* Preserve an EntityBoat tape row at JSON double precision for passenger
 * position following. Call before gm_runtime_ent_view for the same entity. */
void gm_runtime_tape_boat_view(GmRuntime *r, int ent_id, double x, double y,
                               double z, double yaw);
void gm_runtime_ent_views_clear(GmRuntime *r);
/* Fill `out` with this tick's renderable ghost entities; returns count. */
int gm_runtime_ghost_views(const GmRuntime *r, GmEntityView *out, int max);
/* Tape replay: register an open container GUI for this tick's frame capture
 * (container 0/1/2/3; mx/my = vanilla ScaledResolution coords). Render-only. */
void gm_runtime_gui_view(GmRuntime *r, int container, int mx, int my);
void gm_runtime_gui_view_clear(GmRuntime *r);
/* Returns 1 if a gui_view is active this tick; writes container/mx/my. */
int gm_runtime_gui_view_get(const GmRuntime *r, int *container, int *mx, int *my);
int gm_runtime_tape_gui_slot(GmRuntime *r, int slot, int item, int count, int meta);
int gm_runtime_tape_gui_cursor(GmRuntime *r, int item, int count, int meta);
/* Same as above but retain StoredEnchantments subset (optional tape extension). */
int gm_runtime_tape_gui_slot_stack(GmRuntime *r, int slot, ICStack stack);
int gm_runtime_tape_gui_cursor_stack(GmRuntime *r, ICStack stack);
int gm_runtime_tape_gui_slot_get(const GmRuntime *r, int slot, ICStack *out);
int gm_runtime_tape_gui_cursor_get(const GmRuntime *r, ICStack *out);
int gm_runtime_tape_furnace(GmRuntime *r, int burn, int current_burn,
                            int cook, int total_cook);
/* Render-only post-tick tape state. Inventory persists until the next delta. */
int gm_runtime_tape_inventory(GmRuntime *r, int slot, int item, int count, int meta);
void gm_runtime_tape_player_view(GmRuntime *r, int xp_level, float xp_frac, int air,
                                 float portal, int portal_frame, int portal_phase,
                                 int loading, int texture_animations_pinned,
                                 int fire, int creative, int hurt_time,
                                 int max_hurt_time, float hurt_yaw,
                                 float attack_cooldown);
void gm_runtime_tape_potions_clear(GmRuntime *r);
int gm_runtime_tape_potion(GmRuntime *r, int id, int amplifier, int duration,
                           int show_particles);
/* points < 0 clears the override (fall back to the item-derived value). */
void gm_runtime_tape_armor(GmRuntime *r, int points);
void gm_runtime_apply_tape_view(const GmRuntime *r, GmPlayerView *view);

/* Feet position at the entry of the last gm_runtime_tick, in world coords.
 * EntityRenderer.updateRenderer runs before the local player's movement update
 * within Minecraft.runTick, so its fogColor1 light sample sees the pre-move
 * position - but AFTER the network phase, so a server pose packet (the tape's
 * pre-tick set_pose) is already applied. Falls back to the live view before
 * the first tick. */
void gm_runtime_tick_entry_feet(const GmRuntime *r,
                                double *x, double *y, double *z);
/* Seed recorded vitals at tape-replay start. */
void gm_runtime_set_vitals(GmRuntime *r, float health, int food);
/* Interactive / harness respawn (GuiGameOver Respawn button / SPacketRespawn).
 * Restores health to 20, clears dead + fire/hurt, resets death_screen_ticks. */
void gm_runtime_respawn(GmRuntime *r);
int gm_runtime_set_dimension(GmRuntime *r, int dimension);
void gm_runtime_set_time(GmRuntime *r, long long world_time);
void gm_runtime_set_total_time(GmRuntime *r, long long total_time);
/* Tape/live GameRules. Runtime mechanics currently honor naturalRegeneration,
 * doDaylightCycle, and doWeatherCycle; script.c consumes other header entries
 * without changing today's simulation. */
void gm_runtime_set_gamerules(GmRuntime *r, const McGameRules *gamerules);
int gm_runtime_set_block(GmRuntime *r, int x, int y, int z, int id, int meta);
/* Post-tick tape reanchor: write the recorded client final id/meta into the
 * live GmWorld (light + mesh dirty). Fluid CA is marked only when the new cell
 * or a 6-neighbour is liquid. Fall schedules and plant-break cascades are
 * skipped: the tape already carries every final gravity/plant cell, and
 * re-arming BlockFalling here invents a second fall. */
int gm_runtime_reanchor_block(GmRuntime *r, int x, int y, int z, int id, int meta);
/* Snapshot initialization: canonical cell replacement with no fluid/plant
 * mutation side effects. Must run before the first replay tick. */
int gm_runtime_load_block(GmRuntime *r, int x, int y, int z, int id, int meta);
int gm_runtime_snapshot_region(GmRuntime *r, int ccx, int ccz, int radius);
int gm_runtime_load_block_dim(GmRuntime *r, int dimension, int x, int y, int z,
                              int id, int meta);
int gm_runtime_snapshot_region_dim(GmRuntime *r, int dimension,
                                   int ccx, int ccz, int radius);
/* Tile-entity store for mob spawners. entity_type is EW_TYPE_* or -1 (no
 * cached entity). Rotation is MobSpawnerBaseLogic.mobRotation in pre-x10
 * units. Does not consult discover_spawners. */
int gm_runtime_set_tile_entity(GmRuntime *r, int dim, int x, int y, int z,
                               int entity_type, float rotation);
int gm_runtime_spawner_views(const GmRuntime *r, GmRuntimeSpawnerView *out,
                             int max);
int gm_runtime_set_inventory(GmRuntime *r, int slot, int item, int count, int meta);
void gm_runtime_set_weather(GmRuntime *r, int raining, int thundering,
                            int rain_time, int thunder_time);
void gm_runtime_set_rain_thunder(GmRuntime *r, float rain, float thunder);
int gm_runtime_projectile_views(const GmRuntime *r, GmEntityView *out, int max);
/* Execute one survival crafting take from inventory-backed grid slots. Empty
 * cells are -1. Returns 1 only if a recipe matched and the output fit. */
int gm_runtime_craft(GmRuntime *r, int grid_width, const int inv_slots[9]);
/* Survival use at a world block. Verifies reach and block identity before opening. */
int gm_runtime_use_block(GmRuntime *r, int wx, int wy, int wz);
int gm_runtime_furnace_insert(GmRuntime *r, int furnace_slot,
                              int inventory_slot, int amount);
int gm_runtime_furnace_extract(GmRuntime *r, int furnace_slot, int amount);

/* Tape-authoritative container lifecycle (gopen/gclk/gclose). ctype:
 * 0 player 2x2, 1 workbench, 2 furnace, 3 single chest. Block ctypes open via
 * use_block at (wx,wy,wz); player ignores coords. Seeds never invent clicks. */
int gm_runtime_container_open(GmRuntime *r, int ctype, int wx, int wy, int wz);
/* Seed one live GMC slot after open (grid/furnace/chest only; inv via
 * set_inventory). Empty count clears. Returns 0 on invalid slot/container. */
int gm_runtime_container_seed_slot(GmRuntime *r, int gmc_slot,
                                   int item, int count, int meta);
void gm_runtime_container_seed_cursor(GmRuntime *r, int item, int count, int meta);
int gm_runtime_container_seed_furnace_prop(GmRuntime *r, int burn,
                                           int current_burn, int cook,
                                           int total_cook);
/* Close open container (grid/cursor return, chest lid). Always succeeds. */
void gm_runtime_container_force_close(GmRuntime *r);

/* ---- client sound seam (game/audio_live.c is the only consumer) ----
 * These read the ring written during gm_runtime_tick. The ring is drained by
 * index, not popped: the consumer tracks its own `seq` watermark so two
 * consumers (playback and a test) never steal each other's events. */
int gm_runtime_sound_event_count(const GmRuntime *r);
int gm_runtime_sound_event_get(
    const GmRuntime *r, int index, GmRuntimeSoundEvent *out);
/* Drop every retained event. The interactive loop calls this once per tick
 * after playback has consumed the ring; tests call it to isolate a phase. */
void gm_runtime_sound_events_clear(GmRuntime *r);
/* Block SoundType resolution, state_id = legacy id | (meta << 12). Volume and
 * pitch are the exact 1.11.2 SoundType arithmetic per action, so the returned
 * floats are bit-comparable against qrl.BlockBreakSoundGolden. Returns 0 for
 * air and unregistered ids - a caller must never fabricate a sound for those. */
int gm_runtime_block_break_sound(
    int state_id, int *sound, float *volume, float *pitch);
int gm_runtime_block_place_sound(
    int state_id, int *sound, float *volume, float *pitch);
int gm_runtime_block_hit_sound(
    int state_id, int *sound, float *volume, float *pitch);
/* Test fixtures: append the sound a break/place at (x,y,z) would emit without
 * running a player tick. Return 0 when the state resolves to no sound. */
int gm_runtime_block_break_audio_fixture(
    GmRuntime *r, int x, int y, int z, int state_id);
int gm_runtime_block_place_audio_fixture(
    GmRuntime *r, int x, int y, int z, int state_id);

#endif
