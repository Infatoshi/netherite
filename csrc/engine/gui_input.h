/* The playable client's container screens as GuiContainer handles its input:
 * the raw mouse and keyboard events a player makes become the window clicks
 * ("click" window slot button mode) Java's own screen would send.
 *
 * Java handles a screen's input inside the client tick (Minecraft.runTick's
 * currentScreen.handleInput, after updateController has applied the tick's
 * packets): every queued Mouse event first (GuiScreen.handleMouseInput:
 * mouseClicked on a press, mouseMovedOrUp on a release, mouseClickMove on a
 * motion while a button is held), then every Keyboard event (keyTyped on a
 * press). Each click PlayerControllerMP.windowClick sends is predicted on
 * the client's container at once, so the next event already sees the cursor
 * and the slots it left. The hovered slot the keys act on is the one the
 * last drawn frame found under the pointer (drawScreen's field_147006_u),
 * and the shift and control keys the clicks read are Keyboard.isKeyDown,
 * the state the Display.update that delivered the tick's events polled:
 * after every event queued before the tick (a control pressed in the frame
 * of a Q is down for it, a shift released in a click's frame is up). Minecraft.getSystemTime
 * is the tick's time (tick * 50 ms, as the oracle's agent mode pins it), so
 * the double-click window is five ticks.
 *
 * The client queues its events here between ticks (gui_input_push) and
 * marks each drawn frame (gui_input_frame); act.gui_in points at the queue
 * for the tick, and the client tick's gui phase runs gui_input_run in place
 * of the act's own clicks: each click it makes is appended to the act (the
 * server half and the tape read them there) and predicted at once; a close
 * key ends the run, and the keys after it go through gui_input_closed once
 * the client has closed the screen. The
 * screen's own fields (the drag, the double click, the ignored release) live
 * here between ticks; gui_input_reset is a new GuiContainer. */
#ifndef NETHERITE_GUI_INPUT_H
#define NETHERITE_GUI_INPUT_H

#include <stdint.h>

#include "container.h"
#include "gui_screens.h"

struct act;
struct client_player;
struct client_out;

/* LWJGL key codes the container screens read */
enum { GK_ESCAPE = 1, GK_1 = 2, GK_9 = 10, GK_Q = 16, GK_E = 18, GK_LCONTROL = 29,
       GK_LSHIFT = 42, GK_RSHIFT = 54, GK_RCONTROL = 157, GK_LMETA = 219, GK_RMETA = 220 };

enum gui_event_kind { GE_PRESS, GE_RELEASE, GE_MOVE, GE_KEY };

/* One raw event in GuiScreen's scaled coordinates: a button press or
 * release (button 0 left, 1 right, 2 middle), a pointer motion, or a key
 * press (key an LWJGL code; releases and repeats never reach keyTyped). */
struct gui_event {
    int kind, x, y, button, key;
};

#define GUI_INPUT_MAX_EVENTS 1024

struct gui_input {
    struct gui_event ev[GUI_INPUT_MAX_EVENTS];
    int n;
    /* the pointer and the modifiers after the last pushed event (the
     * modifiers are Keyboard.isKeyDown for the tick's clicks and keys), and
     * the pointer as the last drawn frame saw it (the hover) */
    int cur_x, cur_y, cur_shift, cur_ctrl;
    int frame_x, frame_y;
    /* a frame drew the open screen (a new GuiContainer has no hovered slot
     * until its first drawScreen: gui_input_opened clears it) */
    int frame_valid;
    /* the screen size and GUI scale option the layout is computed from */
    int width, height, gui_scale;
    /* GuiScreen's eventButton and lastMouseEvent */
    int event_button;
    int64_t last_mouse_event;
    /* GuiContainer's own fields */
    int dragging;               /* field_147007_t */
    int drag_button;            /* field_146988_G: the button that began it */
    int drag_mode;              /* field_146987_F: 0 split evenly, 1 one each */
    int drag[CONTAINER_MAX_SLOTS];   /* field_147008_s, in the order added */
    int ndrag;
    int ignore_release;         /* field_146995_H */
    int last_slot;              /* field_146998_K (-1: null) */
    int64_t last_time;          /* field_146997_J */
    int last_button;            /* field_146992_L */
    int double_click;           /* field_146993_M */
    int shift_have;             /* field_146994_N: the stack a shift click took */
    struct craft_stack shift_stack;
    /* the screen these fields belong to: a new one starts fresh */
    const struct container *screen;
    int screen_window;
    /* what the last run did, for the frame: the ops it could not fit */
    int dropped;
    /* the tick's keys after a close key, which the closed screen still
     * takes (gui_input_closed): the window it had, the slot it hovered,
     * whether that was the inventory's own container, and (another
     * container, freed with the close) whether the slot held a stack */
    int after_keys[64], nafter;
    int after_window, after_hovered, after_own, after_has;
    /* a recorded session's Minecraft.getSystemTime for this tick (the
     * play-mode client's wall clock at its screen input, csrc/play/
     * rawjudge.sh), used in place of the tick's time when has_clock is set */
    int has_clock;
    int64_t clock;
};

/* A new GuiContainer: every screen field at its initial value (the release
 * ignored until the first press), the queue kept. */
void gui_input_reset(struct gui_input *g);

/* An event, in arrival order; the modifiers follow the key events. */
void gui_input_push(struct gui_input *g, const struct gui_event *e);
/* A modifier key's state change (shift or control down or up). */
void gui_input_modifier(struct gui_input *g, int key, int down);

/* A frame was drawn: the hover the next tick's keys read. */
void gui_input_frame(struct gui_input *g);
/* A container screen opened: no frame has drawn it yet. */
void gui_input_opened(struct gui_input *g);

/* The layout the screen of container C has (GuiContainer.initGui, the
 * effect column's shift for the inventory). */
struct gui_screen_layout gui_input_layout(const struct gui_input *g, const struct container *c, int has_effects);

/* The client tick's gui phase: the queued events through the open
 * container screen's handlers, each click appended to A and predicted on the
 * client (surv_client_click); a close key sets A's gui_close and ends the
 * tick's events. NOW is Minecraft.getSystemTime. The queue empties. */
void gui_input_run(struct gui_input *g, struct client_player *p, struct act *a,
                   struct client_out *out, int64_t now);

/* After the client closed the screen a close key closed in the tick: the
 * keys that followed it through the closed GuiContainer's keyTyped (a close
 * key closes again: another ["close"]; a hotbar key or Q over the slot it
 * hovered clicks the old window's slot, predicted on the inventory
 * container, the client's openContainer now). */
void gui_input_closed(struct gui_input *g, struct client_player *p, struct act *a, struct client_out *out);

/* A frame drew the open container screen (GuiContainer.drawScreen's
 * func_146977_a per slot): a held drag's slot that stopped taking the
 * cursor (the player's own slot filled by a pickup) leaves the set. */
void gui_input_drawn(struct gui_input *g, struct client_player *p);

/* The drag the screen is showing: 1 while a drag holds slots (GuiContainer
 * draws them and the cursor's remainder), with the slots and the mode. */
int gui_input_drag(const struct gui_input *g, const int **slots, int *n, int *mode);

#endif
