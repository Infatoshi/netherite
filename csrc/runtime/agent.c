/* The agent form (agent.h): Act.parseAgent's refusals and Act.applyLook
 * here, before the tick (the oracle's preTick); the keys and gui ops go to
 * the tick in struct act_agent (csrc/engine/act_agent.c), which resolves them
 * where Act.applyGui and Act.applyInput run. */
#include "agent.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../engine/clientworld.h"
#include "../engine/combat.h"
#include "../engine/player.h"
#include "../engine/session.h"
#include "pool.h"

/* enum pool_key to the engine's bindings (player.h K_*) */
static const int KEYMAP[PK_KEYS] = {
    [PK_FORWARD] = K_FORWARD, [PK_BACK] = K_BACK, [PK_LEFT] = K_LEFT, [PK_RIGHT] = K_RIGHT, [PK_JUMP] = K_JUMP,
    [PK_SNEAK] = K_SNEAK, [PK_SPRINT] = K_SPRINT, [PK_ATTACK] = K_ATTACK, [PK_USE] = K_USE, [PK_DROP] = K_DROP,
    [PK_INVENTORY] = K_INVENTORY, [PK_PICK] = K_PICK,
    [PK_HOTBAR1] = K_HOTBAR, [PK_HOTBAR1 + 1] = K_HOTBAR + 1, [PK_HOTBAR1 + 2] = K_HOTBAR + 2,
    [PK_HOTBAR1 + 3] = K_HOTBAR + 3, [PK_HOTBAR1 + 4] = K_HOTBAR + 4, [PK_HOTBAR1 + 5] = K_HOTBAR + 5,
    [PK_HOTBAR1 + 6] = K_HOTBAR + 6, [PK_HOTBAR1 + 7] = K_HOTBAR + 7, [PK_HOTBAR9] = K_HOTBAR + 8,
};

void agent_begin(struct agent_state *ag)
{
    memset(ag, 0, sizeof *ag);
}

static int pitch_ok(float f)
{
    return f >= -90.0F && f <= 90.0F;
}

int agent_act(struct agent_state *ag, struct session *ss, const struct pool_act *src, int hold_only, int rd,
              struct act *a, char *err, size_t n)
{
    struct client_player *cp = &ss->cp;
    struct act_agent *x = &ag->agent;

    memset(a, 0, sizeof *a);
    a->hb = -1;
    a->o_tpv = a->o_hide = a->o_smooth = a->o_rd = a->o_dbg = -1;
    memset(x, 0, sizeof *x);
    x->hb = -1;

    /* Act.parseAgent: a hotbar outside 0..8 or a look pitch outside -90..90
     * refuses the step whole (no tick) */
    if (!hold_only)
    {
        if (src->hotbar != -1 && (src->hotbar < 0 || src->hotbar > 8))
        {
            snprintf(err, n, "hotbar %d outside 0..8", src->hotbar);
            ag->refused_step = 1;
            return 0;
        }
        if (src->look_mode == PL_LOOK && (!pitch_ok(src->look[1]) || (src->look_prev && !pitch_ok(src->look[3]))))
        {
            snprintf(err, n, "pitch %g outside -90..90", (double)(pitch_ok(src->look[1]) ? src->look[3] : src->look[1]));
            ag->refused_step = 1;
            return 0;
        }
    }

    /* Act.applyLook (preTick): the resolved absolute look is what the tape
     * keeps, the current one when the act has none */
    float yaw = cp->rotation_yaw, pitch = cp->rotation_pitch;
    float pyaw = cp->prev_rotation_yaw, ppitch = cp->prev_rotation_pitch;
    if (!hold_only && src->look_mode == PL_LOOK)
    {
        yaw = src->look[0];
        pitch = src->look[1];
        if (src->look_prev)
        {
            pyaw = src->look[2];
            ppitch = src->look[3];
        }
    }
    else if (!hold_only && src->look_mode == PL_DLOOK)
    {
        /* Entity.setAngles with the 0.15 factor already applied */
        float py = pitch, yy = yaw;
        yaw = yy + src->look[0];
        pitch = py + src->look[1];
        if (pitch < -90.0F) pitch = -90.0F;
        if (pitch > 90.0F) pitch = 90.0F;
        ppitch += pitch - py;
        pyaw += yaw - yy;
    }
    a->has_look = 1;
    a->look[0] = yaw;
    a->look[1] = pitch;
    a->look[2] = pyaw;
    a->look[3] = ppitch;

    /* the keys and ops for the tick (Act.holdOnly on a step's later ticks:
     * the holds stay, nothing else repeats) */
    for (int k = 0; k < PK_KEYS; ++k)
    {
        x->held[KEYMAP[k]] = (uint8_t)((src->hold >> k) & 1);
        if (!hold_only) x->presses[KEYMAP[k]] = src->press[k];
    }
    if (!hold_only)
    {
        x->hb = src->hotbar;
        x->ctrl = src->ctrl;
        for (int i = 0; i < src->nops && i < POOL_GUI_OPS && i < ACT_AGENT_OPS; ++i)
        {
            const struct pool_gui_op *o = &src->ops[i];
            x->ops[x->nops].kind = o->kind == PG_CLICK ? AG_CLICK : o->kind == PG_CLOSE ? AG_CLOSE
                                 : o->kind == PG_RESPAWN ? AG_RESPAWN : o->kind == PG_WAKE ? AG_WAKE
                                 : o->kind == PG_TRSEL ? AG_TRSEL : 0;
            x->ops[x->nops].window = o->window;
            x->ops[x->nops].slot = o->slot;
            x->ops[x->nops].button = o->button;
            x->ops[x->nops].mode = o->mode;
            x->ops[x->nops].index = o->index;
            ++x->nops;
        }
    }
    /* Act.applyOpts: the render distance the observation is drawn at */
    if (!hold_only && rd > 0 && session_client_rd(ss) != rd)
    {
        a->has_opts = 1;
        a->o_rd = rd;
        a->rd = rd;
    }
    a->agent = x;
    return 1;
}

void agent_end_tick(struct agent_state *ag)
{
    ag->ops_refused += ag->agent.refused;
    ag->agent.refused = 0;
}
