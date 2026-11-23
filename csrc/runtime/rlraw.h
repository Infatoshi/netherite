/* The raw keyboard and mouse form of the binding's acts (lane/iron): what a
 * person's hands do at the vanilla client, with no action compiler between.
 *
 * neth_rl_raw(h, 1) switches the binding's envs to it (the other compilers,
 * moveact.h and rlact.h, are then off). A step's struct neth_rl_act keeps its
 * fields, read as a keyboard and a mouse:
 *
 *   - hold: the keys and the two mouse buttons held through the step
 *     (attack is the left button, use the right one, sneak is shift);
 *   - press: keys pressed at the step's first tick (inventory E, drop Q,
 *     the hotbar's number keys; a button newly held is also pressed, as
 *     mcsr's action map does);
 *   - look (mode 2, degrees with the 0.15 mouse factor in them): the mouse's
 *     motion in the step.
 *
 * With no container screen up the act runs as the agent form (pool.h): the
 * motion turns the camera, the keys and buttons go to the game. With one up
 * (GuiContainer: the inventory, a table, a furnace, a chest) nothing turns:
 * the motion moves the screen's pointer, 1 count = 1 display pixel = 1/2 GUI
 * pixel at the agent window's 854x480 and GUI scale 2 (a count is 0.15
 * degrees at the default sensitivity), clamped to the window; a button that
 * changes state is a press or release there, and the pressed keys are typed
 * into the screen. These become the engine's own raw screen input
 * (csrc/engine/gui_input.h, the play client's and rawjudge's path):
 * GuiContainer's mouseClicked, mouseClickMove, mouseMovedOrUp and keyTyped
 * make the window clicks Java's screen would, inside the tick. A screen that
 * opens puts the pointer at the window's centre (MouseHelper's ungrab).
 * A step's mouse events in order: the buttons let go, the motion, the
 * buttons pressed.
 *
 * Per tick of a step: the step's motion, button changes and keys on its
 * first tick, the holds alone on the rest; before each tick the last
 * tick's pointer is the hover the keys read (a frame drawn between ticks).
 */
#ifndef NETHERITE_RUNTIME_RLRAW_H
#define NETHERITE_RUNTIME_RLRAW_H

#include <stdint.h>
#include <stdio.h>

#include "../engine/gui_input.h"

struct pool_act;
struct session;
struct act;

/* the agent window and its GUI scale (the tape header's w, h; options gui 2) */
enum { RLRAW_W = 854, RLRAW_H = 480, RLRAW_SCALE = 2 };

struct rlraw_env {
    struct gui_input g;         /* the screen's raw input queue and GuiContainer's fields */
    float x, y;                 /* the pointer in GUI pixels (the fraction kept between steps) */
    int up;                     /* a container screen was up at the last tick's start */
    int window;                 /* its window id */
    int buttons;                /* bit 0 the left button down, bit 1 the right, as the screen last saw them */
    int shift;                  /* shift (the sneak key) down, as last pushed */
    FILE *log;                  /* neth_rl_raw_log: each tick's clicks and closes */
    int64_t t;
};

/* every field at a reset (the pointer at the centre, no screen) */
void rlraw_reset(struct rlraw_env *r);
/* the act hook (pool.h act_hook) for raw acts: always writes out */
int rlraw_hook(struct rlraw_env *r, struct session *ss, int k, const struct pool_act *src, int hold_only,
               struct pool_act *out);
/* after the tick (pool.h act_done): the log's line */
void rlraw_done(struct rlraw_env *r, int64_t t, const struct act *a);

/* What the screen shows of the pointer, for a policy's observation: 8 ints
 * per env: the pointer x, y (GUI pixels), the screen's left and top, its
 * width and height (GuiContainer's xSize, ySize), the slot under the pointer
 * (-1 none, -999 outside the screen), 1 when a container screen is up. */
struct nwrl;
int neth_rl_raw(struct nwrl *h, int on);
int neth_rl_raw_state(struct nwrl *h, const int *ids, int k, int32_t *out);
/* each tick's clicks and closes of env id (its window clicks in Act.java's
 * gui form: ["click", window, slot, button, mode] and ["close"]) to path, a
 * JSON line a tick: {"t": T, "screen": 1 when the tick ran the screen's
 * raw input, "gui": [...]}; NULL stops */
int neth_rl_raw_log(struct nwrl *h, int id, const char *path);

/* The privileged world scan (rlscan.h) at every step's end from now on: the
 * box (radius, below and above the eye cell), the weight of a height above
 * the eye (rlscan_spec.up_weight) and the classes (cls[block
 * id]: bit c for class c); with the envs idle. neth_rl_scan_buf: the answers,
 * struct rlscan_out per env (bytes its size), written by an env's worker
 * before its result is polled. */
int neth_rl_scan(struct nwrl *h, int radius, int below, int above, int up_weight, const uint8_t *cls);
void *neth_rl_scan_buf(struct nwrl *h, int64_t *bytes);

/* The open screen's layer (rlgui.h neth_rl_screen_layer, with the raw pointer) drawn by each env's worker at its
 * step's end from now on (raw on; the envs idle): env id's at mul and add + id * h * w * 3 floats, flag[id] 1 when
 * it has one (a container screen up), written before its result is polled */
int neth_rl_layer_auto(struct nwrl *h, float **mul, float **add, int32_t **flag);

#endif
