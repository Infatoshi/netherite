/* NETH_VEC_SURFACE: the swim up (lane/macromod: cut from vecenv.c, unchanged) */

#ifdef AR_TRACE
/* lane/flint (a VARIANT trace build, -DAR_TRACE=ENV): the player's place and the client world's blocks round the feet
 * (dx, dz -2..2, dy LO..3 from the feet's cell: rows dz, columns dx), stderr only; the swim's every 5th step (ART), and
 * with -DAR_GRID_T0=A -DAR_GRID_T1=B every step from A to B (GRD, vecenv.c decode) */
static void surface_grid(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, const char *tag, int lo)
{
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z), em = -1;
    int eb = obsview_block(v->p, (int)(e - v->e), fx, (int)floor(r->y + 1.62), fz, &em);
    fprintf(stderr, "%s %lld pos %.3f %.3f %.3f yaw %.1f pitch %.1f air %d hp %.1f under %d gr %d mo %d %d %d %d "
            "lq %d %d %d %d kind %d eye %d:%d |", tag, (long long)e->steps, r->x, r->y, r->z, (double)r->yaw,
            (double)r->pitch, (int)r->air, (double)r->health, s->eye_water != 0, r->on_ground,
            s->mo_hit ? s->mo_block : -1, s->mo_x, s->mo_y, s->mo_z, s->lq_hit ? s->lq_block : -1, s->lq_x, s->lq_y,
            s->lq_z, (int)e->kind, eb, em);
    for (int dy = lo; dy <= 3; ++dy)
    {
        fprintf(stderr, " y%+d:", dy);
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx)
                fprintf(stderr, "%s%d", dx == -2 && dz > -2 ? "/" : dx > -2 ? "," : "",
                        obsview_block(v->p, (int)(e - v->e), fx + dx, fy + dy, fz + dz, NULL));
    }
    fprintf(stderr, "\n");
}
#endif

/* the surface macro's swim (M_SURFACE without the escape), its age already counted: 1 when it is over. lane/ladder9:
 * also the surface labeller's act (shadow.c shd_surface) */
static int surface_swim(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s, struct neth_rl_act *a)
{
    /* lane/strip3 (NETH_VEC_STRIP2): an open screen closed first (no key moves the player under it: traced, s9
     * env 9 crafted at a table placed with its head in water, the swim held jump under the crafting screen for
     * 300 steps and drowned with air a block over the head; 14 of the round's first deaths were drowning) */
    if ((v->macros & NETH_VEC_STRIP2) && r->screen != PS_NONE && r->screen != PS_GAMEOVER)
    {
        a->nops = 1;
        a->ops[0] = CLOSE;
        return e->age > 20;
    }
    int under = s->eye_water != 0;
#ifdef AR_TRACE
    if ((int)(e - v->e) == AR_TRACE && e->steps % 5 == 0) surface_grid(v, e, r, s, "ART", -1);
#endif
    if (!under && r->air >= 270) return 1;
    a->hold = (a->hold | (1u << PK_JUMP)) & ~(1u << PK_SNEAK);
    if (under)
    {
        a->hold = (a->hold | (1u << PK_FORWARD) | (1u << PK_SPRINT)) & ~(1u << PK_BACK);
        a->look_mode = 2;
        double m = -60.0 - (double)r->pitch;
        a->look[1] = r->pitch > -60 ? (float)(-10.0 > m ? -10.0 : m) : 0.0f;
        /* lane/obsidian7 (surface_ceiling dig): the look up on a solid block over the head within reach (a flooded
         * pit under a grass ceiling: traced, s42's diamond pickaxe start, the swim held jump into it 300 steps and
         * drowned): dug with the best tool, no walk */
        if (MOPT(MO_SURF_CEIL) && r->pitch <= -45.0f && s->mo_hit && s->mo_y > (int)floor(r->y + 1.62) &&
            s->mo_block > 0 && !(s->mo_block >= 8 && s->mo_block <= 11) && s->mo_block != 7 &&
            py_dist3(r->x, r->y + 1.62, r->z, s->mo_x + 0.5, s->mo_y + 0.5, s->mo_z + 0.5) <= 3.0)
        {
            a->hold = (1u << PK_ATTACK) | (1u << PK_JUMP);
            int best = best_tool(&v->T, s, s->mo_block);
            if (best >= 0 && best != s->current) a->hotbar = best;
        }
    }
    return e->age > 400;
}


/* lane/flint (air_rescue pin): the head against a ceiling: a solid block (ob_solid) in the cell over the body's top in
 * any column the 0.6-wide body stands in (the player's own cells, the client world). Traced on the flint starts' rows
 * (b0- set): the strip's climb dug up into water, the rescue swam up, the eye came out of the water with the feet
 * still in it and the head under a block (y n.2: the ceiling at n + 2.0, the body 1.8 tall); held jump never let the
 * feet reach the ground, so the rescue's release (out of water, air restored, on the ground) never came and it held
 * every later step: 99 of the seven starts' 255 limits ended so (s1cont150 38 of 56) */
#define SURF_PIN_HALF 0.299     /* the body's half width, a hair inside (a column touched only at its face is not under) */
#define SURF_PIN_TOP 1.85       /* over the feet: the body's top (1.8) and the jump's lift against the ceiling */
static int surface_ceiled(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, double top)
{
    int id = (int)(e - v->e), y = (int)floor(r->y + top);
    for (int i = 0; i < 4; ++i)
    {
        int x = (int)floor(r->x + ((i & 1) ? SURF_PIN_HALF : -SURF_PIN_HALF));
        int z = (int)floor(r->z + ((i & 2) ? SURF_PIN_HALF : -SURF_PIN_HALF));
        if (ob_solid(obsview_block(v->p, id, x, y, z, NULL))) return 1;
    }
    return 0;
}
static int surface_pinned(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    return surface_ceiled(v, e, r, SURF_PIN_TOP);
}

/* lane/flint (air_rescue pocket): under water with the head against a ceiling (surface_pinned), the walk into a column
 * beside (the four faces) where a body fits with its eye in air: the feet's cell air or still or flowing water, the cell
 * over it air, at the feet's level or one lower (the own column's cell under the feet open too: the body sinks into it
 * with the jump let go). Traced on the pin- set's rows, s2stock10 attempts 2, 15, 20 and 23: a lake over the strip's
 * level flooded its one-wide shafts, the rescue swam up a shaft into the dirt under the lake bed and drowned pinned there
 * (25 of the start's 36 deaths, one site, x 111 to 122) with an air pocket a block aside and a block down (attempt 2:
 * (112, 56, 160) air over flowing water at (112, 55, 160) from step 900 to its death at 1,253); 1 when the act was set */
#define SURF_POCKET_MID 0.1
static int surface_open(int b) { return b == 0 || b == 8 || b == 9; }
static int surface_pocket(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a)
{
    /* the ceiling at the head or within a block over it (traced, attempt 2 gated on the head against it: the sink took
     * the head 0.1 off it, the swim's held jump took the body back up, and it bobbed there to its death) */
    if (!s->eye_water || !(surface_pinned(v, e, r) || surface_ceiled(v, e, r, SURF_PIN_TOP + 1.0))) return 0;
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    for (int dy = 0; dy >= -1; --dy)
    {
        if (dy < 0 && !surface_open(obsview_block(v->p, id, fx, fy - 1, fz, NULL))) break;
        for (int i = 0; i < 4; ++i)
        {
            int x = fx + (i == 0) - (i == 1), z = fz + (i == 2) - (i == 3), y = fy + dy;
            if (!surface_open(obsview_block(v->p, id, x, y, z, NULL)) || obsview_block(v->p, id, x, y + 1, z, NULL) != 0)
                continue;
            double yaw = r->yaw, pitch = 0.0;
            look_at(r->x, r->y + 1.62, r->z, x + 0.5, r->y + 1.62, z + 0.5, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw;
            a->look[1] = 0.0f;
            /* the walk only once the body fits under the column's next cell up (traced, attempt 2 with the walk at once:
             * the forward into the wall over the pocket is the water's climb, motionY 0.3 on a horizontal collision,
             * and the body hovered at y 56.1 for 300 steps): no key held, the body sinks, till then */
            if (r->y <= y + 0.2 || surface_open(obsview_block(v->p, id, x, y + 2, z, NULL)))
                a->hold = 1u << PK_FORWARD;
            /* ... and the sink off the walls (traced, attempt 2 with no key: the water's flow held the body against the
             * shaft's sand wall, a collision every tick, and the climb kept it at y 56.1): the walk to the own column's
             * middle while it is more than SURF_POCKET_MID off it */
            else if (fabs(r->x - (fx + 0.5)) > SURF_POCKET_MID || fabs(r->z - (fz + 0.5)) > SURF_POCKET_MID)
            {
                look_at(r->x, r->y + 1.62, r->z, fx + 0.5, r->y + 1.62, fz + 0.5, &yaw, &pitch);
                a->look[0] = (float)yaw;
                a->hold = 1u << PK_FORWARD;
            }
#ifdef AR_TRACE
            if (id == AR_TRACE)
                fprintf(stderr, "ARP %lld pos %.3f %.3f %.3f to %d %d %d hold %u under %d gr %d own %d %d %d %d\n",
                        (long long)e->steps, r->x, r->y, r->z, x, y, z, a->hold, s->eye_water, r->on_ground,
                        obsview_block(v->p, id, fx, fy - 1, fz, NULL), obsview_block(v->p, id, fx, fy, fz, NULL),
                        obsview_block(v->p, id, fx, fy + 1, fz, NULL), obsview_block(v->p, id, fx, fy + 2, fz, NULL));
#endif
            return 1;
        }
    }
    return 0;
}

/* lane/flint (lava_edge sneak): on the ground with a cell beside the feet's (the eight round it and its own, at the
 * feet's level) with lava for its floor or under open air within SURF_EDGE_DEEP: 1 (vecenv.c holds the sneak on
 * any act then, which in 1.7.10 keeps a walk from going over an edge). Traced on the sp- set's rows: 38 of the 56 fire
 * deaths began with a fall (the body 0.9 or more lower at the first burn than in the 40 steps before), under every owner
 * (food 9, fight 8, strip 7, craft 7, mine 7); s42k20 attempt 44: the policy's turns put the body's middle over the hole
 * at (-84, 11, -167), the strip's next set took that cell for its feet and walked on, into lava at y 10 */
#define SURF_EDGE_DEEP 3
static int surface_lava_edge(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (!r->on_ground) return 0;
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            for (int k = 1; k <= SURF_EDGE_DEEP; ++k)
            {
                int b = obsview_block(v->p, id, fx + dx, fy - k, fz + dz, NULL);
                if (b == 10 || b == 11) return 1;   /* lava at the floor's level or under open air: no collision box */
                if (b != 0) break;
            }
    return 0;
}

/* lane/flint (water_exit step): the eye in water, a face neighbour's column where the body stands on a solid floor with
 * its eye in air, at the feet's level or one up (the own column's cell over the head open for the jump): the look along
 * it, forward, and the jump for the step up; 1 when the act was set. Traced on the ot- set's rows (s1cont150, 50 of 60
 * limits with no macro, one site): attempt 0 at the bottom of a one-wide hole in a tunnel's floor, water falling into
 * it, every macro's start refusing with the eye in water and the controller's swim jumping into the hole's wall at its
 * yaw, the tunnel's floor a block up beside it on two sides */
static int surface_water_exit(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                              const struct neth_rl_state *s, struct neth_rl_act *a)
{
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    /* ... or the feet in water off the ground (traced, attempt 0 with the eye's test alone: in a hole under a pond's
     * floor the jump took the eye over the pond's surface, the step let go there, and the body sank back, a loop) */
    int fb = obsview_block(v->p, id, fx, fy, fz, NULL);
    if (!s->eye_water && !((fb == 8 || fb == 9) && !r->on_ground)) return 0;
    int room = !ob_solid(obsview_block(v->p, id, fx, fy + 2, fz, NULL));
    for (int dy = 1; dy >= 0; --dy)
    {
        if (dy > 0 && !room) continue;
        for (int i = 0; i < 4; ++i)
        {
            int x = fx + (i == 0) - (i == 1), z = fz + (i == 2) - (i == 3), y = fy + dy;
            int fl = obsview_block(v->p, id, x, y - 1, z, NULL), ft = obsview_block(v->p, id, x, y, z, NULL);
            if (!ob_solid(fl) || ob_solid(ft) || (ft >= 10 && ft <= 11) || obsview_block(v->p, id, x, y + 1, z, NULL))
                continue;
            double yaw = r->yaw, pitch = 0.0;
            look_at(r->x, r->y + 1.62, r->z, x + 0.5, r->y + 1.62, z + 0.5, &yaw, &pitch);
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw;
            a->look[1] = 0.0f;
            a->hold = (1u << PK_FORWARD) | (dy > 0 ? 1u << PK_JUMP : 0u);
            return 1;
        }
    }
    return 0;
}

/* lane/flint (inwall dig): the eye's cell holding a solid block (what suffocates: the screen shows the block's inside)
 * is dug with the best tool held, the look on the cell's middle (straight up when the eye is within SURF_INWALL_MID of
 * it across); 1 when the act was set. Traced on the b0- set: 24 of 175 deaths were the in-wall drain (1 a 10 ticks to
 * death, no attacker), the obsidian macro's climb (OJ_CLIMB) and the strip's climb digging under gravel that fell into
 * the head's cell, the macro standing on (s42k50 attempt 26: gravel at the head from step 3,617, dead at 3,828) */
#define SURF_INWALL_MID 0.15
#define SURF_INWALL_HALF 0.24   /* Entity.isEntityInsideOpaqueBlock's points: the eye +-0.4 of the 0.6 width, +-0.05 up */
static int surface_inwall(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a)
{
    double ey = r->y + 1.62;
    int id = (int)(e - v->e), x = 0, y = 0, z = 0, b = 0, hit = 0;
    for (int i = 0; i < 8 && !hit; ++i)
    {
        x = (int)floor(r->x + ((i & 1) ? SURF_INWALL_HALF : -SURF_INWALL_HALF));
        y = (int)floor(ey + ((i & 2) ? 0.05 : -0.05));
        z = (int)floor(r->z + ((i & 4) ? SURF_INWALL_HALF : -SURF_INWALL_HALF));
        b = obsview_block(v->p, id, x, y, z, NULL);
        /* the full cubes the underground has (isNormalCube): stone, grass, dirt, cobble, sand, gravel, the ores,
         * sandstone, mossy cobble, clay, netherrack; not bedrock, not obsidian (the frame's) */
        hit = (b >= 1 && b <= 4) || b == 12 || b == 13 || (b >= 14 && b <= 16) || b == 21 || b == 24 || b == 48 ||
              b == 56 || b == 73 || b == 74 || b == 82 || b == 87;
    }
    if (!hit) return 0;
    still(a);
    double yaw = r->yaw, pitch = -90.0;
    if (fabs(r->x - (x + 0.5)) > SURF_INWALL_MID || fabs(r->z - (z + 0.5)) > SURF_INWALL_MID)
        look_at(r->x, ey, r->z, x + 0.5, y + 0.5, z + 0.5, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    a->hold = 1u << PK_ATTACK;
    int best = best_tool(&v->T, s, b);
    if (best >= 0 && best != s->current) a->hotbar = best;
    return 1;
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static int surface_m_cand(struct mac_step *c)
{
    return (c->v->macros & NETH_VEC_SURFACE) && c->s->eye_water != 0 && c->r->air < 120;
}

static int surface_m_start(struct mac_step *c)
{
    if ((c->v->macros & NETH_VEC_SURFACE) && c->s->eye_water && c->r->air < 120)
    {
        c->e->kind = M_SURFACE;
        return 1;
    }
    return 0;
}

/* the swim (the lava escape, the same kind, is the strip's and the Nether's seal before it, in drive order) */
static int surface_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_SURFACE) return -1;
    return surface_swim(c->v, c->e, c->r, c->s, a);
}

static uint32_t surface_m_label(const struct vec_env *e) { return e->kind == M_SURFACE ? NETH_VEC_SURFACE : 0; }

static const struct mac_switch SURFACE_SW[] = {{"surface", NETH_VEC_SURFACE}, {NULL, 0}};

static const struct vec_macro MAC_SURFACE = {
    .name = "surface", .bit = NETH_VEC_SURFACE, .sw = SURFACE_SW, .cand = surface_m_cand, .start = surface_m_start,
    .drive = surface_m_drive, .label = surface_m_label,
};
