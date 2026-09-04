/* Shared per-player simulation state for Magma and Blaze. No host pointers or
 * rendering/audio state: each runtime/environment owns its own instance.
 * Snapshot readers/writers map these fields explicitly; this is not a wire ABI. */
#ifndef MC_PLAYER_CONTROL_STATE_H
#define MC_PLAYER_CONTROL_STATE_H
#include "items_core.h"
typedef struct PlayerControlState {
    /* PlayerControllerMP progressive break target and Minecraft input timers.
     * A press-miss arms leftClickCounter; held use obeys rightClickDelayTimer. */
    float dig_progress;
    int dig_hx, dig_hy, dig_hz; /* window-local; INT_MIN means no target */
    int dig_hitting, dig_delay, atk_prev, left_click_counter;
    int rc_delay, use_prev;
    int eat_ticks, eat_item;
    /* Integrated-server velocity shadow, used by damage packet inference. */
    int hurt_vel_reset;
    double server_motion_x, server_motion_z;
    ICStack cursor;
} PlayerControlState;
#endif
