/* The two players the oracle's lockstep drives: the client player the player
 * actually moves (EntityClientPlayerMP through EntityPlayerSP, EntityPlayer,
 * EntityLivingBase and Entity), and the server player the integrated server
 * validates (EntityPlayerMP through NetHandlerPlayServer.processPlayer).
 *
 * The replay drives one tick pair per tape row: the client tick applies the
 * row's act (look, the input snapshot) and runs the client player, whose
 * sendMotionUpdates produces the C03 packet; the server tick applies that
 * packet exactly as NetHandlerPlayServer does, moving its own player through
 * the same moveEntity. What the row records (cp, sp) is read from these
 * structs afterwards.
 *
 * Not ported here, each with its reason (the report carries the full list):
 *   - digging and right-click use: the recorded tapes hold attack against a
 *     wall without breaking it (their row positions prove it), so the dig
 *     packets have no world or physics effect on them;
 *   - the server world tick (weather, random and scheduled block ticks) and
 *     the other entities: nothing they do lands inside cp or sp on these
 *     tapes;
 *   - FoodStats, potions, air: no compared field depends on them (food stays
 *     20, hp stays 20, no tape drowns).
 */
#ifndef NETHERITE_PLAYER_H
#define NETHERITE_PLAYER_H

#include "survival.h"
#include "gui_chat.h"
#include "clientstate.h"

struct combat_state;
struct serverreplay;
#include "container.h"
#include "entity.h"
#include "world.h"
#include "portal.h"

/* the S0Ds one network tick can carry: every item world's entities (two
 * lists of IE_MAX_ENTITIES), each collected at most once */
#define S0D_MAX 16384

/* One binding the tape's input snapshot can name, in vanilla GameSettings
 * order for the first 16, then the nine hotbar slots. */
enum
{
    K_ATTACK, K_USE, K_FORWARD, K_LEFT, K_BACK, K_RIGHT, K_JUMP, K_SNEAK, K_SPRINT,
    K_DROP, K_INVENTORY, K_CHAT, K_PLAYERLIST, K_PICK, K_COMMAND, K_PERSPECTIVE,
    K_HOTBAR, K_N = K_HOTBAR + 9
};

const char *key_desc(int k);
/* The enum slot for a KeyBinding description, -1 when it is not one of the
 * vanilla bindings above. */
int key_lookup(const char *desc);

struct key_state
{
    uint8_t held[K_N];
    int presses[K_N];
};

/* The agent form of a tick's keys and gui ops (java Act.java's agent form:
 * what a controller sends), resolved inside the client tick where
 * Act.applyGui and Act.applyInput run: the gui ops under the screen
 * handleInput sees, the keys at the input block (act_agent.c). The env
 * pool's acts (csrc/runtime) carry one; a tape's act does not. */
enum { AG_CLICK = 1, AG_CLOSE, AG_RESPAWN, AG_WAKE, AG_TRSEL };
#define ACT_AGENT_OPS 8
struct act_agent
{
    uint8_t held[K_N];      /* the bindings held this tick */
    int presses[K_N];       /* presses added to what each binding has */
    int hb;                 /* the hotbar slot, -1 none */
    int ctrl;
    int nops;
    struct { int kind, window, slot, button, mode, index; } ops[ACT_AGENT_OPS];
    int refused;            /* out: the ops Act.applyGui refuses (dropped) */
};

/* One row's act, in tape form, already parsed off the JSON. */
struct act
{
    int has_look;
    float look[4];          /* yaw, pitch, prevYaw, prevPitch */
    int has_in;             /* the input snapshot is present */
    struct key_state keys;
    int hb;                 /* hotbar slot; -1 leaves the inventory alone */
    /* the playable client's own act: hb is resolved inside the tick, from
     * the slot the packets left plus hb_wheel notches (the tape's hb) */
    int hb_live, hb_wheel;
    /* the row's opts (GameSettings the keyboard loop changed: F5, F1, F8,
     * F3, the render distance), applied with the input snapshot; -1 unset */
    int has_opts;
    int o_tpv, o_hide, o_smooth, o_rd, o_dbg;
    int focus, lcc, ctrl;
    int rd;                 /* the row's opts rd: the render distance the
                             * client's GameSettings holds from this row on
                             * (an agent opts step), 0 when it names none */
    int gui_close;          /* the row has a ["close"] op */
    /* the row's ["close"] ops in their places: close_at[k] clicks come
     * before the k-th (GuiContainer.keyTyped's close key; the screen it
     * closed still takes the tick's later keys: another close, a hotbar key
     * or Q over the slot it hovered, clicks landing on the inventory
     * container) */
    int closes, close_at[CONTAINER_MAX_SLOTS + 2];
    int gui_respawn;        /* the row's ["respawn"] op: the C16 */
    int gui_respawn_after_close; /* the respawn follows the row's close (a dead
                             * player's container screen closes into the
                             * game-over screen, whose respawn it is) */
    int gui_wake;           /* the row's ["wake"] op: GuiSleepMP's Leave Bed
                             * button, C0BPacketEntityAction(player, 3) */
    /* The row's ordered ["click", window, slot, button, mode] ops (the
     * C0EPacketClickWindows PlayerControllerMP.windowClick sends): the
     * container clicks that open the inventory, workbench and crafting tapes
     * carry. window < 0 selects the open container, as the client's own
     * openContainer.windowId does. */
    int clicks;
    struct guiclick { int window, slot, button, mode; } gui_click[CONTAINER_MAX_SLOTS + 2];
    /* The row's ordered ["trsel", window, index] ops: the GuiMerchant arrow
     * buttons' C17 MC|TrSel (and the client screen's own index change),
     * each after the first `at` clicks of the row. */
    int trsels;
    struct { int window, index, at; } trsel[8];
    /* The row's ["chat", ...] op: the chat screen's batch (gui_chat.h) */
    int has_chat;
    struct chat_op chat;
    /* The playable client's raw screen input (gui_input.h), NULL in a tape's
     * act: the tick's gui phase runs the open container screen's handlers
     * over it, appending the clicks, trsels and close they make to this act
     * (whose other gui ops it replaces). */
    struct gui_input *gui_in;
    /* The agent form (struct act_agent), NULL in a tape's act: the tick
     * resolves it into the fields above as the oracle's agent mode does */
    struct act_agent *agent;
};

/* The agent form's two points in the client tick (act_agent.c): the gui ops
 * against the screen as handleInput finds it, and the keys when the input
 * block runs (currentScreen null, or GuiInventory over the player's own
 * container). Each writes the tape-form fields of a. */
void act_agent_gui(struct client_player *p, struct act *a);
void act_agent_input(struct client_player *p, struct act *a);

struct living;

struct client_player
{
    struct entity e;
    struct combat_state *combat;
    struct pickobj_state *pickobj;

    /* the client's own container (window 0, the GuiInventory's
     * inventoryContainer) and the workbench's, while that screen is up */
    struct container own_container;
    struct container wb_container;
    /* the row's clicks as windowClick sent them (client_out's click_sent,
     * click_ret) */
    int picked;             /* the row's mouseover is made (client_player_pick) */
    unsigned char click_sent[CONTAINER_MAX_SLOTS + 2];
    struct craft_stack click_ret[CONTAINER_MAX_SLOTS + 2];
    /* why the row's click is one the vanilla client never makes (empty:
     * none); session_row fails the row on it, as the oracle's replay does */
    char refused[96];

    float rotation_yaw, rotation_pitch;
    float prev_rotation_yaw, prev_rotation_pitch;

    /* MovementInput (MovementInputFromOptions) after the last update, and the
     * KeyBinding states the input snapshot pins. */
    struct key_state keys;
    float in_strafe, in_forward;
    int in_jump, in_sneak;

    /* EntityLivingBase. */
    int is_jumping, jump_ticks;
    float jump_movement_factor;

    /* Entity (datawatcher flag 3 through EntityLivingBase.setSprinting). */
    int sprinting;

    /* EntityPlayerSP. */
    int sprint_toggle_timer, sprinting_ticks_left, fly_toggle_timer;
    /* timeInPortal and its previous value, Entity.inPortal and
     * timeUntilPortal: the portal overlay's timer (clientstate.c), set from
     * the client's own moveEntity through e.set_in_portal */
    struct cs_portal portal;

    /* EntityRenderer.updateRenderer, which runTick calls after the input
     * loops and before WorldClient.updateEntities (not while paused): the
     * playable client's FOV, fog brightness, torch flicker and equip
     * animation, from the player as the living update has not yet moved
     * it. NULL in the replay gates, which draw no frames. */
    void (*update_renderer)(struct client_player *p);

    /* EntityClientPlayerMP move-packet bookkeeping. */
    double old_pos_x, old_min_y, old_pos_y, old_pos_z;
    float old_rotation_yaw, old_rotation_pitch;
    int was_on_ground, should_stop_sneaking, was_sneaking, ticks_since_move_packet;

    /* state the tick reads and the snapshot carries or pins */
    float food_level;
    int allow_flying, is_flying;
    int in_game_has_focus;
    int input_ran;          /* the last tick's input block ran: the act's in snapshot took effect */
    int screen_inventory;   /* a GuiInventory is open */
    /* the open screen is a GuiInventory over own_container while
     * open_container (EntityPlayer.openContainer) is still another window:
     * a screen closed without closeScreen (EntityPlayerSP's portal close)
     * leaves openContainer as it was, and the inventory key then opens
     * GuiInventory over inventoryContainer */
    int gui_own;
    int screen_gameover;    /* a GuiGameOver is open (cp.gui in the rows) */
    int screen_sleep;       /* a GuiSleepMP is open */
    int screen_credits;     /* a GuiWinGame is open (after the End's exit portal) */
    /* GuiWinGame.field_146581_h, its updateScreen clock, and the clock the
     * roll ends past: (lines * 12 + height + height + 24) / 0.5; 0 takes the
     * oracle's (569 lines of end.txt and credits.txt for Player, a 240-pixel
     * scaled screen: 14664), the playable client sets its window's */
    int credits_clock;
    float credits_end;
    int screen_chat;        /* a GuiChat is open (key.chat, key.command): its
                               allowUserInput is false; the ["chat"] op's
                               Enter and Escape close it */
    /* the chat screen (GuiChat, GuiSleepMP) and GuiNewChat's sent history,
     * the clipboard, what the tick's batch sent and what it did to the
     * drawn chat (gui_chat.h); chat_unmodelled: the batch reached what is
     * not modelled (the history past its kept GC_HIST) */
    struct gui_chat chat;
    struct chat_sent chat_sent;
    struct chat_clip chat_clip;
    struct chat_out chat_out;
    struct chat_fx chat_fx;
    int chat_unmodelled;
    int game_paused;        /* Minecraft.isGamePaused as the last frame left it:
                               a pausing screen (GuiWinGame) was open, so this
                               tick's runTick and the integrated server's tick
                               skip the world, the player and the packets */
    int left_click_counter;
    int hotbar, ctrl;
    int ticks_existed, first_update;
    int client_tick_index;
    /* the replayed row's tick (the tape's t), the agent-mode getSystemTime
     * base: Minecraft.getSystemTime in agent mode is Oracle.tick * 50, and
     * the client's GuiAchievement clock stores that. Set by the drivers
     * (play.c, test_snapshots.c) before client_player_tick. */
    int64_t client_row_tick;
    int is_dead;            /* set past posY < -64; no tape reaches the void */
    int first_client_tick;  /* the first replayed tick does not run: the
                               client world still holds no chunks (the
                               snapshots are taken at the first ready tick) */
    int right_click_delay;  /* PlayerControllerMP.rightClickDelayTimer */
    /* Entity.ridingEntity: the client copy of the vehicle (an S1B for the
     * player itself), 0 none; the moveStrafing and moveForward the living
     * update left, which the rider's C0C carries */
    int riding_id;
    float move_strafing, move_forward;
    /* the S02 lines the last pump took (S02_ keys, survival.h), for the
     * live client's GuiNewChat, with the chat screen's own lines (the tab
     * completion's list, id 1) where it printed them */
    int s02_n;
    /* each line's parts (formatting code and text pairs, NUL-terminated,
     * chatcomp.h's S02 side part), their count and the line's id */
    char s02_text[8][1536];
    int s02_nparts[8];
    int s02_id[8];
    int synced_item;        /* PlayerControllerMP.currentPlayerItem */

    /* Entity.dimension, and NetHandlerPlayClient.handleRespawn's new
     * WorldClient: an S07 naming another dimension asks the owner of the
     * client world for an empty one (on_world_change sets e.world); the
     * chunks arrive later, as the server sends them */
    int dimension;
    void (*on_world_change)(void *ctx, int dim);
    void *world_change_ctx;

    /* the s2c S08 from the last server tick, applied at the next
     * updateController */
    int pending_s08;
    int pending_confirm, confirm_og;
    struct c03 confirm_pkt[S08_CONFIRM_MAX];  /* the C06s handlePlayerPosLook built, one per S08 */
    int pending_confirm2, confirm2_og;
    struct c03 confirm2_pkt;    /* the pump's second S08 this row */
    double s08_c, s08_d, s08_e;
    float s08_yaw, s08_pitch;

    /* the movementSpeed attribute value, and the modifiers it is computed
     * from (player_move_speed): the sprint boost, which setSprinting applies
     * and removes, and the slowness and speed potions' modifiers, which only
     * an S20 brings (their amplifiers, -1 when absent) */
    double move_speed;
    int mod_sprint, mod_slow, mod_speed;

    /* an S20 entity-properties packet the server's tracker queued; the client
     * applies it at the start of its next tick (processReceivedPackets):
     * handleEntityProperties sets the base and replaces every modifier with
     * the packet's, the server's own set (func_111122_c: the sprint boost
     * when the server player sprints, the potions' modifiers). An S20 thus
     * strips the boost while the client's sprinting flag stays on. */
    int pending_s20;
    int pending_s20_mod;
    int pending_s20_slow, pending_s20_speed;

    /* WorldClient's own clock: its tick adds one to the total and, while its
     * doDaylightCycle (the S03's sign) holds, to the day time; only an S03
     * (every 20th tick, and a respawn or a dimension change) resets it, so
     * it runs a tick behind the overworld's and a sleep or a time change
     * reaches it at the next S03. The renderer's sky and light read it. */
    int64_t cw_total, cw_day;
    int cw_daylight;
    /* WorldClient.rand and World.updateLCG, the row's d.cw (the client
     * never steps the LCG): the joined world's from the recording's rows
     * (client_player_set_world_rand, cwrand.h), a new WorldClient's from its
     * two seeder longs; unknown (cw_rand_known 0) until one of them */
    det_rng cw_rand;
    int32_t cw_lcg;
    int cw_rand_known;
    /* WorldClient.setActivePlayerChunksAndCheckLight's cell this tick (from
     * cw_rand; session.c relights it), and whether a tick of this client
     * world drew one while cw_rand was unknown (its light may then differ
     * from the oracle's: play draws the server's world) */
    int cw_check_pending, cw_check_x, cw_check_y, cw_check_z;
    int cw_check_missed;
    /* EntityRenderer.rendererUpdateCount, one per unpaused updateRenderer
     * since the client started: addRainParticles seeds its Random with it */
    int64_t renderer_update_count;
    /* WorldClient's rainingStrength, the last S2B 7's (set by the driver
     * from the server world before the client tick) */
    float cw_rain;
    /* ItemStack.animationsToGo of the client's 36 main slots: 5 when an
     * S2F raises a hotbar slot's count (handleSetSlot), one less each
     * EntityPlayer.onLivingUpdate (decrementAnimations); GuiIngame's hotbar
     * squashes the item while it is over the partial tick. Kept by slot:
     * a stack the client's own click moves within those five ticks leaves
     * its count behind. */
    int8_t inv_anim[36];
    /* the last row's S03, read at this tick's packets */
    int s03_pending, s03_rule;
    int64_t s03_total, s03_day;
    /* the entities this tick's C02 INTERACTs named: the client ran their
     * interactFirst itself (a lead taken or let go shows at once) */
    int use_ids[8], nuse;

    /* the survival state (the s2c queue the pump reads is s2c_in()) */
    struct surv_state sv;

    /* The open container, in the client's own player the way the server's
     * openContainer is the truth the clicks apply to: NULL while only the
     * inventory screen is up (the GuiInventory's ContainerPlayer is its
     * inventoryContainer). The container.c clicks mutate both sides'
     * inventories; the server player owns the server's copy and the client
     * mirrors it by the S2F slots detectAndSendChanges queues. */
    struct container *open_container;

    /* Render-only client state no row carries: EntityLivingBase's arm swing
     * (swingItem, updateArmSwingProgress) and EntityPlayerSP's lagging arm
     * angles (renderArmPitch/Yaw), which the first-person hand reads. */
    int swing_in_progress, swing_int;
    float swing_progress, prev_swing_progress;
    float arm_pitch, arm_yaw, prev_arm_pitch, prev_arm_yaw;
    int equip_reset;        /* ItemRenderer.resetEquippedProgress requested */
    /* EntityLivingBase's body turn, head yaw and limbs on the client player
     * (func_110146_f, the oldAi branch's rotationYawHead, the end of
     * moveEntityWithHeading): what RenderPlayer draws in third person */
    float render_yaw_offset, prev_render_yaw_offset;
    float yaw_head, prev_yaw_head;
    float limb_swing, limb_swing_amount, prev_limb_swing_amount;
    /* GameSettings.thirdPersonView, hideGUI, smoothCamera, showDebugInfo and
     * renderDistanceChunks as the rows' opts set them (0 view, -1 unset rd) */
    int opt_tpv, opt_hide, opt_smooth, opt_dbg, opt_rd;
};

void cp_living_tail(struct client_player *p);

/* The container the open screen shows (GuiContainer.inventorySlots): the
 * player's own under a GuiInventory opened while openContainer was left
 * pointing at another window (gui_own), else openContainer. */
static inline struct container *cp_gui_container(struct client_player *p)
{
    return p->gui_own ? &p->own_container : p->open_container;
}

struct server_player
{
    struct entity e;
    struct serverreplay *replay;
    /* MinecraftServer.getTickCounter, the statistics file's 300-tick clock */
    int server_tick;
    /* the rows the integrated server stood paused (the credits screen):
     * getTickCounter did not move, so the statistics clock is server_tick -
     * paused_ticks */
    int paused_ticks;

    float rotation_yaw, rotation_pitch;
    float prev_rotation_yaw, prev_rotation_pitch;

    int sprinting, sneaking;

    /* EntityLivingBase, server side: jumpMovementFactor and jumpTicks */
    float jump_movement_factor;
    int jump_ticks;

    /* NetHandlerPlayServer bookkeeping. */
    int has_moved;
    double last_pos_x, last_pos_y, last_pos_z;
    int floating_tick_count;
    /* the handler's networkTickCount, incremented once per network tick (the
     * end of every server tick). The % 20 stationary keepalive correction
     * fires on its multiples. Seeded from the snapshot's player_server.nbt
     * "networkTickCount" tag when the snapshot has one; -1 when the snapshot
     * predates its "net" record. */
    int network_tick_count;

    /* s2c queue out of processPlayer: an S08 correction. */
    int sent_s08;
    /* NetHandlerPlayServer.chatSpamThresholdCount, and what the chat's
     * server half reached that is not modelled (chatcmd.h), NULL none */
    int chat_spam;
    const char *chat_unmodelled;
    double s08_c, s08_d, s08_e;
    float s08_yaw, s08_pitch;

    double move_speed;

    /* the server half of the S20 path: Entities track themselves, and the
     * tracker's per-tick pass (during the world tick, before the network tick
     * processes this row's packets) sends an S20 whenever the attribute map
     * has a dirty instance -- which on these tapes is exactly a C0B sprint
     * packet, because setSprinting applies or removes the (unsaved) speed
     * modifier. The queued packet carries the flag as of the pass. */
    int attr_dirty;
    int s20_queued;
    int s20_mod;
    int s20_slow, s20_speed;       /* the packet's potion modifiers (amplifiers, -1 absent) */
    int pot_slow, pot_speed;       /* the movementSpeed potion modifiers now (the twin's effects) */
    /* the player's own EntityTrackerEntry (updateFrequency 2): its ticks
     * counter, and the data watcher values the last func_111190_b pass saw,
     * whose change (DataWatcher.hasChanges) also runs the pass on an odd
     * tick: flags, air, health, potion colour and ambience, arrows,
     * absorption, score */
    int trk_ticks;
    int trk_w_valid;
    uint32_t trk_w[8];
    /* the S03 time update sent this tick, the last one wins: the total, the
     * day time and the doDaylightCycle rule it carries */
    int s03_sent, s03_rule;
    int64_t s03_total, s03_day;
    /* EntityPlayerMP.onItemPickup's S0Ds this network tick: the collected
     * item, orb or arrow ids, in pickup order */
    int s0d_ids[S0D_MAX], ns0d;
    /* Entity.ridingEntity (the saddled pig, riding.c) and the C0C inputs
     * EntityPlayerMP.setEntityActionState stores while riding */
    uint64_t ridingh;             /* the vehicle (living.h lref), 0 none */
    float move_strafing, move_forward;
    int is_jumping;
int dimension;                  /* Entity.dimension: 0 the overworld on every tape */
    int conquered_end;              /* EntityPlayerMP.playerConqueredTheEnd */
    int limbo;                      /* dead and moved by a portal: transferEntityToWorld put it in
                                     * no world's playerEntities (until the respawn) */
    int offworld;                   /* transferred out of the End: in the destination's player manager only */
    /* the EntityPlayerMP objects respawnPlayer replaced: life counts them
     * (0 the snapshot's), life_dim[k & 7] is the world the object of life k
     * stayed in (its worldObj: the respawn moves only the new one) */
    int life;
    int life_dim[8];
    int pertinent_update;           /* this row's processPlayer reached serverUpdateMountedMovingPlayer */
    struct portal_entity_state portal;
    void (*on_entity_update)(void *ctx); /* EntityPlayerMP.onUpdateEntity in processPlayer */
    void *on_entity_ctx;

    /* the survival state, the packets queued for the client this tick, and
     * the entity list the drops and pickups live on. The NBT the entity digest
     * hashes -- health, air, the hurt timers and the food stats -- lives in
     * surv_state; ent_nbt_player reads it there. */
    struct surv_state sv;
    int held_attr_item; /* held stack when EntityPlayerMP updates equipment */
    /* the s2c queue the tick writes is s2c_out() (survival.h) */
    int dev_prequeued;
    int c08_full_sync;      /* the block C08 called sendContainerToPlayer (the cauldron bottle) */
    int net_phase;                  /* inside server_player_tick: the s2c queue is open */
    int dev_s1c_index;              /* a dev state op's S1C in s2c, +1 (0 none): its health is the tracker pass's */
    struct ie_world *iew;

    /* The snapshot's own player_server.nbt tree: constant fields such as the
     * abilities and UUIDs are read from it at write time. Owned by the snapshot. */
    const void *snapshot_tree;

    /* The server's open container: the truth every C0E click applies to and
     * the S2F mirror's source (EntityPlayerMP.openContainer). The
     * workbench's own container lives here; the player's own (the default
     * openContainer) is owned by the tape driver. */
    struct container *open_container;
    struct container wb_container;
    struct container own_container;   /* the player's own, window 0 */
    int window_id;              /* EntityPlayerMP.currentWindowId */
    int gui_x, gui_y, gui_z;    /* the open tile window's (or workbench's) position */
    struct world *gui_world;    /* the world it was opened in (its container's worldObj, the tile's own) */
    int gui_pair_x, gui_pair_z; /* partner of a double chest, when present */
    int gui_has_pair;
    int gui_ender;              /* the open chest window is the player's ender
                                 * inventory (InventoryEnderChest): the chest
                                 * slots read and write sv.ender, no TE */
    /* InventoryEnderChest.associatedChest: set by BlockEnderChest's
     * func_146031_a before displayGUIChest (whose closeScreen of an open
     * ender window reaches it), cleared by closeInventory */
    int ender_assoc;
    int ender_ax, ender_ay, ender_az;
    /* the tile entities the open tile window holds (a double chest's second
     * half in gui_pair_te): canInteractWith's getTileEntity(x, y, z) == this.
     * Compared as pointers only, never read through. */
    const struct tile_entity *gui_te, *gui_pair_te;
    struct { int item, count, damage, tag; } gui_sent[54];
                                /* the tile slots' last sent stacks
                                 * (Container.inventoryItemStacks) */
};


/* Fill from the snapshot's canonical player files (the parsed nbt trees).
 * 0 on a malformed tree. */
int client_player_load(struct client_player *p, const void *tree, struct world *w);
int server_player_load(struct server_player *p, const void *tree, struct world *w);
/* The client player's Entity.addedToChunk in a snapshot that records its
 * runtime (World.updateEntityWithOptionalForce runs onUpdate only once the
 * entity sits in a chunk list): 1 or 0, -1 when the snapshot has none. */
int client_player_added_to_chunk(const void *tree);

/* One client tick: the S08 from the last server tick lands first
 * (updateController), then the act's look and input snapshot, then the parts
 * of runTick and WorldClient.updateEntities that touch the player. Produces
 * the C03 packet (out set, *has_out 1) and the C0B actions that ride ahead of
 * it in c0b[0..*nc0b). */
void client_player_tick(struct client_player *p, const struct act *a, struct client_out *out);
/* WorldClient.rand and updateLCG: the recording's (cwrand.h), or a new
 * WorldClient's two seeder longs */
void client_player_set_world_rand(struct client_player *p, uint64_t seed, int32_t lcg);
void client_player_new_world_rand(struct client_player *p);

/* Minecraft.runTick's "pick" section, getMouseOver: the row's look, then
 * the mouseover over the client's world and entities as the last tick left
 * them, before updateController hands the tick its packets. */
void client_player_pick(struct client_player *p, const struct act *a);
/* EntityPlayerSP.setSprinting on the client player (the attribute and the
 * 600-tick timer). */
void player_client_set_sprinting(struct client_player *p, int v);

/* ModifiableAttributeInstance.computeValue over the player's movementSpeed:
 * the base 0.1F widened, then the operation-2 modifiers present (SPRINT the
 * boost, SLOW and SPEED the potion amplifiers or -1) in their HashSet order. */
double player_move_speed(int sprint, int slow, int speed);

/* EntityLivingBase.swingItem on the client player (the C0A itself is counted
 * by the callers); six ticks, no haste or mining fatigue on the client. */
void client_swing_item(struct client_player *p);

/* One server tick: the C0B actions in arrival order, then the C03
 * (NetworkManager order), then processPlayer's move and checks. act carries
 * the row's window clicks (may be NULL). */
void server_player_tick(struct server_player *p, const struct client_out *co, const struct act *act,
                        int server_tick);

/* processPlayer for the row's C06 acknowledgements of the S08s its client
 * pump handled. updateController pumps before the screens and the input run,
 * so they reach the network tick ahead of the row's C16, C07, C08 and C02
 * (a use right after a teleport finds hasMoved already true). */
void server_player_process_confirms(struct server_player *p, const struct client_out *co);
/* The out queue's S1Cs read the player's watched values as the server tick
 * left them (the integrated server's packets are objects). */
void server_player_watch_live(struct server_player *p);

/* Entity.setRotation: yaw % 360, pitch % 360, Java's float remainder. */
void entity_set_rotation(float *yaw, float *pitch, float y, float p);

/* Entity.setPositionAndRotation over a player's rotation fields. */
void entity_set_pos_rot(struct entity *e, double x, double y, double z,
                        float *yaw, float *pitch, float *prev_yaw, float *prev_pitch,
                        float ny, float np);

/* Entity.handleWaterMovement for a player. */
int handle_water_movement(struct world *w, struct aabb bb, double *mx, double *my, double *mz,
                          uint8_t *in_water, float *fall_distance);
/* the server player's, with the water entry's splash draws on its Random */
int sp_handle_water_movement(struct entity *e, det_rng *rand);

/* Negative-check switches, set by the harness before the replay: pin the
 * sprint speed boost off, or every ground slipperiness at 1 (the friction
 * multiplier stays 0.91 either way). They are nw_env->cfg.player_no_sprint_boost
 * and player_no_ground_friction (env.h). */

/* The C0BPacketEntityAction action numbers. */
enum { C0B_SNEAK_ON = 1, C0B_SNEAK_OFF = 2, C0B_SPRINT_ON = 4, C0B_SPRINT_OFF = 5 };

#endif
