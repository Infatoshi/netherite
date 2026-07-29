/* game/input_map.c - CrInput (present layer) -> GmAction (game intent).
 *
 * INPUT module (owner: INPUT agent). Pure translation with a tiny internal
 * static state: attack/use rising-edge detection and a wheel-cycled hotbar
 * selection. gm_input_reset() clears it; call once at startup.
 *
 * Look convention is VANILLA MC (EntityPlayerSP.turn): dyaw/dpitch are MC
 * degrees, +yaw turns RIGHT, +pitch looks DOWN. Mouse right (mouse_dx > 0)
 * -> +dyaw, mouse toward you (mouse_dy > 0) -> +dpitch (caller clamps).
 * player_ctl applies these to the MC-convention psv yaw/pitch, and
 * game/view.h converts that to the magma camera. (The pre-2026-07-10 signs
 * were negated to match a mirrored camera; both were wrong together, which
 * made look feel right but movement mirror away from the view once turned.)
 */
#include "game/input_map.h"
#include <string.h>

/* Internal edge/selection state. Only this small block persists across calls. */
static int s_prev_attack = 0;  /* attack held on the previous poll */
static int s_prev_use    = 0;  /* use held on the previous poll    */
static int s_prev_q      = 0;  /* Q (throw) held previous poll     */
static int s_prev_tab    = 0;  /* Tab held previous poll           */
static int s_sel         = 0;  /* current wheel-tracked hotbar slot 0..8 */

void gm_input_reset(void)
{
    s_prev_attack = 0;
    s_prev_use    = 0;
    s_prev_q      = 0;
    s_prev_tab    = 0;
    s_sel         = 0;
}

/* wrap v into [0,8] (9 slots). */
static int wrap9(int v)
{
    v %= 9;
    if (v < 0)
        v += 9;
    return v;
}

GmAction gm_input_map(const CrInput *in, float mouse_sens)
{
    GmAction a;
    memset(&a, 0, sizeof a);

    a.forward = (float)(in->key_w - in->key_s);
    a.strafe  = (float)(in->key_d - in->key_a);

    a.dyaw   = (float)in->mouse_dx * mouse_sens;
    a.dpitch = (float)in->mouse_dy * mouse_sens;

    /* Arrow-key look (held): the primary look control over VNC, where SDL
     * relative-mouse capture is unreliable. Right/Down arrows use the SAME sign as
     * pushing the mouse right/down (mouse_dx/dy > 0), so keyboard and mouse look agree:
     * Right arrow -> +dyaw (turn right), Down arrow -> +dpitch (look down).
     * ARROW_LOOK_DEG is degrees per poll; tune for feel. */
    {
        const float ARROW_LOOK_DEG = 3.5f;
        a.dyaw   += (float)(in->key_right - in->key_left) * ARROW_LOOK_DEG;
        a.dpitch += (float)(in->key_down  - in->key_up)   * ARROW_LOOK_DEG;
    }

    a.jump   = in->key_space ? 1 : 0;
    a.sneak  = in->key_shift ? 1 : 0;
    a.sprint = in->key_ctrl  ? 1 : 0;

    int attack = in->mouse_left  ? 1 : 0;
    int use    = in->mouse_right ? 1 : 0;
    a.attack = attack;
    a.use    = use;

    a.do_break = (attack && !s_prev_attack) ? 1 : 0;
    a.do_place = (use    && !s_prev_use)    ? 1 : 0;
    s_prev_attack = attack;
    s_prev_use    = use;

    /* Hotbar selection: number key wins; else wheel cycles; else unchanged. */
    if (in->key_num >= 1 && in->key_num <= 9) {
        s_sel = in->key_num - 1;
        a.hotbar_sel = s_sel;
        /* Shift+number: QUICK_MOVE that slot into other hotbar slots (live inventory). */
        if (in->key_shift) {
            a.inv_click  = 1;
            a.inv_slot   = s_sel;
            a.inv_button = 0;
            a.inv_type   = 1; /* CC_CLICK_QUICK_MOVE */
        }
    } else if (in->wheel != 0) {
        int step = (in->wheel > 0) ? 1 : -1;   /* sign(wheel) */
        s_sel = wrap9(s_sel - step);           /* cycle by -sign(wheel) */
        a.hotbar_sel = s_sel;
    } else if (in->key_tab && !s_prev_tab) {
        /* Tab edge steps across the hotbar (Shift+Tab steps back). */
        s_sel = wrap9(s_sel + (in->key_shift ? -1 : 1));
        a.hotbar_sel = s_sel;
    } else {
        a.hotbar_sel = -1;
    }
    s_prev_tab = in->key_tab ? 1 : 0;

    /* Q edge: THROW from the current hotbar slot (container_click THROW).
     * Vanilla: Q drops one item, Ctrl+Q drops the whole stack (button 1). */
    {
        int q = in->key_q ? 1 : 0;
        if (q && !s_prev_q) {
            a.inv_click  = 1;
            a.inv_slot   = s_sel;
            a.inv_button = in->key_ctrl ? 1 : 0;
            a.inv_type   = 4; /* CC_CLICK_THROW */
        }
        s_prev_q = q;
    }

    return a;
}
