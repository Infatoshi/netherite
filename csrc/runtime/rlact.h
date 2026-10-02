/* The binding's action compiler (lane/guiact), beside rlgui.h: a policy's
 * higher-level screen actions turned into vanilla 1.7.10 gui ops (the
 * clicks and closes a person at the client makes, Act.java's rules), one at
 * a time, at least click_interval ticks apart, queued across steps.
 *
 * The actions ride in nwrl_act.ops (up to NWRL_OPS a step, in order) beside
 * pool.h's own kinds:
 *
 *   PG_CLICK, PG_CLOSE, ...  a raw op, as without the compiler
 *   NWRL_OP_MOVE             a stack from one slot to another (move_stack):
 *                            window the from slot (NWRL_BY_ITEM: the item
 *                            mode, damage index (-1 any) picks the stack,
 *                            item_select), slot the to slot (NWRL_BY_ITEM: a
 *                            stack of the moved item with room on the other
 *                            side of the screen, else an empty slot there,
 *                            item_select), button the amount (NWRL_ALL,
 *                            NWRL_HALF, NWRL_ONE). Its clicks: pick the
 *                            stack up (left; right for half), put it down
 *                            (left; right for one), then the cursor's rest
 *                            back where it came from (what a swap left
 *                            there, or what did not fit).
 *   NWRL_OP_CRAFT            recipe index (nwrl_recipes' order) button times
 *                            (0: as many as the materials allow), from what
 *                            the open screen can make (recipes): the
 *                            crafting grid cleared of other items
 *                            (shift-clicks), each cell given its ingredient
 *                            (a stack picked up, right clicks), the result
 *                            shift-clicked; under a furnace (a furnace
 *                            recipe) its input and, if it is not burning
 *                            and has none, a fuel from the inventory.
 *
 * With every switch off and click_interval 1 the compiler is not there:
 * the ops go to the tick as before (several in one tick). Otherwise every op
 * of a step joins the env's queue; before each tick (the pool's act hook,
 * on the worker, the env's own client state) the front action gives its next
 * op once click_interval ticks have passed since the last one, planned from
 * the screen as it is then (a click's effect is seen before the next is
 * chosen). An action whose screen closed or changed, or which cannot go on
 * (no such stack, no room), is dropped (failed). The masks refuse an op
 * (dropped, counted): mask_close_grid a close while a crafting grid holds
 * items (ContainerWorkbench and ContainerPlayer drop them), mask_noop_clicks
 * a click that changes nothing (an empty slot under an empty cursor, an
 * empty result, a click outside with nothing held). */
#ifndef NETHERITE_RUNTIME_RLACT_H
#define NETHERITE_RUNTIME_RLACT_H

#include <stdint.h>

#define NWRL_OP_MOVE 16
#define NWRL_OP_CRAFT 17
#define NWRL_BY_ITEM (-2)
enum { NWRL_ALL = 0, NWRL_HALF = 1, NWRL_ONE = 2 };

/* the config's sim.actions (lanes runyaml, guiact) */
struct nwrl_actconf {
    int32_t click_interval;     /* 1 .. 100 */
    int32_t mask_close_grid, mask_noop_clicks, move_stack, item_select, recipes;   /* 0 off, 1 on */
};

struct nwrl;
/* every env's compiler (their queues emptied); 0, or -1 for a value out of
 * range. nwrl_make_ex takes the config's sim.actions first. */
int nwrl_actions(struct nwrl *h, const struct nwrl_actconf *c);
void nwrl_actions_get(const struct nwrl *h, struct nwrl_actconf *c);

#define NWRL_RMAX 512
/* An idle env's compiler and screen, as valid-action masks for a head:
 * click_ok per slot of the open screen, bit NWRL_CK_* set where the op
 * changes something (and passes the masks); crafts per recipe
 * (nwrl_recipes' order) how many times the open screen can make it now from
 * the inventory, the grid and the cursor (a crafting recipe under the
 * inventory, 2x2, or a table; a furnace recipe under a furnace). */
enum {
    NWRL_CK_CLICK = 1,          /* a left or right click (mode 0) */
    NWRL_CK_SHIFT = 2,          /* a shift-click (mode 1) */
    NWRL_CK_DROP = 4,           /* Q over it (mode 4) */
    NWRL_CK_FROM = 8,           /* a move's from: a stack to pick up */
    NWRL_CK_TO = 16,            /* a move's to: a slot that takes a stack */
};
struct nwrl_actinfo {
    int32_t queued;             /* actions waiting, the front one in progress */
    int32_t screen, nslots, window;
    int32_t close_ok;           /* a close passes mask_close_grid */
    int32_t outside_ok;         /* a click outside (-999) changes something: a stack held */
    int32_t nrecipes;
    int32_t pad;
    int64_t emitted;            /* since the reset: ops given to the tick */
    int64_t refused;            /* ops or actions refused: a mask, a switch off, a full queue */
    int64_t failed;             /* compiled actions dropped unfinished */
    int64_t done;               /* compiled actions finished */
    uint8_t click_ok[90];
    uint8_t pad2[6];
    int16_t crafts[NWRL_RMAX];
};
int nwrl_act_info(struct nwrl *h, int id, struct nwrl_actinfo *out);

/* Every tick's ops the compiler gives env id from now on, a line a tick to
 * PATH (NULL stops): {"t":T,"gui":[...]} in Act.java's tape vocabulary (the
 * gate's expanded script). 0, or -1. */
int nwrl_act_log(struct nwrl *h, int id, const char *path);

#endif
