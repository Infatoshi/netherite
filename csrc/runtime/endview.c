/* lane/dragon: the End as its player sees it (endview.h) */
#include "endview.h"

#include <math.h>
#include <string.h>

#include "../engine/env.h"
#include "../engine/clientworld.h"
#include "../engine/combat.h"
#include "../engine/dragon.h"
#include "../engine/living.h"
#include "../engine/pick.h"
#include "../engine/pickobj.h"
#include "../engine/player.h"
#include "../engine/raytrace.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/survival.h"

/* the view: within this many degrees of the look (the fight's vis), crystals within this many blocks of the eye (the
 * render distance's fog past it at the configs' 4 chunks), endermen within this many (EntityEnderman's own reach) */
#define END_VIEW_DEG 75.0
#define END_CRYSTAL_RANGE 96.0
#define END_ENDER_RANGE 64.0
#define END_PI 3.14159265358979323846
#ifndef END_RAYS
#define END_RAYS 1              /* lane/dragon2: 0 is a measurement's variant only (the scan's cost: every line of sight
                                 * taken as clear, which no player sees) */
#endif
/* the crystal's second point: this far over its position, which is its box's middle (EntityEnderCrystal's yOffset is
 * half its 2-block height, pick.c pick_kind_size; lane/dragon read it as the feet, so its middle was the box's top
 * edge and its top 0.8 over the box, where every arrow passed over: lane/dragon2) */
#define END_CRYSTAL_TOP 0.8

/* the eye sees (x, y, z): within the cone, nothing solid between; d its distance */
static int sees(struct world *w, double ex, double ey, double ez, const double l[3], double x, double y, double z,
                double range, double *d)
{
    double dx = x - ex, dy = y - ey, dz = z - ez, d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > range * range || d2 < 1e-6) return 0;
    *d = sqrt(d2);
    if ((dx * l[0] + dy * l[1] + dz * l[2]) / *d < cos(END_VIEW_DEG * END_PI / 180.0)) return 0;
    if (!END_RAYS) return 1;
    struct rt_mop m;
    return !(raytrace_blocks(w, ex, ey, ez, x, y, z, 0, 1, 0, &m) && m.type >= 0);
}

void end_scan(struct neth_rl_end *o, struct session *ss, struct client_player *cp)
{
    memset(o, 0, sizeof *o);
    struct world *w = ss->client_world;
    if (w == NULL || cp->dimension != 1) return;
    o->on = 1;
    double dgx = 0.0, dgy = 0.0, dgz = 0.0;
    {
        /* the books' (lane/dragon2): what the death message would name */
        const struct surv_combat *c = &ss->sp.sv.combat;
        o->cm_dt = c->last.dtype, o->cm_name = c->last.name, o->cm_damage = c->last.damage;
        o->fall_dt = c->has_fall ? c->fall_entry.dtype : -1;
        o->fall_name = c->has_fall ? c->fall_entry.name : -1;
        o->fall_len = c->has_fall ? c->fall_len : 0.0f;
        o->sv_health = ss->sp.sv.health;
        /* lane/dragon3, the books': the server's dragon (its healing crystal, its ticks) */
        const struct dragon_state *sd = ss->sr ? serverreplay_dragon(ss->sr) : NULL;
        if (sd)
        {
            int h = sd->healing_crystal_index;
            if (h >= 0 && h < sd->n_crystals)
                o->sv_heal = 1, o->sv_heal_at[0] = (float)sd->crystals[h].x, o->sv_heal_at[1] = (float)sd->crystals[h].y,
                o->sv_heal_at[2] = (float)sd->crystals[h].z;
            for (int c = 0; c < sd->n_crystals; ++c)
                if (sd->crystals[c].in_world && !sd->crystals[c].dead)
                {
                    if (o->sv_ncry < NETH_RL_END_CRYSTALS)
                        o->sv_cry[o->sv_ncry][0] = (float)sd->crystals[c].x, o->sv_cry[o->sv_ncry][1] = (float)sd->crystals[c].y,
                        o->sv_cry[o->sv_ncry][2] = (float)sd->crystals[c].z;
                    ++o->sv_ncry;
                }
                else if (sd->crystals[c].dead)
                {
                    if (o->sv_dead_ncry < NETH_RL_END_CRYSTALS)
                        o->sv_dead_pos[o->sv_dead_ncry][0] = (int32_t)floor(sd->crystals[c].x),
                        o->sv_dead_pos[o->sv_dead_ncry][1] = (int32_t)floor(sd->crystals[c].z),
                        ++o->sv_dead_ncry;
                }
            o->sv_ticks = sd->ticks;
            o->sv_dragon_hp = sd->health;
            o->sv_pos[0] = (float)sd->x, o->sv_pos[1] = (float)sd->y, o->sv_pos[2] = (float)sd->z;
        }
    }
    double ex = cp->e.pos_x, ey = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), ez = cp->e.pos_z;
    double yr = (double)cp->rotation_yaw * END_PI / 180.0, pr = (double)cp->rotation_pitch * END_PI / 180.0;
    double l[3] = {-sin(yr) * cos(pr), -sin(pr), cos(yr) * cos(pr)};
    int n = 0;
    const struct pickobj *ob = pickobj_client_objs(cp, &n);
    for (int i = 0; i < n; ++i)
    {
        const struct pickobj *p = &ob[i];
        if (p->dead) continue;
        if (p->kind == PK_CRYSTAL)
        {
            if (o->held < NETH_RL_END_CRYSTALS)
                o->held_at[o->held][0] = (float)p->e.pos_x, o->held_at[o->held][1] = (float)p->e.pos_y,
                o->held_at[o->held][2] = (float)p->e.pos_z, o->held_id[o->held] = p->id;
            ++o->held;
            /* the 2x2 box's middle, else its top part (from below a pillar's own edge hides the middle): the point
             * seen is the one kept, the one a shot aims at */
            double x = p->e.pos_x, y = p->e.pos_y, z = p->e.pos_z, d;
            if (!sees(w, ex, ey, ez, l, x, y, z, END_CRYSTAL_RANGE, &d))
            {
                y = p->e.pos_y + END_CRYSTAL_TOP;
                if (!sees(w, ex, ey, ez, l, x, y, z, END_CRYSTAL_RANGE, &d)) continue;
            }
            int k = o->ncrystal < NETH_RL_END_CRYSTALS ? o->ncrystal : NETH_RL_END_CRYSTALS;
            if (k == NETH_RL_END_CRYSTALS && d >= o->crystal[k - 1].d) continue;
            if (k == NETH_RL_END_CRYSTALS) --k;
            while (k > 0 && o->crystal[k - 1].d > d)
            {
                o->crystal[k] = o->crystal[k - 1];
                --k;
            }
            o->crystal[k].id = p->id;
            o->crystal[k].x = (float)x, o->crystal[k].y = (float)y, o->crystal[k].z = (float)z;
            o->crystal[k].d = (float)d;
            if (o->ncrystal < NETH_RL_END_CRYSTALS) ++o->ncrystal;
        }
        else if (p->kind == PK_DRAGON)
        {
            const struct dragon_interp *ip = NULL;
            const struct dragon_state *dg = pickobj_client_dragon(p, &ip);
            if (dg == NULL) continue;
            o->dragon_id = p->id;
            o->dragon_health = ip ? ip->health : dg->health;
            o->dragon_dying = o->dragon_health <= 0.0f;
            const struct dragon_part_state *hp = &dg->part[0], *bp = &dg->part[1];
            o->head[0] = (float)hp->x, o->head[1] = (float)(hp->y + 0.5 * hp->height), o->head[2] = (float)hp->z;
            o->body[0] = (float)bp->x, o->body[1] = (float)(bp->y + 0.5 * bp->height), o->body[2] = (float)bp->z;
            o->head_h = hp->height;
            dgx = dg->x, dgy = dg->y, dgz = dg->z;
            /* the last place the server sent (the copy's own stops short of it once its health reads 0: the death's
             * rise runs in place of the interpolation) */
            o->pos[0] = (float)(ip ? ip->new_x : dg->x), o->pos[1] = (float)(ip ? ip->new_y : dg->y);
            o->pos[2] = (float)(ip ? ip->new_z : dg->z);
            double d;
            o->dragon_view = sees(w, ex, ey, ez, l, o->head[0], o->head[1], o->head[2], 256.0, &d) ||
                             sees(w, ex, ey, ez, l, o->body[0], o->body[1], o->body[2], 256.0, &d);
        }
    }
    if (o->dragon_view && !o->dragon_dying)
    {
        /* lane/dragon2: the healing beam, drawn from the dragon to its crystal while the dragon is drawn
         * (RenderDragon, EntityDragon.updateDragonEnderCrystal's nearest crystal within its box grown 32, 40 up): its
         * end is seen where the dragon is, whatever hides the crystal itself */
        double best = 1e30;
        for (int k = 0; k < o->held && k < NETH_RL_END_CRYSTALS; ++k)
        {
            double cx = o->held_at[k][0], cy = o->held_at[k][1], cz = o->held_at[k][2];
            if (!(cx + 1.0 > dgx - 40.0 && cx - 1.0 < dgx + 40.0 && cy + 1.0 > dgy - 32.0 && cy - 1.0 < dgy + 40.0 &&
                  cz + 1.0 > dgz - 40.0 && cz - 1.0 < dgz + 40.0))
                continue;
            double dd = (cx - dgx) * (cx - dgx) + (cy - dgy) * (cy - dgy) + (cz - dgz) * (cz - dgz);
            if (dd < best) best = dd, o->beam_id = o->held_id[k], o->beam[0] = (float)cx, o->beam[1] = (float)cy,
                           o->beam[2] = (float)cz;
        }
    }
    const struct clientworld *cw = cp->combat ? &cp->combat->client : NULL;
    if (cw == NULL) return;
    for (int i = 0; i < cw->nents; ++i)
    {
        const struct client_entity *ce = &cw->ents[i];
        if (!ce->is_living || ce->is_dead || ce->dying || ce->kind != HK_ENDERMAN) continue;
        double x = ce->x, y = ce->y + 0.5 * ce->height, z = ce->z, d;
        double nx = x - cp->e.pos_x, ny = ce->y - cp->e.pos_y, nz = z - cp->e.pos_z, nd = nx * nx + ny * ny + nz * nz;
        if (o->ender_near == 0.0f || nd < (double)o->ender_near * o->ender_near) o->ender_near = (float)sqrt(nd);
        if (!sees(w, ex, ey, ez, l, x, y, z, END_ENDER_RANGE, &d)) continue;
        int k = o->nender < NETH_RL_END_ENDERS ? o->nender : NETH_RL_END_ENDERS;
        if (k == NETH_RL_END_ENDERS && d >= o->ender[k - 1].d) continue;
        if (k == NETH_RL_END_ENDERS) --k;
        while (k > 0 && o->ender[k - 1].d > d)
        {
            o->ender[k] = o->ender[k - 1];
            --k;
        }
        o->ender[k].id = ce->id;
        o->ender[k].x = (float)x, o->ender[k].y = (float)y, o->ender[k].z = (float)z;
        o->ender[k].d = (float)d;
        if (o->nender < NETH_RL_END_ENDERS) ++o->nender;
    }
}
