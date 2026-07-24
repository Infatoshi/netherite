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

#define GM_RUNTIME_FURNACES 16
/* Growable chest TE table: starts at this capacity, doubles when full.
 * Never evicts a live TE while its block 54 still exists. */
#define GM_RUNTIME_CHESTS_INITIAL 64
#define GM_RUNTIME_PROJECTILES 32
#define GM_RUNTIME_GHOSTS 16
#define GM_RUNTIME_GHOST_VIEWS 32  /* REC_ENT_MAX in the qrl recorder */
/* Compat alias for tests that still reference the old fixed size. */
#define GM_RUNTIME_CHESTS GM_RUNTIME_CHESTS_INITIAL
typedef struct {int active,type,age;double x,y,z,vx,vy,vz;} GmRuntimeProjectile;
typedef struct {
    int active, wx, wy, wz;
    FurnaceLive state;
} GmRuntimeFurnace;
typedef struct {
    int active, wx, wy, wz;
    ChestLive state;
} GmRuntimeChest;

typedef struct GmRuntime {
    GmWorld *world;
    GmWorld *worlds[3]; /* index dimension+1: Nether, Overworld, End */
    Chunk *window;
    McSinTable sin_table;
    PsvPlayer player;
    PvStats vitals;
    GmWorldClock clock;
    GmLiveSim entities;
    GmMobLive mobs;
    GmDragonLive dragon;
    GmRuntimeProjectile projectiles[GM_RUNTIME_PROJECTILES];
    int bow_ticks,bow_drawing;
    int player_fire_ticks; /* Entity.fire, setFire(seconds) stores seconds*20 */
    int ccx, ccz;
    int ox, oz;
    /* physics-window fill memo: refill only on recenter / world switch / block
     * mutation (gm_world_block_gen). The unconditional per-tick refill was 94%
     * of a physics-only tape replay (find_chunk+light_state, perf 2026-07-10). */
    const GmWorld *win_world;
    int win_ccx, win_ccz;
    long long win_gen;
    int dead, deaths, won, credits;
    int dimension;
    int portal_time, portal_cooldown;
    long long seed;
    long long tick;
    int weather_enabled;
    int mobs_enabled; /* --mobs off skips gm_mobs_tick (tape-replay parity) */
    int container; /* 0 player 2x2, 1 crafting table, 2 furnace, 3 chest */
    int container_wx, container_wy, container_wz;
    int active_furnace;
    int active_chest;
    ICStack craft_grid[9]; /* live craft matrix (container_live slot ids 36..44) */
    GmRuntimeFurnace furnaces[GM_RUNTIME_FURNACES];
    GmRuntimeChest *chests; /* growable; capacity in chests_cap */
    int chests_cap;
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
/* Tape/live equipment bridge for EntityEquipmentSlot.CHEST == Items.ELYTRA. */
void gm_runtime_set_elytra(GmRuntime *r, int equipped);
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
int gm_runtime_tape_potion(GmRuntime *r, int id, int amplifier, int duration);
void gm_runtime_apply_tape_view(const GmRuntime *r, GmPlayerView *view);
/* Seed recorded vitals at tape-replay start. */
void gm_runtime_set_vitals(GmRuntime *r, float health, int food);
int gm_runtime_set_dimension(GmRuntime *r, int dimension);
void gm_runtime_set_time(GmRuntime *r, long long world_time);
void gm_runtime_set_total_time(GmRuntime *r, long long total_time);
int gm_runtime_set_block(GmRuntime *r, int x, int y, int z, int id, int meta);
/* Snapshot initialization: canonical cell replacement with no fluid/plant
 * mutation side effects. Must run before the first replay tick. */
int gm_runtime_load_block(GmRuntime *r, int x, int y, int z, int id, int meta);
int gm_runtime_snapshot_region(GmRuntime *r, int ccx, int ccz, int radius);
int gm_runtime_load_block_dim(GmRuntime *r, int dimension, int x, int y, int z,
                              int id, int meta);
int gm_runtime_snapshot_region_dim(GmRuntime *r, int dimension,
                                   int ccx, int ccz, int radius);
int gm_runtime_set_inventory(GmRuntime *r, int slot, int item, int count, int meta);
void gm_runtime_set_weather(GmRuntime *r, int raining, int thundering,
                            int rain_time, int thunder_time);
int gm_runtime_projectile_views(const GmRuntime *r, GmEntityView *out, int max);
/* Execute one survival crafting take from inventory-backed grid slots. Empty
 * cells are -1. Returns 1 only if a recipe matched and the output fit. */
int gm_runtime_craft(GmRuntime *r, int grid_width, const int inv_slots[9]);
/* Survival use at a world block. Verifies reach and block identity before opening. */
int gm_runtime_use_block(GmRuntime *r, int wx, int wy, int wz);
int gm_runtime_furnace_insert(GmRuntime *r, int furnace_slot,
                              int inventory_slot, int amount);
int gm_runtime_furnace_extract(GmRuntime *r, int furnace_slot, int amount);

#endif
