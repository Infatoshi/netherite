/* The first half of ItemInWorldManager.activateBlockOrUseItem (Block
 * .onBlockActivated and everything it touches) and the item half it falls
 * through to when the activation refuses and a disc is held, vanilla
 * 1.7.10, against the oracle's ActivateProbe dump.
 *
 * The machine runs on the real native world (world.c): every metadata write
 * is world_set_meta or world_set_block with the vanilla flags, so the door
 * and bed neighbour pops, the tile entity removal and the liquids are the
 * world core's own, and the popped bed foot's drop goes through blockcb.c's
 * drop_item_stack with this machine's environment installed.
 *
 * The one piece of the second half that is reachable is the disc on a
 * meta-0 jukebox (ItemRecord.onItemUse) and the empty answer the base
 * Item.onItemUse gives for a glass bottle on an empty cauldron; the per-kind
 * held pools of the probe make every other placement unreachable.
 *
 * Sleep is recorded, not ported: the bed cases run the vanilla branch order
 * (the occupied search, the refusals, sleepInBedAt's checks) and the machine
 * reports what the oracle records, the player's position and sleeping flag
 * included. NOT_SAFE is unreachable (no mobs in the region) and the sleeping
 * lane owns what a sleeping player does afterwards.
 *
 * The containers: the chest's open and close run openInventory and
 * closeInventory over the real tile entities (numPlayersUsing and the block
 * events, the InventoryLargeChest pairing opens and closes both halves) and
 * the GUI itself is only the gui flag. The bottle into a full inventory
 * spills through the same entity constructor shape the jukebox uses.
 */
#ifndef NETHERITE_ACTIVATE_H
#define NETHERITE_ACTIVATE_H

#include <stdint.h>

#include "container.h"
#include "det.h"
#include "item_entity.h"
#include "jrand.h"
#include "world.h"

/* The kind indices, the oracle's kind byte. */
enum {
    ACT_DOOR_W = 0, ACT_DOOR_I = 1, ACT_TRAPDOOR = 2, ACT_GATE = 3,
    ACT_BTN_STONE = 4, ACT_BTN_WOOD = 5, ACT_LEVER = 6, ACT_BED = 7,
    ACT_CAKE = 8, ACT_NOTE = 9, ACT_JUKEBOX = 10, ACT_REP_U = 11,
    ACT_REP_P = 12, ACT_CMP_U = 13, ACT_CMP_P = 14, ACT_POT = 15,
    ACT_CAULDRON = 16, ACT_CHEST = 17, ACT_CHEST2 = 18, ACT_FURNACE = 19,
    ACT_FURNACE_LIT = 20, ACT_DISPENSER = 21, ACT_WORKBENCH = 22
};

/* One case's inputs, the oracle's cases.bin record fields. */
typedef struct {
    int kind;
    int x, y, z;             /* the clicked cell */
    int half;                /* doors, beds and chest pairs: 1 the upper/head/right */
    int side;
    float vx, vy, vz;        /* the C08 hit offsets */
    float yaw;
    double px, py, pz;       /* the player's pose */
    int sneak, day;          /* day: 0 day (isDaytime true), 1 night */
    int food_level;
    float food_sat;
    int inv_full;            /* the inventory-full filler in slots 1..35 */
    int meta;                /* the kind's meta for the single-cell kinds */
    int bed_dir, foot_occ, head_occ;
    int pot_full, pot_item, pot_data;
    int jmeta, jdisc;
    int note_pitch;
    int chest_blocked;
    int held_item, held_damage, held_count;   /* held_item 0 is the empty hand */
    int64_t opseed;          /* the case's world Random seed */
    int dummy_sleeping;      /* the occupied-bed search's sleeper at this bed */
} act_case;

/* What one case leaves behind, compared against the oracle's caseout. */
typedef struct {
    int ret, chat, gui, stat_use;
    int held_item, held_damage, held_count;
    int food_level;
    float food_sat, food_exh;
    int sleeping;
    double px, py, pz;
    int chest_num, chest_pair_num;
    int pot_item, pot_data;
    int note_pitch;
    int disc_item, disc_count;
    int cmp_out;
    uint64_t wr_state, math_state, seeder_state;
    int32_t next_id;
    int64_t tick_entry;
    int n_new_ticks;
} act_out;

/* One spawned item entity, as the oracle's ents.bin rows carry it. */
typedef struct {
    int item, damage, count, entity_id;
    double x, y, z, mx, my, mz;
    float yaw;
} act_ent;

#define ACT_MAX_ENTS 512
#define ACT_MAX_EVENTS 64

/* The block events of a case: x/y/z, block, event id and parameter. */
typedef struct {
    int32_t v[ACT_MAX_EVENTS][6];
    int n;
} act_events;

typedef struct act_env {
    struct world *w;
    det_state *det;
    jrand wr;                /* the case's world Random */

    /* the player the machine activates with */
    double pos_x, pos_y, pos_z;
    int sneak, sleeping;
    int food_level;
    float food_sat, food_exh;
    struct inv player;       /* 40 slots: 36 main, 4 armor */

    /* the chat message the case produced: 0 none, 1 occupied, 2 noSleep,
     * 3 notSafe */
    int chat;
    int stat_use;
    int can_respawn;         /* the provider's canRespawnHere */
    int ret;                 /* the call's return, set by act_run */
    int setup;               /* 1 while act_case_begin's setup runs: the
                              * pops it makes spawn without joining the
                              * case's ents record (the probe's capture is
                              * detached during the setup) */
    int case_kind;
    int case_x, case_y, case_z;
    int case_half;           /* the chest pairs: 1 when the clicked cell is
                              * the right one, so the site's left chest is
                              * case_x - 1 */

    /* the open GUI's chest positions for the close: 0 none, 1 chest single,
     * 2 chest double, 3 other container */
    int open_gui;
    int open_x, open_y, open_z;
    int open_ux, open_uz, open_px, open_pz;

    /* a live server call (act_live_block, act_live_chest_open and _close):
     * the block events join the world's queue (world_block_event), and
     * the chest's openInventory waits for displayGUIChest's closeScreen
     * (survival.c gui_open_chest) */
    int live;

    act_ent ents[ACT_MAX_ENTS];
    int nents;
    act_events events;

    /* the entity world the explosion machine and the sinks share */
    struct ie_world iew;
    int iew_call_start;      /* the iew list's index where the call's own
                              * spawns begin (the setup's pops precede) */

    /* the write listener the test installs, saved off while the setup runs */
    void (*on_write)(void *ctx, int x, int y, int z, int id, int meta);
    void *on_write_ctx;
    void (*saved_write)(void *ctx, int x, int y, int z, int id, int meta);
    void *saved_write_ctx;
} act_env;

/* An act_env from the environment's scratch for a live path (activate.c):
 * a variable declared ACT_SCRATCH gives it back at scope exit. */
#define ACT_SCRATCH_DEPTH 2
act_env *act_scratch_begin(void);
void act_scratch_end(act_env **d);
#define ACT_SCRATCH __attribute__((cleanup(act_scratch_end)))

/* det must already carry the run's OTHER-role state (seeder, entity ids).
 * The tick-entry counter continues from the manifest's start. */
void act_init(act_env *d, struct world *w, det_state *det, int64_t total_time,
              int64_t next_tick_entry);

/* The case's setup: the site's cells back to the drawn state, with the
 * write listener off (the oracle's setup writes are not recorded). The
 * entities, events and pending-tick deltas of the previous case are
 * dropped. */
void act_case_begin(act_env *d, const act_case *c);

/* The whole activateBlockOrUseItem call: 1 when the call returned true. */
int act_run(act_env *d, const act_case *c);

/* The case's records; the entities are read from the env afterwards. */
void act_case_end(act_env *d, act_out *out);

/* The live block half of a C08: what survival.c passes in. The streams are
 * the server's own: wr is the world Random whose seed the call spends and
 * whose state comes back in wr_state, det carries the SERVER role's draws
 * (blockcb_env's role for the duration), and iew is the replay's item
 * entity list (the role the spawns draw with). The food and held fields
 * come in and the mutated values go out: the cake's eat, the pot's plant
 * decrement and the cauldron's bottle fill or bucket swap move them. */
struct act_live_env {
    jrand *wr;
    det_state *det;
    struct ie_world *iew;
    int food_level;
    float food_sat;
    int held_item, held_damage, held_count;   /* held_item 0 is the empty hand */
    /* the caller's inventory: the machine seeds its own slots from it, so an
     * add lands in the right slot (the cauldron's overflow potion) */
    const int *inv_item;                      /* 40 entries, item 0 = empty slot */
    const int *inv_damage;
    const int *inv_count;
    /* an add to a slot other than the held one: the machine slot it landed
     * in and the stack, count 0 = none */
    int add_slot, add_item, add_damage, add_count;
};
struct act_live_result { int activated, gui, x, y, z, ux, uz, px, pz; };
struct act_live_result act_live_block(struct world *w, const struct act_live_env *in,
                                      struct act_live_env *out,
                                      int x, int y, int z, int side,
                                      float hx, float hy, float hz,
                                      float yaw, int sneaking);

/* TileEntityChest.openInventory on a live world: the count, the block
 * event and the two neighbour notifies (ContainerChest's constructor, after
 * displayGUIChest closed any open window; InventoryLargeChest opens the
 * upper half first). */
void act_live_chest_open(struct world *w, struct tile_entity *te);

/* TileEntityChest.closeInventory on a live world (ContainerChest's
 * onContainerClosed; InventoryLargeChest closes the upper half first). */
void act_live_chest_close(struct world *w, struct tile_entity *te);

/* BlockDragonEgg.func_150019_m on a live world: the teleport draws from the
 * caller's world Random. */
void act_live_dragon_egg(struct world *w, jrand *wr, int x, int y, int z);

#endif
