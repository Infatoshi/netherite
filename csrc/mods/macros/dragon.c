/* NETH_VEC_DRAGON (lane/dragon): the End's fight as a mod (lane/macromod: macro_start's, macro_drive's and the env
 * reset's lines cut from vecenv.c unchanged). The fight itself and the End's books are csrc/runtime/vecdragon.c (its own
 * translation unit: dragon_want, dragon_begin, dragon_drive; the books run with the switch off too, the End and the
 * dragon's death are milestones); vecenv.c's dragon_view and dragon_input give it the player and the End in view */

/* the player as the End fight reads it (vecdragon.h struct dragon_in), from env e's last result and the action */
static void dragon_input(const struct neth_vec *v, struct vec_env *e, const int32_t *act)
{
    const struct vec_last *r = &e->last;
    struct dragon_in *in = &e->dgin;
    in->x = r->x, in->y = r->y, in->z = r->z;
    in->yaw = r->yaw, in->pitch = r->pitch, in->health = r->health;
    in->dim = r->dim;
    in->dead = (r->flags & PF_DEAD) != 0;
    in->screen = r->screen;
    in->on_ground = r->on_ground;
    in->food = r->food;
    in->steps = e->steps;
    in->lob = MOPT(MO_DG_LOB), in->crit = MOPT(MO_DG_CRIT), in->block = MOPT(MO_DG_BLOCK);   /* lane/macroopts */
    in->oracle = MOPT(MO_DG_ORACLE);    /* lane/shold */
    in->snow = MOPT(MO_DG_CRYSTALS);
    in->food_fetch = MOPT(MO_DG_FOOD);   /* lane/chain2 */
    in->sword_stock = MOPT(MO_SWORD_STOCK);
    /* lane/legs: dragon_hunt_cap, and dragon_snow_tower's cap (16 the default: 0; 24; 32) */
    in->hunt_opts = MOPT(MO_DG_HUNT_CAP) | (MOPT(MO_DG_SNOW_TOWER) == 1 ? 24 : MOPT(MO_DG_SNOW_TOWER) == 2 ? 32 : 0) << 8;
    in->hunt_opts |= MOPT(MO_DG_CLUTCH) << 1;   /* lane/dleg: dragon_clutch water, bit 1 */
    if (MOPT(MO_DG_CLUTCH) && r->dim == 1 && !r->on_ground)
    {
        /* lane/dleg, PRIVILEGED: the clutch's ground, the top face of the first block under the feet's column within
         * 8 blocks (bit 4 found; bits 16-23 its y), for the press on the last tick before the landing */
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 1e-3), fz = (int)floor(r->z);
        for (int k = 0; k <= 8 && fy - k >= 0; ++k)
            if (shoracle_block_dim(v->p, (int)(e - v->e), 1, fx, fy - k, fz, NULL) != 0)
            {
                in->hunt_opts |= 16 | ((fy - k + 1) & 255) << 16;
                break;
            }
    }
    in->hunt_opts |= MOPT(MO_DG_FOLLOW) << 5;   /* lane/dleg: dragon_follow server, bit 5 (privileged) */
    in->hunt_opts |= MOPT(MO_DG_WAIT) << 6;     /* lane/dleg: dragon_wait centre, bit 6; band (2), bit 7 */
    if (MOPT(MO_DG_WAIT) == 2 && r->dim == 1)
    {
        /* lane/dleg, PRIVILEGED (dragon_wait band): the island's widest side, bits 29-31: along each eighth of the circle
         * from the centre, the first column from r 8 with no block from y 30 to 75 (the void), at most 64; the eighth
         * whose void is farthest. Read once every ray ends inside the server's loaded chunks (a column not loaded
         * ends it unread, tried again 20 steps later); until then the eighth away from where the player stands */
        static const double C8[8] = {1.0, 0.70710678118654752, 0.0, -0.70710678118654752, -1.0, -0.70710678118654752,
                                     0.0, 0.70710678118654752};
        struct vec_dragon *g = &e->dg;
        if (!g->band_ok && e->steps % 20 == 0)
        {
            int id = (int)(e - v->e), best = -1, bk = 0, all = 1;
            for (int k = 0; k < 8 && all; ++k)
            {
                int rr = 8;
                for (; rr < 64; ++rr)
                {
                    int cx = (int)floor(0.5 + rr * C8[k]), cz = (int)floor(0.5 + rr * C8[(k + 6) & 7]), any = 0;
                    for (int y = 30; y <= 75 && !any; ++y)
                    {
                        int b = shoracle_block_dim(v->p, id, 1, cx, y, cz, NULL);
                        if (b < 0) { all = 0; break; }
                        any = b != 0;
                    }
                    if (!all || !any) break;
                }
                if (all && rr > best) best = rr, bk = k;
            }
            if (all) g->band_ok = 1, g->band_k = bk;
        }
        int k = g->band_ok ? g->band_k : 0;
        if (!g->band_ok)
        {
            /* the eighth nearest the direction away from the player */
            double ang = atan2(-(r->z - 0.5), -(r->x - 0.5));
            k = (int)floor(ang / (M_PI / 4.0) + 0.5);
            k = ((k % 8) + 8) % 8;
        }
        in->hunt_opts = (int32_t)((uint32_t)in->hunt_opts | (uint32_t)k << 29);
    }
    {
        /* lane/dleg: dragon_late off, nohunt, nopit, walkin, all: bits 24, 25, 26 */
        static const int32_t late_bits[5] = {0, 1 << 24, 1 << 25, 1 << 26, 7 << 24};
        in->hunt_opts |= late_bits[MOPT(MO_DG_LATE)];
    }
    {
        /* lane/dleg: dragon_pit dig, once, stand: bits 27, 28 */
        static const int32_t pit_bits[3] = {0, 1 << 27, 1 << 28};
        in->hunt_opts |= pit_bits[MOPT(MO_DG_PIT)];
    }
    if (MOPT(MO_DG_HOLE_FLOOR) && r->dim == 1)
    {
        /* lane/dleg, PRIVILEGED: dragon_hole_floor on, bit 2; bit 3 no block two under the feet (the cell under the
         * one a hole would dig) */
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 1e-3), fz = (int)floor(r->z);
        in->hunt_opts |= 4;
        if (fy >= 2 && shoracle_block_dim(v->p, (int)(e - v->e), 1, fx, fy - 2, fz, NULL) == 0) in->hunt_opts |= 8;
    }
    int pb = act[0] < 0 ? 0 : act[0] >= NETH_VEC_CAM ? NETH_VEC_CAM - 1 : act[0];
    int yb = act[1] < 0 ? 0 : act[1] >= NETH_VEC_CAM ? NETH_VEC_CAM - 1 : act[1];
    in->nudge[0] = (float)v->camdeg[yb];
    in->nudge[1] = (float)v->camdeg[pb];
}

/* lane/shold, PRIVILEGED (dragon_oracle crystals): the crystal to take with the sword and the tower's cell for it, into
 * dragon_in's rc_*. Every crystal copy the client holds (seen or not), not given up on, nearest first; its pillar read
 * off the server's End blocks: the bedrock cap under the crystal (WorldGenSpikes: the cap at the obsidian's top, the
 * crystal on it, its blast at the cap's top face), the tower's cell outside the obsidian's circle whose middle is within
 * DG_RAID_REACH of the crystal's 2.2-wide box (the swing's 3 blocks from an eye pressed toward it), its column open from
 * the ground to the cap's cell, the ground end stone or obsidian (not the void), at most the blocks held high; the
 * cheapest by blocks and walk. The tower's top is the cap's cell less 1: the body under the cap's top face, every ray
 * of the blast to it crosses the cap (Explosion.getBlockDensity 0: 1 damage) and so does every ray to the tower. */
#define DG_RAID_REACH 2.75
#define DG_BRIDGE_INLAND_X 20.0
#define DG_BRIDGE_SIDE_Z 12.0
#define DG_BRIDGE_EDGE 0.55
#define DG_BRIDGE_CENTER 0.20

/* Add the adjacent floor before leaving each placed bridge cell. The arrival's z is the centre lane; the selected
 * side is kept in gz while DP_PIT owns the bridge (the later exit walk assigns its own gz). Sneaking over the current
 * floor's side exposes that face to the crosshair without putting an unsupported foot on the next cell. */
static int dg_bridge_wide_step(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s,
                               struct neth_rl_act *a, int fx, int fy, int hb)
{
    if (MOPT(MO_DG_BRIDGE) != 2 || fx > 97 || fx <= DG_BRIDGE_INLAND_X) return 0;
    const struct vec_last *r = &e->last;
    int id = (int)(e - v->e), lane = (int)floor(e->dg.az + 0.5);
    if (e->dg.gz == 0.0) e->dg.gz = r->z <= e->dg.az ? -1.0 : 1.0;
    int side = e->dg.gz < 0.0 ? -1 : 1;
    int base = shoracle_block_dim(v->p, id, 1, fx, fy - 1, lane, NULL);
    if (!shoracle_floor(base) || base == 121) return 0;
    int other = shoracle_block_dim(v->p, id, 1, fx, fy - 1, lane + side, NULL);
    double center = lane + 0.5, along = (r->z - center) * side, yaw, pitch;
    if (other == 0 &&
        shoracle_open(shoracle_block_dim(v->p, id, 1, fx, fy, lane + side, NULL)) &&
        shoracle_open(shoracle_block_dim(v->p, id, 1, fx, fy + 1, lane + side, NULL)))
    {
        still(a);
        a->hotbar = hb;
        a->hold = 1u << PK_SNEAK;
        if (along < DG_BRIDGE_EDGE)
        {
            look_at(r->x, r->y + 1.62, r->z, fx + 0.5, r->y + 1.62, lane + side + 0.5, &yaw, &pitch);
            a->hold |= 1u << PK_FORWARD;
        }
        else
            look_at(r->x, r->y + 1.62, r->z, fx + 0.5, fy - 0.5, center + 0.5 * side, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        if (along >= DG_BRIDGE_EDGE && s->mo_hit && s->mo_x == fx && s->mo_y == fy - 1 && s->mo_z == lane &&
            s->mo_side == (side > 0 ? 3 : 2) && s->current == hb) a->press[PK_USE] = 1;
        return 1;
    }
    if (other > 0 && fabs(r->z - center) > DG_BRIDGE_CENTER)
    {
        still(a);
        look_at(r->x, r->y + 1.62, r->z, fx + 0.5, r->y + 1.62, center, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hold = (1u << PK_SNEAK) | (1u << PK_FORWARD);
        return 1;
    }
    return 0;
}

/* The End arrival platform is separate from the island on some seeds. The ordinary first walk assumes a floor until
 * it gets in from x=100; seed 2 fell into the void at x=80 in all 31 measured End attempts. With the privileged search
 * option on, inspect only the adjacent End floor cell and place carried cobblestone on the current floor's west face
 * while sneaking. The actual acts are the same look, sneak and use a player can send. */
/* lane/dleg (dragon_bridge cliff): the bridge's walk stopped by the island's cliff (the next cell west not open at the
 * feet or the head) while the floor here is a one-wide bridge: a block placed on each side of the floor cell (sneaking
 * to that side's edge, the look on the floor's side face, as the wide step does), so the stand where the rise towers up
 * the cliff is three wide; at most DG_CLIFF_STEPS steps an episode. Traced on s2's End rows (b0, cl2): 58 of 64 reach
 * the cliff at x 79.3, stand there about 100 steps towering up its face, and 15 die knocked sideways off the one-wide
 * bridge by the dragon's first pass */
#define DG_CLIFF_STEPS 80
static int dg_bridge_cliff(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s,
                           struct neth_rl_act *a, int fx, int fy, int fz, int hb)
{
    const struct vec_last *r = &e->last;
    int id = (int)(e - v->e);
    if (e->dg.cliff_steps >= DG_CLIFF_STEPS || !r->on_ground) return 0;
    /* the cell is the one the routine began on (the sneak to a side's edge carries the body's middle over the next
     * cell), while the feet are within a block of it */
    if (e->dg.cliff_steps > 0 && fabs(r->x - (e->dg.cliff_x + 0.5)) < 1.0 && fabs(r->z - (e->dg.cliff_z + 0.5)) < 1.0)
        fx = e->dg.cliff_x, fz = e->dg.cliff_z;
    else if (!shoracle_floor(shoracle_block_dim(v->p, id, 1, fx, fy - 1, fz, NULL)))
        return 0;
    if (shoracle_open(shoracle_block_dim(v->p, id, 1, fx - 1, fy, fz, NULL)) &&
        shoracle_open(shoracle_block_dim(v->p, id, 1, fx - 1, fy + 1, fz, NULL))) return 0;
    double center = fz + 0.5, yaw, pitch;
    for (int side = -1; side <= 1; side += 2)
    {
        if (shoracle_block_dim(v->p, id, 1, fx, fy - 1, fz + side, NULL) != 0 ||
            !shoracle_open(shoracle_block_dim(v->p, id, 1, fx, fy, fz + side, NULL)) ||
            !shoracle_open(shoracle_block_dim(v->p, id, 1, fx, fy + 1, fz + side, NULL))) continue;
        if (e->dg.cliff_steps == 0) e->dg.cliff_x = fx, e->dg.cliff_z = fz;
        ++e->dg.cliff_steps;
        double along = (r->z - center) * side;
        still(a);
        a->hotbar = hb;
        a->hold = 1u << PK_SNEAK;
        if (along < DG_BRIDGE_EDGE)
        {
            look_at(r->x, r->y + 1.62, r->z, fx + 0.5, r->y + 1.62, fz + side + 0.5, &yaw, &pitch);
            a->hold |= 1u << PK_FORWARD;
        }
        else
            look_at(r->x, r->y + 1.62, r->z, fx + 0.5, fy - 0.5, center + 0.5 * side, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        if (along >= DG_BRIDGE_EDGE && s->mo_hit && s->mo_x == fx && s->mo_y == fy - 1 && s->mo_z == fz &&
            s->mo_side == (side > 0 ? 3 : 2) && s->current == hb) a->press[PK_USE] = 1;
        return 1;
    }
    if (fabs(r->z - center) > DG_BRIDGE_CENTER && e->dg.cliff_steps > 0 && e->dg.cliff_steps < DG_CLIFF_STEPS)
    {
        /* both sides down: the feet back to the cell's middle before the rise */
        ++e->dg.cliff_steps;
        still(a);
        look_at(r->x, r->y + 1.62, r->z, fx + 0.5, r->y + 1.62, center, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hold = (1u << PK_SNEAK) | (1u << PK_FORWARD);
        return 1;
    }
    return 0;
}

static int dg_bridge_step(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s, struct neth_rl_act *a)
{
    const struct vec_last *r = &e->last;
    if (!MOPT(MO_DG_BRIDGE) || e->dg.phase != DP_PIT || r->dim != 1 || r->y < 40.0 ||
        r->x <= DG_BRIDGE_INLAND_X || fabs(r->z) > DG_BRIDGE_SIDE_Z) return 0;
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    int sx = fx, target = fx - 1;
    int floor_here = shoracle_block_dim(v->p, id, 1, fx, fy - 1, fz, NULL);
    if (MOPT(MO_DG_BRIDGE) == 3)
    {
        int hb3 = sh_hotbar(s, 4);
        if (hb3 < 0) hb3 = sh_hotbar(s, 121);
        if (hb3 >= 0 && dg_bridge_cliff(v, e, s, a, fx, fy, fz, hb3)) return 1;
    }
    if (floor_here == 0)
    {
        sx = fx + 1;
        target = fx;
    }
    else if (shoracle_block_dim(v->p, id, 1, target, fy - 1, fz, NULL) != 0) return 0;
    if (!shoracle_floor(shoracle_block_dim(v->p, id, 1, sx, fy - 1, fz, NULL)) ||
        shoracle_block_dim(v->p, id, 1, target, fy - 1, fz, NULL) != 0 ||
        !shoracle_open(shoracle_block_dim(v->p, id, 1, target, fy, fz, NULL)) ||
        !shoracle_open(shoracle_block_dim(v->p, id, 1, target, fy + 1, fz, NULL))) return 0;
    int hb = sh_hotbar(s, 4);
    if (hb < 0) hb = sh_hotbar(s, 121);
    if (hb < 0) return 0;
    if (dg_bridge_wide_step(v, e, s, a, fx, fy, hb)) return 1;
    still(a);
    a->hotbar = hb;
    a->hold = 1u << PK_SNEAK;
    double along = sx + 0.5 - r->x, yaw, pitch;
    if (along < DG_BRIDGE_EDGE)
    {
        look_at(r->x, r->y + 1.62, r->z, target + 0.5, r->y + 1.62, fz + 0.5, &yaw, &pitch);
        a->hold |= 1u << PK_FORWARD;
    }
    else
        look_at(r->x, r->y + 1.62, r->z, sx, fy - 0.5, fz + 0.5, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw, a->look[1] = (float)pitch;
    if (along >= DG_BRIDGE_EDGE && s->mo_hit && s->mo_x == sx && s->mo_y == fy - 1 && s->mo_z == fz &&
        s->mo_side == 4 && s->current == hb) a->press[PK_USE] = 1;
    return 1;
}

static void dg_raid_pick(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s)
{
    struct dragon_in *in = &e->dgin;
    const struct vec_dragon *d = &e->dg;
    const struct neth_rl_end *en = dragon_view(v, e);
    int id = (int)(e - v->e), held = 0;
    for (int i = 0; i < 36; ++i)
        if (s->inv[i].item == 4 || s->inv[i].item == 121) held += s->inv[i].count;
    in->rc_ok = 0;
    double best = 1e30;
    int nh = en->held < NETH_RL_END_CRYSTALS ? en->held : NETH_RL_END_CRYSTALS;
    for (int k = 0; k < nh; ++k)
    {
        int skip = 0;
        for (int j = 0; j < d->nraid_skip; ++j) skip |= d->raid_skip[j] == en->held_id[k];
        if (skip) continue;
        double x = en->held_at[k][0], y = en->held_at[k][1], z = en->held_at[k][2];
        int cx = (int)floor(x), cz = (int)floor(z), cap = -1;
        for (int yy = (int)floor(y) + 1; yy >= (int)floor(y) - 3 && yy > 0; --yy)
            if (shoracle_block_dim(v->p, id, 1, cx, yy, cz, NULL) == 7)
            {
                cap = yy;
                break;
            }
        if (cap < 0) continue;
        /* the obsidian's radius at the cap's height less 1 (WorldGenSpikes: dx^2 + dz^2 <= r^2 + 1) */
        int r = 0;
        while (r < 6 && shoracle_block_dim(v->p, id, 1, cx + r + 1, cap - 1, cz, NULL) == 49) ++r;
        int top = cap - 1;
        for (int dx = -(r + 2); dx <= r + 2; ++dx)
            for (int dz = -(r + 2); dz <= r + 2; ++dz)
            {
                if (dx * dx + dz * dz <= r * r + 1) continue;
                double hx = fabs((double)dx) - 1.1, hz = fabs((double)dz) - 1.1;
                if (hx < 0) hx = 0;
                if (hz < 0) hz = 0;
                if (sqrt(hx * hx + hz * hz) > DG_RAID_REACH) continue;
                int sx = cx + dx, sz = cz + dz, ground = -1, open = 1;
                for (int yy = top + 1; yy >= 1; --yy)
                {
                    int b = shoracle_block_dim(v->p, id, 1, sx, yy, sz, NULL);
                    if (b < 0)
                    {
                        open = 0;
                        break;
                    }
                    if (b != 0)
                    {
                        if (yy >= top) open = 0;        /* the column not open over the top */
                        else if (b == 121 || b == 49) ground = yy + 1;
                        else open = 0;
                        break;
                    }
                }
                if (!open || ground < 0 || top - ground > held) continue;
                double wx = sx + 0.5 - in->x, wz = sz + 0.5 - in->z;
                double cost = 4.0 * (top - ground) + sqrt(wx * wx + wz * wz);
                if (cost < best)
                {
                    best = cost;
                    in->rc_ok = 1, in->rc_id = en->held_id[k];
                    in->rc_x = cx + 0.5, in->rc_y = cap + 1, in->rc_z = cz + 0.5;
                    in->rc_sx = sx, in->rc_sz = sz, in->rc_top = top, in->rc_ground = ground;
                }
            }
    }
}

/* the player as this step's act reads it, then the fight wanted (vecdragon.h dragon_want) */
static int dragon_m_cand(struct mac_step *c)
{
    if (!(c->v->macros & NETH_VEC_DRAGON)) return 0;
    struct vec_env *e = c->e;
    dragon_input(c->v, e, c->act);
    e->dgin.rc_ok = 0;
    if (e->dgin.oracle && e->dg.raid_ask && c->s) dg_raid_pick(c->v, e, c->s);
    return dragon_want(&e->dg, &e->dgin, c->s, dragon_view(c->v, e));
}

/* the End's fight, the step the run needs once in the End */
static int dragon_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    if ((v->macros & NETH_VEC_DRAGON) && dragon_want(&e->dg, &e->dgin, c->s, dragon_view(v, e)))
    {
        e->kind = M_MINE;
        e->dragon = 1;
        dragon_begin(&e->dg, &e->dgin, c->s, dragon_view(v, e));
        return 1;
    }
    return 0;
}

/* lane/chain3 (dragon_ender_avert on): the pit phase's act kept from provoking an enderman. The look is vanilla's test
 * (EntityEnderman.shouldAttackPlayer: the look within acos(1 - 0.025 / d) of the box middle, the line clear) against the
 * endermen the end view has in sight, at twice the cone; a look inside it is turned 30 degrees up or down away from the
 * middle (at most three turns). An enderman under the crosshair takes no attack (a hit provokes it too) */
/* lane/dleg (dragon_ender_avert all): the stare broken before it provokes, in every phase. 1.7.10's enderman takes the
 * player once shouldAttackPlayer holds on 6 ticks running (findPlayerToAttack: stareTimer++ == 5; the look's dot with
 * the line to its box middle over 1 - 0.025 / d, a clear line): each enderman in view keeps the run of steps the act's
 * look met its cone, and the step its run would reach DG_STARE_BREAK the look is turned out of it by the cone's
 * half-angle plus DG_STARE_PAD (the yaw, or the pitch where the look is steep), one step; no attack is pressed while an
 * enderman is under the crosshair. On dc's rows 200 stares ran 6 or more, 175 of them in the pit phase, and 69 of the
 * 98 enderman deaths had one in the 1200 steps before */
#define DG_STARE_BREAK 4
#define DG_STARE_PAD 2.0
static int dg_stare_cone(const struct dragon_in *in, float yaw, float pitch, float ex, float ey, float ez, double *sep,
                         double *half)
{
    double yr = yaw * M_PI / 180.0, pr = pitch * M_PI / 180.0;
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
    double dx = ex - in->x, dy = ey - (in->y + 1.62), dz = ez - in->z, d = sqrt(dx * dx + dy * dy + dz * dz);
    if (d <= 0.0 || d > 64.0) return 0;
    double c = (lx * dx + ly * dy + lz * dz) / d, lim = 1.0 - 0.025 / d;
    *sep = acos(c > 1.0 ? 1.0 : c < -1.0 ? -1.0 : c) * 180.0 / M_PI;
    *half = acos(lim) * 180.0 / M_PI;
    return c > lim;
}

static void dg_stare_break(struct vec_env *e, const struct neth_rl_state *s, const struct neth_rl_end *en, struct neth_rl_act *a)
{
    const struct dragon_in *in = &e->dgin;
    struct vec_dragon *d = &e->dg;
    if (!en || !en->on) return;
    float yaw = a->look_mode == 1 ? a->look[0] : a->look_mode == 2 ? in->yaw + a->look[0] : in->yaw;
    float pitch = a->look_mode == 1 ? a->look[1] : a->look_mode == 2 ? in->pitch + a->look[1] : in->pitch;
    int32_t ids[NETH_RL_END_ENDERS];
    int runs[NETH_RL_END_ENDERS], n = en->nender < NETH_RL_END_ENDERS ? en->nender : NETH_RL_END_ENDERS, turned = 0;
    for (int k = 0; k < n; ++k)
    {
        int old = 0;
        for (int j = 0; j < d->av_n; ++j)
            if (d->av_id[j] == en->ender[k].id) old = d->av_run[j];
        double sep, half;
        int in_cone = dg_stare_cone(in, yaw, pitch, en->ender[k].x, en->ender[k].y, en->ender[k].z, &sep, &half);
        ids[k] = en->ender[k].id, runs[k] = in_cone ? old + 1 : 0;
        if (runs[k] < DG_STARE_BREAK) continue;
        /* out of the cone: away from the enderman's bearing by the half-angle less the separation plus the pad */
        double turn = half - sep + DG_STARE_PAD;
        if (fabsf(pitch) > 60.0f)
            pitch += (en->ender[k].y > in->y + 1.62 ? -1.0f : 1.0f) * (float)turn;
        else
        {
            double by = atan2(-(en->ender[k].x - in->x), en->ender[k].z - in->z) * 180.0 / M_PI;
            double df = fmod(by - yaw + 540.0, 360.0) - 180.0;
            yaw += (float)(df > 0.0 ? -turn : turn);
        }
        if (pitch > 90.0f) pitch = 90.0f;
        if (pitch < -90.0f) pitch = -90.0f;
        runs[k] = 0, turned = 1, ++d->av_breaks;
    }
#ifdef DG_STARE_TRACE
    if (n > 0)
        fprintf(stderr, "STARE %lld n %d run0 %d turned %d look %d yaw %.1f pitch %.1f e0 %.1f %.1f %.1f\n",
                (long long)in->steps, n, runs[0], turned, a->look_mode, yaw, pitch, en->ender[0].x, en->ender[0].y,
                en->ender[0].z);
#endif
    d->av_n = n;
    for (int k = 0; k < n; ++k) d->av_id[k] = ids[k], d->av_run[k] = runs[k];
    if (turned)
    {
        a->look_mode = 1;
        a->look[0] = yaw;
        a->look[1] = pitch;
    }
    int under = 0;
    for (int k = 0; k < n; ++k) under |= s && s->mo_entity == en->ender[k].id;
    if (under)
    {
        a->hold &= ~(1u << PK_ATTACK);
        a->press[PK_ATTACK] = 0;
    }
}

static void dg_ender_avert(const struct vec_env *e, const struct neth_rl_state *s, const struct neth_rl_end *en,
                           struct neth_rl_act *a)
{
    const struct dragon_in *in = &e->dgin;
    if (!en || !en->on || e->dg.phase != DP_PIT) return;
    float yaw = a->look_mode == 1 ? a->look[0] : a->look_mode == 2 ? in->yaw + a->look[0] : in->yaw;
    float pitch = a->look_mode == 1 ? a->look[1] : a->look_mode == 2 ? in->pitch + a->look[1] : in->pitch;
    int turned = 0;
    for (int t = 0; t < 3; ++t)
    {
        double yr = yaw * M_PI / 180.0, pr = pitch * M_PI / 180.0;
        double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
        int hit = -1;
        for (int k = 0; k < en->nender && k < NETH_RL_END_ENDERS && hit < 0; ++k)
        {
            double dx = en->ender[k].x - in->x, dy = en->ender[k].y - (in->y + 1.62), dz = en->ender[k].z - in->z;
            double d = sqrt(dx * dx + dy * dy + dz * dz);
            if (d > 0.0 && d <= 64.0 && (lx * dx + ly * dy + lz * dz) / d > 1.0 - 0.05 / d) hit = k;
        }
        if (hit < 0) break;
        double dy = en->ender[hit].y - (in->y + 1.62);
        pitch = dy > 0.0 ? pitch + 30.0f : pitch - 30.0f;  /* the middle above the eye: look down; else up */
        if (pitch > 90.0f) pitch = 90.0f;
        if (pitch < -90.0f) pitch = -90.0f;
        turned = 1;
    }
    if (turned)
    {
        a->look_mode = 1;
        a->look[0] = yaw;
        a->look[1] = pitch;
    }
    int under = 0;
    for (int k = 0; k < en->nender && k < NETH_RL_END_ENDERS; ++k) under |= s && s->mo_entity == en->ender[k].id;
    if (under)
    {
        a->hold &= ~(1u << PK_ATTACK);
        a->press[PK_ATTACK] = 0;
    }
}

/* lane/dleg (dragon_void_edge sneak), PRIVILEGED (the server's End blocks): in the End, on the ground, with a cell
 * beside the feet's (the eight round it, at the feet's level) open and no block under it within the fall the body
 * survives (health + 2 blocks: a fall's damage is its distance less 3), or open to the void: 1 (vecenv.c holds the
 * sneak then, which keeps a walk from going over the edge; lava_edge's test with the void as the hazard). Traced on the
 * b0 set's End rows: 24 of the 122 void deaths a walk off the island's rim or the bridge with no push and no hit */
static int dragon_void_edge(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (r->dim != 1 || !r->on_ground || r->y < 1.0) return 0;
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    int deep = (int)r->health + 2;
    if (deep > fy) deep = fy;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (!dx && !dz) continue;
            if (!shoracle_open(shoracle_block_dim(v->p, id, 1, fx + dx, fy, fz + dz, NULL))) continue;
            int k = 1;
            while (k <= deep && shoracle_block_dim(v->p, id, 1, fx + dx, fy - k, fz + dz, NULL) == 0) ++k;
            if (k > deep) return 1;
        }
    return 0;
}

static int dragon_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    struct vec_env *e = c->e;
    if (e->kind != M_MINE || !e->dragon) return -1;
    const struct neth_vec *v = c->v;
    if (dg_bridge_step(v, e, c->s, a))
    {
        /* lane/dleg (dragon_ender_avert all): the bridge's walk off the arrival platform looks too (the pit phase's
         * stares were mostly there) */
        if (MOPT(MO_DG_ENDER_AVERT) == 2) dg_stare_break(e, c->s, dragon_view(v, e), a);
        return 0;
    }
    int r = dragon_drive(&e->dg, &e->dgin, c->s, dragon_view(v, e), a);
    if (MOPT(MO_DG_ENDER_AVERT) == 2) dg_stare_break(e, c->s, dragon_view(v, e), a);   /* lane/dleg: all */
    else if (MOPT(MO_DG_ENDER_AVERT)) dg_ender_avert(e, c->s, dragon_view(v, e), a);
    return r;
}

static uint32_t dragon_m_label(const struct vec_env *e) { return e->kind == M_MINE && e->dragon ? NETH_VEC_DRAGON : 0; }

static void dragon_m_end(struct vec_env *e) { e->dragon = 0; }

static void dragon_m_reset(struct vec_env *e) { e->dragon = 0; }     /* (the books' e->dg: the core's reset) */

static const struct mac_switch DRAGON_SW[] = {{"dragon", NETH_VEC_DRAGON}, {NULL, 0}};

static const struct vec_macro MAC_DRAGON = {
    .name = "dragon", .bit = NETH_VEC_DRAGON, .sw = DRAGON_SW, .cand = dragon_m_cand, .start = dragon_m_start, .drive = dragon_m_drive,
    .label = dragon_m_label, .end = dragon_m_end, .reset = dragon_m_reset,
};
