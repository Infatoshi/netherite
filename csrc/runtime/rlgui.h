/* The binding's screens (lane/invact), beside rlbind.h: what each slot of the
 * open screen is, so a trainer can point at one, and a row record of every
 * tick an env runs, so the gui ops it sends through the binding can be held
 * to a recording's rows.
 *
 * A step's result already carries the open screen (neth_rl_result.screen, pool.h
 * enum pool_screen), its slots as the client's container has them (item,
 * count, damage; nslots of them) and the cursor's stack; a step's act carries
 * up to NETH_RL_OPS gui ops in vanilla's own vocabulary (neth_rl_op: click with
 * 1.7.10 windowClick's window, slot, button and mode; close; respawn; wake;
 * trsel). neth_rl_screen_slots names each slot's role, its index in that role
 * and where vanilla draws it (Slot.xDisplayPosition, yDisplayPosition, in the
 * screen's own pixels from its top left corner; the Container constructors of
 * oracle/src/inventory). Pure tables: the same answer for every env. */
#ifndef NETHERITE_RUNTIME_RLGUI_H
#define NETHERITE_RUNTIME_RLGUI_H

#include <stdint.h>

/* a slot's role */
enum neth_rl_role {
    NR_NONE = 0,        /* no such slot */
    NR_GRID,            /* a crafting grid cell (index row * 3 + column, the 2x2 grid in the 3x3's top left) */
    NR_RESULT,          /* the crafting result */
    NR_ARMOUR,          /* helmet 0 .. boots 3 */
    NR_INV,             /* the main inventory, 0 .. 26 (server inventory slots 9 .. 35) */
    NR_HOTBAR,          /* 0 .. 8 */
    NR_CHEST,           /* a chest's own slots, 0 .. 26 or 0 .. 53 */
    NR_FURN_IN,         /* the furnace's input */
    NR_FUEL,            /* the furnace's fuel */
    NR_FURN_OUT,        /* the furnace's output */
    NR_TILE,            /* a dispenser's or a hopper's own slots */
    NR_TRADE_IN,        /* the merchant's two payment slots */
    NR_TRADE_OUT,       /* the merchant's result */
    NR_ROLES
};

struct neth_rl_slotinfo { int8_t role, index; int16_t x, y; };

/* The slots of screen kind SCREEN (pool.h enum pool_screen) with NSLOTS
 * slots (the result's nslots: a chest's 63 or 90 say its rows) into out[0
 * .. max): how many there are, 0 for a screen with no container (none, game
 * over, sleep, chat, credits) or a slot count the kind never has. w and h,
 * if not NULL, get the screen's size in its pixels (GuiContainer xSize,
 * ySize). */
int neth_rl_screen_slots(int screen, int nslots, struct neth_rl_slotinfo *out, int max, int *w, int *h);

/* The row record (csrc/tests/rowrec.h, what test_snapshots --row-dump
 * writes) of every tick env id runs from now on, one line a tick appended
 * to PATH (NULL stops and closes it). The client world's Random is known
 * from row cw_from on (the d.cw field is "-" before it, as test_snapshots
 * writes it): neth_rl_cw_from gives it for a recording's tape. 0, or -1. */
struct nwrl;
int neth_rl_rows(struct nwrl *h, int id, const char *path, int64_t cw_from);
int64_t neth_rl_cw_from(const char *tape, int64_t t0);

/* The open container screen of env id (idle: after its poll, before its
 * next step) as a layer over the frame: GuiContainer as the oracle's client
 * draws it (csrc/engine/raster_hud.c raster_gui_live: the darkened
 * background, the screen's sheet, every slot's item and count, the
 * furnace's progress, the effect column) at the agent window's 854x480 and
 * GUI scale 2, reduced to the observation's w x h by area. A frame f gets
 * the screen as f * mul + add, each channel (mul, add: float [h][w][3]).
 * Not drawn: the mouse (the agent has none: no hover, no cursor stack),
 * GuiInventory's player preview. 1 with a layer, 0 when no container screen
 * is open, -1 for a bad id. */
int neth_rl_screen_layer(struct nwrl *h, int id, int w, int hh, float *mul, float *add);
/* neth_rl_screen_layer for idle envs ids[0..k) at once, on up to `threads`
 * threads (0: one an env, at most 16; lane/guiact: the draws share their
 * textures and block meshers, read once a process): env ids[i]'s layer at
 * mul + i * hh * w * 3 (add likewise), rc[i] its return. 0, or -1 for a bad
 * id. */
int neth_rl_screen_layers(struct nwrl *h, const int *ids, int k, int w, int hh, float *mul, float *add, int *rc,
                       int threads);

/* neth_rl_make with flags: NETH_RL_DEV takes dev starts (a recording's or a
 * scenario's start whose tape is dev:true, agent only: csrc/engine/dev.h
 * applies its header's dev ops), which neth_rl_make refuses as the product
 * does. */
#define NETH_RL_DEV 1
/* NETH_RL_SAVE: starts can be saved from the envs (rlbind.h neth_rl_start_save):
 * each env keeps the acts it ran since its reset, four saves made at once */
#define NETH_RL_SAVE 2
/* ... and NETH_RL_SAVE_SLOTS(K) (lane/chain2): K saves at once (1..32; 0 or unset: four). s42k3's 128 envs refused five
 * saves for each one made with four */
#define NETH_RL_SAVE_SLOTS(k) (((k) & 63) << 8)
/* NETH_RL_ANYSEED: starts of any world seed (pool.h any_seed): one library's
 * envs reset to the starts of several seeds (lane/iron: the 17 speedrun
 * seeds in one pool) */
#define NETH_RL_ANYSEED 4
/* opt-in training fidelity flags (zero ABI impact on neth_rl_opts; default 0 = vanilla exact) */
#define NETH_RL_SKIP_AMBIENT (1 << 16)
#define NETH_RL_DISTANT_R(r) (((r) & 255) << 17)
struct neth_rl_opts;
struct nwrl *neth_rl_make_ex(const struct neth_rl_opts *o, int flags, char *err, int en);

/* neth_rl_step with n ticks a step, whatever the config's ticks_per_step (the
 * holds kept over the n ticks, the rest on the first: Act.holdOnly), as a
 * recording's script steps are. */
struct neth_rl_act;
int neth_rl_step_ticks(struct nwrl *h, const int *ids, int k, const struct neth_rl_act *acts, int n);

#endif
