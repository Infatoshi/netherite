/* The action compiler for moving, mining and looking (moveact.h). */
#include "moveact.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/clientstate.h"
#include "../engine/env.h"
#include "../engine/player.h"
#include "../engine/potion.h"
#include "../engine/session.h"
#include "../engine/survival.h"
#include "../engine/world.h"
#include "pool.h"

struct mv_env {
    struct moveact_cmd pend;    /* written by the trainer's thread before the step, read by its first tick */
    int has_pend;
    int holding, hx, hy, hz, hblock;
    int turning, left;
    double tyaw, tpitch;        /* the turn's target (yaw unwrapped near the camera's at its start) */
    FILE *script;
    uint64_t st[MVS_N];
};

struct moveact {
    int n;
    struct moveact_cfg c;
    struct mv_env *e;
};

static const char *const KEYNAME[PK_KEYS] = {
    [PK_FORWARD] = "forward", [PK_BACK] = "back", [PK_LEFT] = "left", [PK_RIGHT] = "right", [PK_JUMP] = "jump",
    [PK_SNEAK] = "sneak", [PK_SPRINT] = "sprint", [PK_ATTACK] = "attack", [PK_USE] = "use", [PK_DROP] = "drop",
    [PK_INVENTORY] = "inventory", [PK_PICK] = "pick",
    [PK_HOTBAR1] = "hotbar.1", [PK_HOTBAR1 + 1] = "hotbar.2", [PK_HOTBAR1 + 2] = "hotbar.3",
    [PK_HOTBAR1 + 3] = "hotbar.4", [PK_HOTBAR1 + 4] = "hotbar.5", [PK_HOTBAR1 + 5] = "hotbar.6",
    [PK_HOTBAR1 + 6] = "hotbar.7", [PK_HOTBAR1 + 7] = "hotbar.8", [PK_HOTBAR9] = "hotbar.9",
};

struct moveact *moveact_new(int n)
{
    struct moveact *m = calloc(1, sizeof *m);
    if (m == NULL) return NULL;
    m->n = n;
    m->e = calloc((size_t)n, sizeof *m->e);
    if (m->e == NULL)
    {
        free(m);
        return NULL;
    }
    m->c.look_ticks = 3;
    m->c.fov = 70.0F;
    m->c.sensitivity = 0.5F;
    return m;
}

void moveact_free(struct moveact *m)
{
    if (m == NULL) return;
    for (int i = 0; i < m->n; ++i)
        if (m->e[i].script) fclose(m->e[i].script);
    free(m->e);
    free(m);
}

void moveact_config(struct moveact *m, const struct moveact_cfg *c)
{
    m->c = *c;
    if (m->c.look_ticks < 1) m->c.look_ticks = 1;
    if (!(m->c.fov > 0.0F)) m->c.fov = 70.0F;
    if (!(m->c.sensitivity >= 0.0F)) m->c.sensitivity = 0.5F;
}

void moveact_get_config(const struct moveact *m, struct moveact_cfg *c)
{
    *c = m->c;
}

void moveact_reset(struct moveact *m, int id)
{
    struct mv_env *e = &m->e[id];
    FILE *keep = e->script;
    memset(e, 0, sizeof *e);
    e->script = keep;
}

int moveact_command(struct moveact *m, int id, const struct moveact_cmd *c, char *err, size_t n)
{
    if (id < 0 || id >= m->n)
    {
        snprintf(err, n, "moveact: no env %d", id);
        return -1;
    }
    if (c->attack != MV_ATTACK_NONE && (!m->c.hold_attack || (c->attack != MV_ATTACK_PRESS && c->attack != MV_ATTACK_RELEASE)))
    {
        snprintf(err, n, m->c.hold_attack ? "moveact: attack command %d" : "moveact: actions.hold_attack is off", c->attack);
        return -1;
    }
    if (c->look != MV_LOOK_NONE &&
        (!m->c.look_target || c->look != MV_LOOK_TARGET || !(c->u >= 0.0F && c->u <= 1.0F && c->v >= 0.0F && c->v <= 1.0F)))
    {
        snprintf(err, n, m->c.look_target ? "moveact: look target %g, %g outside the frame" : "moveact: actions.look_target is off",
                 (double)c->u, (double)c->v);
        return -1;
    }
    m->e[id].pend = *c;
    m->e[id].has_pend = c->attack != MV_ATTACK_NONE || c->look != MV_LOOK_NONE;
    return 0;
}

int moveact_script(struct moveact *m, int id, const char *path)
{
    if (id < 0 || id >= m->n) return -1;
    struct mv_env *e = &m->e[id];
    if (e->script) fclose(e->script);
    e->script = NULL;
    if (path == NULL) return 0;
    e->script = fopen(path, "a");
    return e->script ? 0 : -1;
}

void moveact_stats(const struct moveact *m, int id, uint64_t out[MVS_N])
{
    memcpy(out, m->e[id].st, sizeof m->e[id].st);
}

int moveact_state(const struct moveact *m, int id)
{
    return (m->e[id].holding ? 1 : 0) | (m->e[id].turning ? 2 : 0);
}

/* ------------------------------------------------------------- the turn */

static double wrap180(double d)
{
    d = fmod(d, 360.0);
    if (d >= 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

/* EntityRenderer.getFOVModifier(1, true) less the death zoom: the fov option
 * times the movement multiplier (EntityPlayerSP.getFOVMultiplier: flying,
 * the movementSpeed attribute with the speed and slowness effects' modifiers,
 * a drawn bow), 60/70 with the eye in water */
static float frame_fov(const struct moveact *m, struct session *ss)
{
    struct client_player *cp = &ss->cp;
    struct cs_fov_in fi = {cp->is_flying, 0.1F, cp->move_speed, 0, 0};
    for (int id = 1; id <= 2; ++id)
    {
        const struct potion_effect *pe = potion_map_get(&cp->sv.potions, id);
        struct cs_attr_mod md;
        if (pe && clientstate_potion_speed_mod(id, pe->amplifier, &md)) fi.move_speed *= 1.0 + md.amount;
    }
    int us = cp->sv.using_slot;
    if (us >= 0 && cp->sv.inv[us].count > 0 && cp->sv.inv[us].item == 261)
    {
        fi.bow = 1;
        fi.use_duration = 72000 - cp->sv.using_count;
    }
    float mult = clientstate_fov_multiplier(&fi);
    if (mult > 1.5F) mult = 1.5F;
    if (mult < 0.1F) mult = 0.1F;
    float fov = m->c.fov * mult;
    struct world *cw = ss->client_world;
    if (cw)
    {
        int b = world_get_block(cw, (int)floor(cp->e.pos_x), (int)floor(cp->e.pos_y), (int)floor(cp->e.pos_z)) & 4095;
        if (b == 8 || b == 9) fov = fov * 60.0F / 70.0F;
    }
    return fov;
}

/* the yaw and pitch that put frame point (u, v) under the crosshair, seen
 * from the camera's current look */
static void target_of(const struct moveact *m, struct session *ss, float u, float v, double *yaw, double *pitch)
{
    const struct client_player *cp = &ss->cp;
    const double R = 3.14159265358979323846 / 180.0;
    double y0 = cp->rotation_yaw * R, p0 = cp->rotation_pitch * R;
    double t = tan(frame_fov(m, ss) * 0.5 * R);
    double asp = m->c.obs_h > 0 ? (double)m->c.obs_w / m->c.obs_h : 1.0;
    double sx = (2.0 * u - 1.0) * t * asp, sy = (1.0 - 2.0 * v) * t;
    /* the camera's forward, right and up in the world (Entity.getLook: yaw 0 faces +z) */
    double f[3] = {-sin(y0) * cos(p0), -sin(p0), cos(y0) * cos(p0)};
    double r[3] = {-cos(y0), 0.0, -sin(y0)};
    double up[3] = {-sin(y0) * sin(p0), cos(p0), cos(y0) * sin(p0)};
    double d[3];
    for (int i = 0; i < 3; ++i) d[i] = f[i] + sx * r[i] + sy * up[i];
    double ty = atan2(-d[0], d[2]) / R, tp = -atan2(d[1], sqrt(d[0] * d[0] + d[2] * d[2])) / R;
    *yaw = cp->rotation_yaw + wrap180(ty - cp->rotation_yaw);
    *pitch = tp;
}

/* whole mouse counts toward the target this tick, as degrees (EntityRenderer:
 * the delta times f^3 * 8, then setAngles' 0.15) */
static float mouse_deg(const struct moveact *m, double want)
{
    float f = m->c.sensitivity * 0.6F + 0.2F;
    float f2 = f * f * f * 8.0F;
    double step = (double)f2 * 0.15;
    if (step <= 0.0) return 0.0F;
    double counts = floor(want / step + 0.5);
    return (float)((double)((float)counts * f2) * 0.15);
}

/* ------------------------------------------------------------- the script */

static void script_line(FILE *f, const struct pool_act *a)
{
    fputs("{\"n\":1,\"act\":{", f);
    const char *sep = "";
    if (a->look_mode == PL_LOOK)
    {
        if (a->look_prev)
            fprintf(f, "\"look\":[%.9g,%.9g,%.9g,%.9g]", (double)a->look[0], (double)a->look[1], (double)a->look[2],
                    (double)a->look[3]);
        else
            fprintf(f, "\"look\":[%.9g,%.9g]", (double)a->look[0], (double)a->look[1]);
        sep = ",";
    }
    else if (a->look_mode == PL_DLOOK)
    {
        fprintf(f, "\"dlook\":[%.9g,%.9g]", (double)a->look[0], (double)a->look[1]);
        sep = ",";
    }
    if (a->hold)
    {
        fprintf(f, "%s\"hold\":[", sep);
        const char *s2 = "";
        for (int k = 0; k < PK_KEYS; ++k)
            if ((a->hold >> k) & 1)
            {
                fprintf(f, "%s\"%s\"", s2, KEYNAME[k]);
                s2 = ",";
            }
        fputc(']', f);
        sep = ",";
    }
    int any = 0;
    for (int k = 0; k < PK_KEYS; ++k) any |= a->press[k] != 0;
    if (any)
    {
        fprintf(f, "%s\"press\":[", sep);
        const char *s2 = "";
        for (int k = 0; k < PK_KEYS; ++k)
            for (int c = 0; c < a->press[k]; ++c)
            {
                fprintf(f, "%s\"%s\"", s2, KEYNAME[k]);
                s2 = ",";
            }
        fputc(']', f);
        sep = ",";
    }
    if (a->hotbar != -1)
    {
        fprintf(f, "%s\"hotbar\":%d", sep, a->hotbar);
        sep = ",";
    }
    if (a->ctrl)
    {
        fprintf(f, "%s\"ctrl\":%d", sep, a->ctrl);
        sep = ",";
    }
    if (a->nops > 0)
    {
        fprintf(f, "%s\"gui\":[", sep);
        for (int i = 0; i < a->nops && i < POOL_GUI_OPS; ++i)
        {
            const struct pool_gui_op *o = &a->ops[i];
            if (i) fputc(',', f);
            switch (o->kind)
            {
            case PG_CLICK: fprintf(f, "[\"click\",%d,%d,%d,%d]", o->window, o->slot, o->button, o->mode); break;
            case PG_CLOSE: fputs("[\"close\"]", f); break;
            case PG_RESPAWN: fputs("[\"respawn\"]", f); break;
            case PG_WAKE: fputs("[\"wake\"]", f); break;
            default: fprintf(f, "[\"trsel\",%d]", o->index); break;
            }
        }
        fputc(']', f);
    }
    fputs("}}\n", f);
}

/* ------------------------------------------------------------- the hook */

static int screen_open(const struct client_player *cp)
{
    return cp->screen_inventory || cp->screen_gameover || cp->screen_sleep || cp->screen_chat || cp->screen_credits;
}

int moveact_hook(void *ctx, int id, struct session *ss, int k, const struct pool_act *src, int hold_only,
                 struct pool_act *out)
{
    struct moveact *m = ctx;
    struct mv_env *e = &m->e[id];
    if (!m->c.hold_attack && !m->c.look_target && e->script == NULL) return 0;
    if (hold_only)
    {
        /* Act.holdOnly: the holds stay, nothing else repeats */
        memset(out, 0, sizeof *out);
        out->kind = PA_AGENT;
        out->hold = src->hold;
        out->look_mode = PL_NONE;
        out->hotbar = -1;
    }
    else
        *out = *src;
    struct client_player *cp = &ss->cp;
    const struct surv_mouseover_pos *mo = &nw_env->survival.mo.cur;
    int on_block = mo->num != 0 && !mo->is_miss && mo->entity_id == 0;
    const uint32_t ATT = 1u << PK_ATTACK;
    int pressed = 0;
    if (k == 0 && e->has_pend)
    {
        const struct moveact_cmd *c = &e->pend;
        if (c->attack == MV_ATTACK_PRESS)
        {
            /* a person's click: the key down (held) and its press */
            if (out->press[PK_ATTACK] == 0) out->press[PK_ATTACK] = 1;
            out->hold |= ATT;
            ++e->st[MVS_PRESSES];
            pressed = 1;
            e->holding = 0;
            struct world *cw = ss->client_world;
            if (on_block && cw && !screen_open(cp))
            {
                e->holding = 1;
                e->hx = mo->x;
                e->hy = mo->y;
                e->hz = mo->z;
                e->hblock = world_get_block(cw, mo->x, mo->y, mo->z) & 4095;
                ++e->st[MVS_HOLDS];
                ++e->st[MVS_HOLD_TICKS];
            }
        }
        else if (c->attack == MV_ATTACK_RELEASE && e->holding)
        {
            e->holding = 0;
            ++e->st[MVS_END_RELEASE];
        }
        if (c->look == MV_LOOK_TARGET)
        {
            target_of(m, ss, c->u, c->v, &e->tyaw, &e->tpitch);
            e->turning = 1;
            e->left = m->c.look_ticks;
            ++e->st[MVS_TURNS];
        }
        e->has_pend = 0;
    }
    if (e->holding && !pressed)
    {
        struct world *cw = ss->client_world;
        int end = -1;
        if (screen_open(cp)) end = MVS_END_SCREEN;
        else if (cw == NULL || (world_get_block(cw, e->hx, e->hy, e->hz) & 4095) != e->hblock) end = MVS_END_BROKEN;
        else if (!on_block || mo->x != e->hx || mo->y != e->hy || mo->z != e->hz) end = MVS_END_TARGET;
        if (end >= 0)
        {
            e->holding = 0;
            ++e->st[end];
        }
        else
        {
            out->hold |= ATT;
            ++e->st[MVS_HOLD_TICKS];
        }
    }
    if (e->turning)
    {
        double dy = (e->tyaw - cp->rotation_yaw) / e->left;
        double dp = (e->tpitch - cp->rotation_pitch) / e->left;
        out->look_mode = PL_DLOOK;
        out->look_prev = 0;
        out->look[0] = mouse_deg(m, dy);
        out->look[1] = mouse_deg(m, dp);
        if (out->look[0] == 0.0F && out->look[1] == 0.0F) out->look_mode = PL_NONE;
        else ++e->st[MVS_TURN_TICKS];
        if (--e->left <= 0) e->turning = 0;
    }
    if (e->script) script_line(e->script, out);
    return 1;
}
