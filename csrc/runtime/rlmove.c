/* The binding's action compiler (rlmove.h). */
#include "rlmove.h"

#include <stdio.h>
#include <string.h>

#include "../engine/worldconf.h"
#include "moveact.h"
#include "rlbind.h"

_Static_assert(NWRL_MOVE_STATS == MVS_N, "nwrl_move_stats is moveact's counters");
_Static_assert(sizeof(struct nwrl_move) == sizeof(struct moveact_cmd), "nwrl_move is moveact_cmd");

void rlmove_init(struct moveact *m, int w, int h, const struct wconf *wc)
{
    struct moveact_cfg c;
    moveact_get_config(m, &c);
    c.obs_w = w;
    c.obs_h = h;
    c.hold_attack = wconf_on(wc, WC_HOLD_ATTACK);
    c.look_target = wconf_on(wc, WC_LOOK_TARGET);
    c.look_ticks = wconf_int(wc, WC_LOOK_TICKS);
    moveact_config(m, &c);
}

int nwrl_move_config(struct nwrl *h, const struct nwrl_move_cfg *c)
{
    struct moveact *m = rlmove_of(h);
    struct moveact_cfg mc;
    moveact_get_config(m, &mc);
    mc.hold_attack = c->hold_attack != 0;
    mc.look_target = c->look_target != 0;
    mc.look_ticks = c->look_ticks > 0 ? c->look_ticks : 3;
    mc.fov = c->fov > 0.0F ? c->fov : 70.0F;
    mc.sensitivity = c->sensitivity > 0.0F ? c->sensitivity : 0.5F;
    moveact_config(m, &mc);
    return 0;
}

void nwrl_move_get_config(struct nwrl *h, struct nwrl_move_cfg *c)
{
    struct moveact_cfg mc;
    moveact_get_config(rlmove_of(h), &mc);
    c->hold_attack = mc.hold_attack;
    c->look_target = mc.look_target;
    c->look_ticks = mc.look_ticks;
    c->fov = mc.fov;
    c->sensitivity = mc.sensitivity;
}

int nwrl_move(struct nwrl *h, const int *ids, int k, const struct nwrl_move *moves, char *err, int en)
{
    struct moveact *m = rlmove_of(h);
    for (int i = 0; i < k; ++i)
    {
        struct moveact_cmd c;
        memcpy(&c, &moves[i], sizeof c);
        if (moveact_command(m, ids[i], &c, err, (size_t)(en > 0 ? en : 0))) return -1;
    }
    return 0;
}

int nwrl_move_script(struct nwrl *h, int id, const char *path)
{
    return moveact_script(rlmove_of(h), id, path);
}

void nwrl_move_stats(struct nwrl *h, int id, uint64_t out[NWRL_MOVE_STATS])
{
    moveact_stats(rlmove_of(h), id, out);
}

void nwrl_move_state(struct nwrl *h, const int *ids, int k, int32_t *out)
{
    for (int i = 0; i < k; ++i) out[i] = moveact_state(rlmove_of(h), ids[i]);
}
