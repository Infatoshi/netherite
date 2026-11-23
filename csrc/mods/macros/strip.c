/* NETH_VEC_STRIP: the strip mine and the lava escape (lane/macromod: cut from vecenv.c, unchanged; NETH_VEC_STRIP2's errands are strip2.c) */

#define STRIP_SPIN_AGE 120      /* lane/flint (strip_spin water): steps of a run with no cell walked (a set takes about
                                   10 to 20) after which water ahead no longer turns the strip */


/* lane/lavaesc (NETH_VEC_STRIP): 1 when the crosshair is on the block under the feet below y 16, where caves hold lava
 * (y 10 and down): the seek and the mine lock dug such shafts and fell 2 blocks into lava (6 of 31 deaths in 26 of 128
 * recorded 20-minute runs of s64-w2-it55) */
static int dig_under(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    if (!((v->macros & NETH_VEC_STRIP) && r->y < 16.0 && s->mo_hit)) return 0;
    int fy = (int)floor(r->y);
    if (s->mo_x == (int)floor(r->x) && s->mo_z == (int)floor(r->z) && s->mo_y == fy - 1) return 1;
    /* lane/strip2: nor a block two or more under the feet anywhere: the mine lock dug a pit beside the feet at y 12
     * (11, 10, 9), the player dropped in and the strip ran on at y 9, under the lava lakes' level (the case's first
     * deaths: 21 lava of 23) */
    return (v->macros & NETH_VEC_STRIP2) && s->mo_y <= fy - 2;
}

/* lane/lavaesc: the escape from lava: back to the newest cell stood on out of fire that is not the feet's, walking at
 * it and jumping (a lava pool's edge is a block up), the screen closed first; done on the ground with no lava hit for
 * 12 steps (lava hurts every 10), or a cell given up after 60 steps (the next older one next time). The macros had dug
 * on in lava that flowed into the tunnel (9 of 31 deaths) */
/* lane/flint (lava_escape seal): traced on the base's rows (fes-, FIRE_TRACE's ESC line): of 40 fire deaths that had
 * poured their water, the escape's target (the last cell stood on) was over the feet in 39 (their own column 1 to 11 up
 * in 31), forward pushed at the middle of the feet's own cell, the body rose at most 1.36, and the head's cell was lava
 * in 33 (1.7.10's liquid lift needs the box raised 0.6 clear of any liquid); cobblestone held in all 40. So: the lava
 * against the body sealed first, then a standable cell beside aimed at, else the pillar */
#define ESC_SEAL_TRIES 8        /* steps one cell's placement may take */
#define ESC_SEAL_AGE 30         /* the escape's age the sealing stops at */
#define ESC_HALF 0.3

static int esc_lava(int b) { return b == 10 || b == 11; }

/* the cell (x, y, z) within the body's box */
static int esc_in_body(const struct vec_last *r, int x, int y, int z)
{
    return x >= (int)floor(r->x - ESC_HALF) && x <= (int)floor(r->x + ESC_HALF) && z >= (int)floor(r->z - ESC_HALF) &&
           z <= (int)floor(r->z + ESC_HALF) && y >= (int)floor(r->y) && y <= (int)floor(r->y + 1.8);
}

/* a block from hotbar slot hb placed into cell (cx, cy, cz): the crosshair on a face of a solid block bordering it that
 * the eye sees (liquids are not hit), USE pressed when it is on it. 1 the act set, 0 no face */
static int esc_place(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_state *s, struct neth_rl_act *a, int hb, int cx, int cy, int cz)
{
    static const int OX[6] = {0, 0, 0, 0, -1, 1}, OY[6] = {-1, 1, 0, 0, 0, 0}, OZ[6] = {0, 0, -1, 1, 0, 0};
    static const int D[6][3] = {{0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    int id = (int)(e - v->e);
    double ex = r->x, ey = r->y + 1.62, ez = r->z;
    for (int k = 0; k < 6; ++k)
    {
        int dx = D[k][0], dy = D[k][1], dz = D[k][2];
        if (!ob_solid(obsview_block(v->p, id, cx + dx, cy + dy, cz + dz, NULL))) continue;
        /* the face toward the cell, seen from the eye's side of its plane */
        if ((dy < 0 && ey <= cy) || (dy > 0 && ey >= cy + 1) || (dx > 0 && ex >= cx + 1) || (dx < 0 && ex <= cx) ||
            (dz > 0 && ez >= cz + 1) || (dz < 0 && ez <= cz))
            continue;
        double tx = cx + 0.5 + 0.5 * dx, ty = cy + 0.5 + 0.5 * dy, tz = cz + 0.5 + 0.5 * dz, yaw, pitch;
        if (py_dist3(ex, ey, ez, tx, ty, tz) > 4.4) continue;
        look_at(ex, ey, ez, tx, ty, tz, &yaw, &pitch);
        a->hotbar = hb;
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        int side = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
        if (s->mo_hit && s->current == hb && s->mo_x + OX[side] == cx && s->mo_y + OY[side] == cy &&
            s->mo_z + OZ[side] == cz)
        {
            a->look[0] = r->yaw, a->look[1] = r->pitch;
            a->press[PK_USE] = 1;
        }
        return 1;
    }
    return 0;
}

/* the seal: the first lava cell against the body a block may go into (over the head, then beside the head, then beside
 * the feet), placed into; 1 the act set */
static int esc_seal(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, int hb)
{
    if (e->esl_done || e->age > ESC_SEAL_AGE) return 0;
    int id = (int)(e - v->e), x0 = (int)floor(r->x - ESC_HALF), x1 = (int)floor(r->x + ESC_HALF);
    int z0 = (int)floor(r->z - ESC_HALF), z1 = (int)floor(r->z + ESC_HALF), fy = (int)floor(r->y),
        hy = (int)floor(r->y + 1.8);
    for (int pass = 0; pass < 3; ++pass)
    {
        int y = pass == 0 ? hy + 1 : pass == 1 ? hy : fy;
        for (int x = x0 - (pass > 0); x <= x1 + (pass > 0); ++x)
            for (int z = z0 - (pass > 0); z <= z1 + (pass > 0); ++z)
            {
                if (pass > 0 && (x < x0 || x > x1) == (z < z0 || z > z1)) continue;   /* the face cells only */
                if (esc_in_body(r, x, y, z) || !esc_lava(obsview_block(v->p, id, x, y, z, NULL))) continue;
                if (e->esl[0] != x || e->esl[1] != y || e->esl[2] != z)
                    e->esl[0] = x, e->esl[1] = y, e->esl[2] = z, e->esl_n = 0;
                if (!esc_place(v, e, r, s, a, hb, x, y, z)) continue;          /* no face for it: the next */
                /* one cell's tries spent: the sealing over (it would come back to the same cell first) */
                if (++e->esl_n > ESC_SEAL_TRIES) goto done;
                a->hold |= 1u << PK_JUMP;
                return 1;
            }
    }
done:
    still(a);
    e->esl_done = e->age > 0 ? e->age : 1;
    return 0;
}

/* a standable face neighbour (a solid floor, its cell and the one over it clear of blocks and lava) at the feet's level
 * or one up (then the own column's cell over the head clear for the jump): 1 with its cell */
static int esc_stand(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, int *ox, int *oy,
                     int *oz)
{
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    for (int dy = 0; dy <= 1; ++dy)
    {
        if (dy > 0 && ob_solid(obsview_block(v->p, id, fx, fy + 2, fz, NULL))) continue;
        for (int i = 0; i < 4; ++i)
        {
            int x = fx + (i == 0) - (i == 1), z = fz + (i == 2) - (i == 3), y = fy + dy;
            int fl = obsview_block(v->p, id, x, y - 1, z, NULL), c0 = obsview_block(v->p, id, x, y, z, NULL),
                c1 = obsview_block(v->p, id, x, y + 1, z, NULL);
            if (!ob_solid(fl) || ob_solid(c0) || esc_lava(c0) || ob_solid(c1) || esc_lava(c1)) continue;
            *ox = x, *oy = y, *oz = z;
            return 1;
        }
    }
    return 0;
}

/* the pillar: looking down, the jump held, a block placed into the cell under the feet once the body is over it (its
 * own floor solid) */
static int esc_pillar(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s, struct neth_rl_act *a, int hb)
{
    int id = (int)(e - v->e), fx = (int)floor(r->x), fz = (int)floor(r->z), q = (int)floor(r->y) - 1;
    int b = obsview_block(v->p, id, fx, q, fz, NULL);
    a->hold = 1u << PK_JUMP;
    a->look_mode = 1;
    a->look[0] = r->yaw, a->look[1] = 90.0f;
    if (!ob_solid(b) && ob_solid(obsview_block(v->p, id, fx, q - 1, fz, NULL)) && !esc_in_body(r, fx, q, fz))
        esc_place(v, e, r, s, a, hb, fx, q, fz);
    a->hold |= 1u << PK_JUMP;
    return 1;
}

static int escape(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                  struct neth_rl_act *a)
{
    still(a);
    if (r->screen != PS_NONE)
    {
        a->nops = 1;
        a->ops[0] = CLOSE;
        return e->age > 20;
    }
    int fx = (int)floor(r->x), fy = (int)floor(r->y), fz = (int)floor(r->z), in_lava = e->steps - e->lava_t <= 11;
    while (e->nsafe > 0 && e->safe[e->nsafe - 1][0] == fx && e->safe[e->nsafe - 1][1] == fy &&
           e->safe[e->nsafe - 1][2] == fz && in_lava)
        e->nsafe -= 1;
    if (!in_lava && r->on_ground) return 1;
    if (MOPT(MO_LAVA_ESCAPE) && r->dim == 0 && e->age <= 60)
    {
        int hb = nt_fill_slot(v, s);
        if (hb >= 0 && esc_seal(v, e, r, s, a, hb)) return 0;
        /* the target: the last cell stood on when beside the feet and at most one up, else a standable cell beside */
        const int *c = e->nsafe > 0 ? e->safe[e->nsafe - 1] : NULL;
        int tx, ty, tz;
        if (c && abs(c[0] - fx) + abs(c[2] - fz) == 1 && c[1] - fy <= 1 && c[1] - fy >= 0)
            tx = c[0], ty = c[1], tz = c[2];
        else if (!esc_stand(v, e, r, &tx, &ty, &tz))
        {
            if (hb >= 0) return esc_pillar(v, e, r, s, a, hb), 0;
            tx = c ? c[0] : fx, ty = c ? c[1] : fy, tz = c ? c[2] : fz;
        }
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, tx + 0.5, ty + 1.0, tz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw;
        a->look[1] = 0.0f;
        a->hold = (1u << PK_FORWARD) | (1u << PK_JUMP);
        return 0;
    }
    if (e->nsafe == 0 || e->age > 60)
    {
        if (e->nsafe > 0) e->nsafe -= 1;
        e->esc_off = e->steps + 20;
        return 1;
    }
    const int *c = e->safe[e->nsafe - 1];
    double yaw, pitch;
    look_at(r->x, r->y + 1.62, r->z, c[0] + 0.5, c[1] + 1.0, c[2] + 0.5, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = 0.0f;
    a->hold = (1u << PK_FORWARD) | (1u << PK_JUMP);
    return 0;
}


/* NETH_VEC_STRIP: 1 when the strip mine is wanted: a stone pickaxe or better, iron still needed (seek_want's count) or
 * diamonds with an iron pickaxe, in the overworld's ground above bedrock, the eye out of water */
static int strip_want0(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s)
{
    if (r->dim != 0 || r->screen != PS_NONE || s->eye_water || r->y < 4.0 || e->steps < e->soff) return 0;
    if (sh_owns(v, e, r)) return 0;     /* lane/stronghold */
    if (pg_surface(v, r, s)) return 0;   /* lane/pearlgo: the hunt first (pearl_night_first, macros.c) */
    if (prov_want(v, e, r, s)) return 0; /* lane/chain: food_oracle's provisions first (food.c) */
    if ((v->macros & NETH_VEC_OBSIDIAN) && ob_search(v, (struct vec_env *)e, r, s)) return 1;   /* lane/obsidian */
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    if (tier < 2) return 0;
    /* a table held, or 6 planks for one and for fuel: the furnace and the pickaxe are made underground (17% of runs
     * held iron ore and could make no furnace, their table left at the surface) */
    /* lane/strip2: 4 planks (a table made again when a plan needs one: the furnace's table was left behind and 5 planks
     * held, the strip never began again) */
    int s2 = (v->macros & NETH_VEC_STRIP2) != 0;
    if (inv_count(s, TABLE) == 0 && inv_count(s, PLANKS) < (s2 ? 4 : 6) && !(s2 && tier >= 3)) return 0;
    /* (lane/strip2: no table needed with an iron pickaxe held: the diamonds are dug first, the table after; env 20
     * held 3 planks and its table was left behind at y 36, and never dug again) */
    int64_t need = (inv_count(s, IRON_PICK) + inv_count(s, DIAMOND_PICK) == 0 ? 3 : 0) +
                   (inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) + inv_count(s, LAVA_BUCKET) == 0 ? 3 : 0) +
                   (inv_count(s, FLINT_STEEL) == 0 ? 1 : 0);
    if (need > inv_count(s, IRON_ORE) + inv_count(s, INGOT)) return 1;
    return tier >= 3 && inv_count(s, DIAMOND_PICK) == 0 && inv_count(s, DIAMOND) < 3;
}

/* lane/strip3: the strip's own want, or the climb's (strip2.c st3_want: the wood errand, out of the depth) */
static int strip_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s)
{
    if (shore_night_yield(v, e, r, s)) return 0;
    int w = strip_want0(v, e, r, s);
    /* lane/obsid2: obsidian_yield on; lane/bportal (obsidian_yield_live on): only while the obsidian macro is wanted
     * (the yield is the last ob_search's, and strip_want0 stops calling it once the provisions are wanted) */
    if ((v->macros & NETH_VEC_OBSIDIAN) && e->ob.yield && (!MOPT(MO_OB_YIELD_LIVE) || ob_want(v, e, r, s)))
        return 0;
    return w || ((v->macros & NETH_VEC_STRIP2) && st3_want(v, e, r, s, 0));
}

/* NETH_VEC_STRIP's drive (lane/strip): the macro's return. lane/craftfix: an absolute look only where one is
 * computed (on the step it ended, and the step it entered a cell, the policy's look deltas were taken for the
 * absolute yaw and pitch), and the seek taken over on the spot: the strip had ended for it, the look thrown off
 * the ore by those deltas, the seek's start (every 5th step) seeing none, the strip begun again, its look at the
 * next cell seeing the ore, ended again: the look flipping every step, the player still, for the rest of the run
 * (14 of 17 runs of 128 that spent 20 minutes in the mine macro holding no iron) */
static int strip(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                 struct neth_rl_act *a, double ey)
{
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
    still(a);
    e->st_steps += 1;                                /* lane/strip2's books */
    int s2 = (v->macros & NETH_VEC_STRIP2) != 0;     /* lane/strip2 (strip2.c) */
    if (s2 && e->stf) return st_errand(v, e, r, s, a);
    if (e->age > 300 || r->screen != PS_NONE || s->eye_water || !strip_want(v, e, r, s)) return 1;
    int fid = 0;
    if (s2 && st3_guard(e, r, s, a, &fid))            /* lane/strip3: the guard (strip2.c) */
    {
        if (!fid)
        {
            e->st3_back = e->steps;                   /* the look back: what hurt comes into view */
            return 0;
        }
        e->strip = 0;
        e->fid = fid;
        e->flost = 0;
        e->flast = -100;
        e->age = 1;
        return fight(v, e, r, s, a, ey);
    }
    int m3 = s2 ? st3_want(v, e, r, s, strip_want0(v, e, r, s)) : 0;   /* lane/strip3: the climb (strip2.c) */
    if (s2) st3_step(e, m3);
    if (m3) e->st3_climb_at = e->steps;              /* lane/diamond: climb_swim's clock */
    int q;
    if (!provision_mode(v, e, r, s) && (v->macros & NETH_VEC_SEEK) && e->age > 1 &&
        (q = seek_want(v, e, s)) >= 0)
    {
        e->strip = 0;
        e->age = 1;
        seek_begin(e, r, s, q);
        return seek(v, e, r, s, a, ey);
    }
    if (!(provision_mode(v, e, r, s) && provision_under(v, e, r)) &&
        (v->macros & NETH_VEC_CRAFT) && e->age % 5 == 0 && craft_plan(v, e, r, s) >= 0) return 1;
    if (!provision_mode(v, e, r, s) && ob_handoff(v, e, r, s)) return 1; /* the food trip has its own work */
#ifdef ST_DIA_TRACE
#ifndef ST_DIA_TRACE_T0
#define ST_DIA_TRACE_T0 17100
#endif
    if ((int)(e - v->e) == ST_DIA_TRACE &&
        (e->steps % 20 == 0 || (e->steps >= ST_DIA_TRACE_T0 && e->steps < ST_DIA_TRACE_T0 + 60)))
        fprintf(stderr, "TR step %lld y %.4f gr %d m3 %d st3m %d ph %d off %lld age %d sk %d mo %d %d dy %d lq %d "
                "jbase %.4f fail %d qwhy %d %d %d %d %d\n",
                (long long)e->steps, r->y, r->on_ground, m3, e->st3_m, e->cw_ph, (long long)e->cw_off, (int)e->age,
                e->sk, s->mo_hit, s->mo_block, s->mo_y - (int)floor(r->y + 0.01), s->lq_hit ? s->lq_block : -1,
                e->cw_jbase, e->cw_fail, (int)e->cw_qwhy[0], (int)e->cw_qwhy[1], (int)e->cw_qwhy[2],
                (int)e->cw_qwhy[3], (int)e->cw_qwhy[4]);
#endif
    /* lane/diamond: climb_way pillar, both climbs a block dug a level (strip2.c st3_pillar, lane/pearlhunt's) */
    if ((m3 == ST3_OUT || m3 == ST3_WOOD) && MOPT(MO_CLIMB_WAY) == CW_PILLAR && st3_pillar(v, e, r, s, a)) return 0;
    a->look_mode = 1;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    /* a liquid source ahead within 3 blocks, or no progress for 60 steps: a quarter turn, the next cell picked again */
    int liquid = s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 &&
                 py_dist3(r->x, ey, r->z, s->lq_x + 0.5, s->lq_y + 0.5, s->lq_z + 0.5) < 3.0;
    /* lane/flint (strip_spin water): this run has walked no cell in STRIP_SPIN_AGE steps (st_walked's clock): water
     * ahead no longer turns it (traced on s42k20: water in all four directions, a turn every 16 steps, frozen) */
    if (liquid && MOPT(MO_STRIP_SPIN) && s->lq_block <= 9 && e->age > STRIP_SPIN_AGE &&
        e->steps - e->st_last > STRIP_SPIN_AGE)
        liquid = 0;
    if ((v->macros & NETH_VEC_OBSIDIAN) && ob_hot(v, e)) liquid = 1;   /* lane/obsidian2: hot lava ahead in view */
    /* lane/strip2: lava that near (the dig opened it) reverses the strip back along its own tunnel, past lava's flow of
     * about 4 blocks; a quarter turn kept the tunnel beside the flow (the case's first deaths at feet y 12) */
    int back = s2 && s->lq_hit && (s->lq_block == 10 || s->lq_block == 11) &&
               py_dist3(r->x, ey, r->z, s->lq_x + 0.5, s->lq_y + 0.5, s->lq_z + 0.5) < 3.0;
    if (s2 && e->age % ST_HOT_EVERY == 0 && st_flee(v, e, r, ey))   /* lane/strip2: lava in view near: away */
    {
        e->sturns += 1;
        e->sk = -1;
        e->sgain = e->age;
        liquid = back = 0;
    }
    else if (s2 && e->age % ST_HOT_EVERY == 0 && st_hot(v, e, r, ey))
        liquid = 1;                                  /* lane/strip2: lava in view ahead */
    if (liquid || e->age - e->sgain > 60)
    {
        e->sdir = (e->sdir + 1 + back) & 3;
        e->sturns += 1;
        e->sk = -1;
        e->sgain = e->age;
    }
    if (s2 && e->sk < 0 && st_drop_start(v, e, r, s, a)) return 0;   /* lane/strip2: the surplus out */
    if (s2 && e->sk < 0 && !m3 && MOPT(MO_DIA_ORACLE)) st_do_steer(v, e, r, s);   /* lane/chain: diamond_oracle */
    if (s2 && e->sk < 0 && !m3 && (v->macros & NETH_VEC_OBSIDIAN)) ob_wk_steer(v, e, r);   /* lane/flint: walk dig */
    /* the next set: the cell along the direction from where the feet stand (the player moved off it: picked again) */
    if (e->sk < 0 ||
        ((e->sk < 3 || e->sk == 5) && (fx != e->scx - DX[e->sdir] || fz != e->scz - DZ[e->sdir] || fy != e->scy)))
    {
        e->scx = fx + DX[e->sdir];
        e->scz = fz + DZ[e->sdir];
        e->scy = fy;
        e->sk = 0;
        e->slook = e->age;
        e->st3_m = ST3_UP;                            /* lane/strip3: a climb's set tries a stair up first */
        e->st3_fall = 0;
    }
    int down = e->scy > ((v->macros & NETH_VEC_OBSIDIAN) ? ob_strip_y(v, e) : NETH_VEC_STRIP_Y), ncell = down ? 3 : 2;
    int up = s2 && e->scy < NETH_VEC_STRIP_Y;        /* lane/strip2: a stair back up (strip2.c st_up_cell) */
    if (s2 && e->do_on && !m3)                       /* lane/chain: diamond_oracle's level, the ore's (strip2.c) */
        down = e->scy > st_do_level(e, NETH_VEC_STRIP_Y), up = e->scy < st_do_level(e, NETH_VEC_STRIP_Y),
        ncell = down ? 3 : 2;
    /* lane/flint (obsidian_walk dig): the walk's level, one over its target's */
    if (s2 && (v->macros & NETH_VEC_OBSIDIAN) && e->ob.wk_on && !m3)
        down = e->scy > e->ob.wk_y + 1, up = e->scy < e->ob.wk_y + 1, ncell = down ? 3 : 2;
    if (m3) down = e->st3_m == ST3_DOWN, up = e->st3_m == ST3_UP, ncell = down ? 3 : 2;   /* lane/strip3: the set's mode */
    if (up) ncell = 3;
    double yaw, pitch;
    if (e->sk < ncell)
    {
        int cx = e->scx, cy = e->sk == 0 ? e->scy + 1 : e->sk == 1 ? e->scy : e->scy - 1, cz = e->scz;
        if (up) st_up_cell(e, e->sk, &cx, &cy, &cz);
        look_at(r->x, ey, r->z, cx + 0.5, cy + 0.5, cz + 0.5, &yaw, &pitch);
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        if (e->age - e->slook < 2) return 0;          /* the look settles before the crosshair is judged */
        if (s->mo_hit && s->mo_x == cx && s->mo_y == cy && s->mo_z == cz)
        {
            int b = s->mo_block;
            if (b == 7 || b == OBSIDIAN)
            {
                e->sgain = -1000;                     /* bedrock or obsidian: turned next step */
                return 0;
            }
            if (s2 && up && e->sk <= 1 && st3_lakebed(b))
            {
                e->sgain = -1000;                     /* lane/strip3: over the head on a stair up: turned next step */
                return 0;
            }
            if (s2 && (b == GRAVEL || b == 12)) e->st3_fall = e->steps;   /* lane/strip3: a falling block's wait */
            int fetch = 0, best = s2 ? st_tool(v, e, s, b, &fetch) : best_tool(&v->T, s, b);
            if (fetch)
            {
                st_begin(e, a, ST_FETCH);             /* lane/strip2: the pickaxe from the main inventory first */
                return 0;
            }
            a->hold = 1u << PK_ATTACK;
            if (best >= 0 && best != s->current) a->hotbar = best;
            return 0;
        }
        /* the crosshair passes the cell: clear */
        e->sk += 1;
        e->slook = e->age;
        e->sgain = e->age;
        if (e->sk == ncell) e->sk = 5;
        /* lane/strip3: a falling block dug in the set: the cells judged again until it has had time to land */
        if (s2 && e->sk == 5 && e->st3_fall > 0 && e->steps - e->st3_fall < ST3_FALL_WAIT) e->sk = 0;
        return 0;
    }
    /* lane/stripsafe: the floor the feet will stand on judged before the walk: the crosshair on it (a block there) and
     * no liquid in the column the ray crosses to it, else a quarter turn; four in a row with no cell walked into, the
     * strip off for 400 steps (the policy's to move). The walk had stepped over caves: of 9 deaths in 8 runs of 128,
     * 5 were a fall of about two blocks from the tunnel into lava, burned 40 steps later */
    if (e->sk == 6)
    {
        /* lane/obsidian7 (obsidian_drop on): the floor's deeper look, in phases (e->ob.drop_ph): 0 crouched to the
         * edge (the crouch never walks off it: from the cell's middle the look down met the player's own floor, traced
         * at s42's pillar, pitch 81); 1 the look down the column from past the edge: a block met in the column at most
         * OB_DROP_DEEP under the floor's (a fall over three blocks only at OB_DROP_HP health or more), no liquid in it,
         * no lava at the landing: 2, walked off uncrouched until the feet are lower; else 3, crouched back into the
         * player's own cell (hanging over the edge, the next set took the hole's column for the feet's: traced, a fall
         * of seven), then the quarter turn as before */
        int fl = (down ? e->scy - 2 : up ? e->scy : e->scy - 1), lo = fl - OB_DROP_DEEP;
        double ex = e->scx + 0.5 - 0.5 * DX[e->sdir], ez = e->scz + 0.5 - 0.5 * DZ[e->sdir];
        double left = DX[e->sdir] ? (ex - r->x) * DX[e->sdir] : (ez - r->z) * DZ[e->sdir];   /* < 0: past the edge */
        int t = (int)(e->age - e->ob.drop_look);
        e->sgain = e->age;
        look_at(r->x, ey, r->z, e->scx + 0.5, e->ob.drop_ph == 1 ? lo + 0.5 : ey - 0.3, e->scz + 0.5, &yaw, &pitch);
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        if (e->ob.drop_ph == 0)
        {
            a->hold = (1u << PK_FORWARD) | (1u << PK_SNEAK);
            if (left <= -OB_DROP_EDGE || t >= OB_DROP_CREEP) e->ob.drop_ph = 1, e->ob.drop_look = e->age;
            return 0;
        }
        if (e->ob.drop_ph == 1)
        {
            a->hold = 1u << PK_SNEAK;
            if (t < 2) return 0;
            int deep = s->mo_hit && s->mo_x == e->scx && s->mo_z == e->scz && s->mo_y >= lo && s->mo_y < fl &&
                       !(s->mo_block >= 8 && s->mo_block <= 11);
            int wet = s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 && s->lq_x == e->scx && s->lq_z == e->scz &&
                      s->lq_y >= lo;
            int fall = e->scy - (s->mo_y + 1);
            if (deep && !wet && !ob_drop_lava(v, e, e->scx, s->mo_y, e->scz) && (fall <= 3 || r->health >= OB_DROP_HP))
            {
                e->ob.drops += 1;
                e->ob.drop_ph = 2;
            }
            else
                e->ob.drop_ph = 3;
            e->ob.drop_look = e->age;
            return 0;
        }
        if (e->ob.drop_ph == 2)
        {
            a->hold = 1u << PK_FORWARD;
            if ((r->on_ground && r->y < e->scy - 0.5) || t >= OB_DROP_CREEP)
            {
                e->sk = -1;
                e->shaz = 0;
                a->hold = 0;
            }
            return 0;
        }
        a->hold = (1u << PK_BACK) | (1u << PK_SNEAK);
        if (left < 0.3 && t < OB_DROP_CREEP) return 0;
        a->hold = 0;
        if (++e->shaz >= 4)
        {
            e->shaz = 0;
            e->soff = e->steps + 400;
            return 1;
        }
        e->sdir = (e->sdir + 1) & 3;
        e->sturns += 1;
        e->sk = -1;
        e->sgain = e->age;
        return 0;
    }
    if (e->sk == 5)
    {
        int fl = down ? e->scy - 2 : up ? e->scy : e->scy - 1;
        double flx = e->scx + 0.5, fly = fl + 0.5, flz = e->scz + 0.5;
        if (s2 && (down || up)) st_floor_point(e, fl, &flx, &fly, &flz);   /* lane/strip2: over the near edge */
        look_at(r->x, ey, r->z, flx, fly, flz, &yaw, &pitch);
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        if (e->age - e->slook < 2) return 0;
        int solid = s->mo_hit && s->mo_x == e->scx && s->mo_y == fl && s->mo_z == e->scz;
        int wet = s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 && s->lq_x == e->scx && s->lq_z == e->scz &&
                  s->lq_y >= fl && s->lq_y <= e->scy + 1 + up;
        if (solid && !wet)
        {
            e->sk = 3;
            e->slook = e->age;
            return 0;
        }
        /* lane/obsidian7 (obsidian_drop on): the search's strip, no floor and no liquid: the deeper look (sk 6) */
        if (!wet && !solid && (v->macros & NETH_VEC_OBSIDIAN) && ob_drop_ok(v, e))
        {
            e->sk = 6;
            e->slook = e->age;
            e->ob.drop_ph = 0, e->ob.drop_look = e->age;
            return 0;
        }
        if (m3 && !wet && e->st3_m < ST3_DOWN)
        {
            e->st3_m += 1;                            /* lane/strip3: no stair up here: flat, then a step down */
            e->sk = 0;
            e->slook = e->age;
            return 0;
        }
        if (++e->shaz >= 4)
        {
            e->shaz = 0;
            e->soff = e->steps + 400;
            return 1;
        }
        e->sdir = (e->sdir + 1) & 3;
        e->sturns += 1;
        e->sk = -1;
        e->sgain = e->age;
        return 0;
    }
    /* every cell clear: the walk into it (a step down when going down), then the next set */
    if (fx == e->scx && fz == e->scz)
    {
        st_walked(e, r);                              /* lane/strip2's books */
        if (v->macros & NETH_VEC_OBSIDIAN) ob_glance_walked(e);   /* lane/obsid2: obsidian_glance's side */
        /* lane/strip3: the surface reached (lane/pearlgo: the hunt's; lane/bportal food_scout_sky: not before the sky) */
        if (m3 && !st3_sky_due(v, e, r, s))
            st3_walked(v, e, r, (provision_mode(v, e, r, s) ||
                                 (MOPT(MO_SH_FOOD) == SHW_SURFACE && e->sh.fwait)) ? FOOD_SCOUT_Y :
                                (pg_surface(v, r, s) || prov_want(v, e, r, s) ? PEARL_SURF_Y : ST3_SURF_Y),
                       provision_mode(v, e, r, s));
        e->sk = -1;
        e->sgain = e->age;
        e->shaz = 0;
        a->look_mode = 0;
        return 0;
    }
    look_at(r->x, ey, r->z, e->scx + 0.5, ey - 0.3, e->scz + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    a->hold = 1u << PK_FORWARD;
    if ((v->macros & NETH_VEC_OBSIDIAN) && !up && !down) ob_glance(v, e, r, a, yaw);   /* lane/obsid2 */
    if (up) a->hold |= 1u << PK_JUMP;                 /* lane/strip2: the stair's step up */
    if (e->age - e->slook > 40)
    {
        e->sk = 0;                                    /* something came back into the way: judged again */
        e->slook = e->age;
    }
    return 0;
}


/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

/* lane/lavaesc: the cells stood on out of fire and water, and a lava hit (burning and 3 or more lost at once: the
 * screen's orange) ends any macro for the escape (c->lava; strip_m_interrupt); a strip in progress turns back */
static void strip_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    int dead = (r->flags & PF_DEAD) != 0;
    if (v->macros & NETH_VEC_STRIP)
    {
        int fx = (int)floor(r->x), fy = (int)floor(r->y), fz = (int)floor(r->z);
        if (dead) e->nsafe = 0;
        else if (r->on_ground && r->fire == 0 && !s->eye_water &&
                 (e->nsafe == 0 || e->safe[e->nsafe - 1][0] != fx || e->safe[e->nsafe - 1][1] != fy ||
                  e->safe[e->nsafe - 1][2] != fz))
        {
            if (e->nsafe == 8)
            {
                memmove(e->safe[0], e->safe[1], 7 * sizeof e->safe[0]);
                e->nsafe = 7;
            }
            e->safe[e->nsafe][0] = fx, e->safe[e->nsafe][1] = fy, e->safe[e->nsafe][2] = fz;
            e->nsafe += 1;
        }
        if (!dead && r->fire && e->lhp - r->health >= 3.0f) e->lava_t = e->steps;
        else if (!dead && lava_hit_seen(v, e, r)) e->lava_t = e->steps;   /* lane/lava (lava_hit seen) */
        e->lhp = dead ? 20.0f : r->health;
        c->lava = !dead && e->lava_t == e->steps && !e->esc && e->steps >= e->esc_off && !nt_esc_veto(v, e, r);
        /* the policy's own attack on the block under the feet below y 16 dropped (it dug shafts into lava too) */
        if (c->attack && dig_under(v, r, s))
        {
            a->hold &= ~(1u << PK_ATTACK);
            a->press[PK_ATTACK] = 0;
            c->attack = 0;
        }
    }
}

/* lane/diamond (climb_swim on): the eyes under water within ST3_SWIM_AFTER steps of a climb step (the pillar or the
 * stair dug up into a sea's or a lake's floor): the swim up begins at once with the air whole, where the surface macro
 * waits for the air under 120. From s4-h14's flint-and-steel start (strip_case, 32 envs, random policy) 19 of 32 first
 * deaths were drowning there at master's options and 31 of 32 with the pillar, the surface macro's swim begun with
 * 6 s of air under about 18 blocks of water */
static int st3_swim_want(const struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    return MOPT(MO_CLIMB_SWIM) && (v->macros & NETH_VEC_STRIP2) && (v->macros & NETH_VEC_SURFACE) && c->s->eye_water &&
           c->r->dim == 0 && c->e->st3_climb_at > 0 && c->e->steps - c->e->st3_climb_at < ST3_SWIM_AFTER &&
           !(c->r->flags & PF_DEAD);
}

static int strip_m_cand(struct mac_step *c)
{
    int cand = c->lava || ((c->v->macros & NETH_VEC_STRIP) && strip_want(c->v, c->e, c->r, c->s)) || st3_swim_want(c);
#ifdef STRIP_GATE_TRACE
    if (c->e == c->v->e && c->e->steps % 100 == 0 && c->r->dim == 0 &&
        inv_count(c->s, WATER_BUCKET) > 0 && inv_count(c->s, DIAMOND_PICK) == 0 && inv_count(c->s, DIAMOND) < 3)
    {
        const struct neth_vec *v = c->v;
        const struct vec_env *e = c->e;
        const struct neth_rl_state *s = c->s;
        const struct vec_last *r = c->r;
        int need = (inv_count(s, IRON_PICK) + inv_count(s, DIAMOND_PICK) == 0 ? 3 : 0) +
                   (inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) + inv_count(s, LAVA_BUCKET) == 0 ? 3 : 0) +
                   (inv_count(s, FLINT_STEEL) == 0 ? 1 : 0);
        fprintf(stderr, "SGATE step=%lld world=%lld pos=%.2f,%.2f,%.2f cand=%d strip0=%d strip=%d kind=%d soff_left=%lld screen=%d eye_water=%d ground=%d tier=%d need=%d ore=%lld ingot=%lld diamond=%lld table=%lld planks=%lld dia_first=%d dia_oracle=%d ob_search=%d ob_yield=%d craftplan=%d seek=%d prov=%d fs_on=%d hunt=%d eat=%d food=%d meat=%lld scout=%d huntwant=%d\n",
                (long long)e->steps, (long long)r->wtime, r->x, r->y, r->z, cand,
                strip_want0(v, e, r, s), e->strip, e->kind, (long long)(e->soff - e->steps), r->screen,
                s->eye_water, r->on_ground, arm_tier(v, s), need,
                (long long)inv_count(s, IRON_ORE), (long long)inv_count(s, INGOT),
                (long long)inv_count(s, DIAMOND), (long long)inv_count(s, TABLE),
                (long long)inv_count(s, PLANKS), MOPT(MO_DIA_FIRST), MOPT(MO_DIA_ORACLE),
                e->ob.search, e->ob.yield, craft_plan(v, e, r, s), seek_want(v, e, s),
                provision_mode(v, e, r, s), e->fs_on, e->hunt, e->eat, r->food,
                (long long)meat_count(s), scout_want(v, e, r, s), hunt_want(v, e, s));
    }
#endif
    return cand;
}

static int strip_craft_blocks(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                              const struct neth_rl_state *s)
{
    int plan = craft_plan(v, e, r, s);
    if (MOPT(MO_WATER_DIA_STRIP) && MOPT(MO_DIA_FIRST) && r->dim == 0 &&
        inv_count(s, IRON_PICK) > 0 && inv_count(s, WATER_BUCKET) > 0 &&
        inv_count(s, DIAMOND_PICK) == 0 && inv_count(s, DIAMOND) < 3 &&
        (plan == C_PLANKS || plan == C_TABLE || plan == C_STICK)) return 0;
    return plan >= 0;
}

/* the lava hit: the strip turned back, the running macro ended, the escape (the surface kind) begun */
static int strip_m_interrupt(struct mac_step *c)
{
    struct vec_env *e = c->e;
    if (c->lava)
    {
        if (e->strip)
        {
            e->sdir = (e->sdir + 2) & 3;
            e->sk = -1;
        }
        mac_abort(e);
        mac_end(e);
        e->kind = M_SURFACE;
        e->esc = 1;
        e->esl_n = e->esl_done = 0;
        e->esl[0] = e->esl[1] = e->esl[2] = INT32_MIN;
    }
    return 0;
}

/* lane/strip: the strip mine before the policy's own lock (the policy dug one shaft to bedrock), after nothing to seek
 * and nothing to craft. lane/deadhour: the strip also starts from the policy's own stall (a spot it has not left for 400
 * steps and the want is met): a new branch a quarter turn from the spent one, so an hour past the column mines again
 * instead of holding five keys at a wall (4 of the 4 recorded hour runs: 50 of 60 minutes, 0 blocks) */
static int strip_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    int picked = c->picked;
    if (st3_swim_want(c))
    {
        e->kind = M_SURFACE;                          /* lane/diamond: climb_swim (surface.c's swim drives it) */
        e->st3_swims += 1;
        e->st3_climb_at = 0;
        return 1;
    }
    if (shore_night_yield(v, e, r, s)) return 0;
    int stall = (v->macros & NETH_VEC_STRIP) && e->steps - e->istep > 400 && !sh_owns(v, e, r);
    if ((v->macros & NETH_VEC_STRIP) && (strip_want(v, e, r, s) || stall) &&
        !(provision_mode(v, e, r, s) == PV_OFF && (v->macros & NETH_VEC_SEEK) && seek_want(v, e, s) >= 0) &&
        !(provision_mode(v, e, r, s) && provision_under(v, e, r) ? 0 :
          v->picks ? picked >= 0 : (v->macros & NETH_VEC_CRAFT) && strip_craft_blocks(v, e, r, s)))
    {
        e->kind = M_MINE;
        e->strip = 1;
        if (!e->sset)
        {
            e->sdir = ((int)floor(((double)r->yaw + 45.0) / 90.0) % 4 + 4) % 4;
            e->sset = 1;
        }
        else if (stall && !strip_want(v, e, r, s))
        {
            /* the spent column: a quarter turn and a fresh age, or the strip would walk the same cells again */
            e->sdir = (e->sdir + 1) & 3;
            e->age = 0;
            e->sturns = 0;
        }
        e->sk = -1;
        e->slook = 0;
        e->sgain = 0;
        e->istep = e->steps;
        e->idx = 1e9;
        return 1;
    }
    return 0;
}

/* the lava escape (the surface kind), and the strip */
static int strip_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    struct vec_env *e = c->e;
#ifdef FIRE_TRACE
    /* lane/flint (a VARIANT trace build): each escape step: the feet, the target cell (the newest safe one) off the feet,
     * the cells kept, the age, the keys held and the look, cobblestone held, and the four face columns' blocks at the
     * feet's level and one and two over (block:meta, +x -x +z -z) */
    if (e->kind == M_SURFACE && e->esc)
    {
        const struct vec_last *r = c->r;
        int rc = escape(c->v, e, r, c->s, a), id = (int)(e - c->v->e), fx = (int)floor(r->x), fy = (int)floor(r->y),
            fz = (int)floor(r->z);
        static const int FX[4] = {1, -1, 0, 0}, FZ[4] = {0, 0, 1, -1};
        fprintf(stderr, "ESC %d %lld at %.2f %.2f %.2f gr %d fire %d tg", id, (long long)e->steps, r->x, r->y, r->z,
                r->on_ground, r->fire);
        if (e->nsafe > 0)
            fprintf(stderr, " %d,%d,%d", e->safe[e->nsafe - 1][0] - fx, e->safe[e->nsafe - 1][1] - fy,
                    e->safe[e->nsafe - 1][2] - fz);
        else fprintf(stderr, " none");
        fprintf(stderr, " ns %d age %d hold %x yaw %.1f cob %d rc %d faces", e->nsafe, e->age, a->hold, a->look[0],
                (int)inv_count(c->s, COBBLE), rc);
        for (int f = 0; f < 4; ++f)
        {
            fprintf(stderr, " ");
            for (int k = 0; k < 3; ++k)
            {
                int m = 0, b = obsview_block(c->v->p, id, fx + FX[f], fy + k, fz + FZ[f], &m);
                fprintf(stderr, "%s%d:%d", k ? "/" : "", b, m);
            }
        }
        int m = 0, b = obsview_block(c->v->p, id, fx, fy, fz, &m), m1 = 0, b1 = obsview_block(c->v->p, id, fx, fy + 1, fz, &m1);
        fprintf(stderr, " own %d:%d/%d:%d\n", b, m, b1, m1);
        return rc;
    }
#endif
    if (e->kind == M_SURFACE && e->esc) return escape(c->v, e, c->r, c->s, a);
    if (e->kind == M_MINE && e->strip && shore_night_yield(c->v, e, c->r, c->s)) return 1;
    if (e->kind == M_MINE && e->strip) return strip(c->v, e, c->r, c->s, a, c->ey);
    return -1;
}

static uint32_t strip_m_label(const struct vec_env *e)
{
    return (e->kind == M_SURFACE && e->esc) || (e->kind == M_MINE && e->strip) ? NETH_VEC_STRIP : 0;
}

static void strip_m_end(struct vec_env *e)
{
    e->strip = 0;
    e->esc = 0;
}

static void strip_m_reset(struct vec_env *e)
{
    e->idx = 1e9, e->idy = e->idz = 0.0;
    e->istep = 0;
    e->sset = e->sturns = e->shaz = 0;
    e->soff = 0;
    e->nsafe = e->esc = 0;
    e->esl_n = e->esl_done = 0;
    e->esc_off = 0;
    e->lava_t = -100;
    e->lhp = 20.0f;
}

/* lane/deadhour: the stall anchor: a spot the policy has not left (3 blocks) keeps its step count, and a strip start
 * from it (strip_m_start) is the new branch */
static void strip_m_decision(struct vec_env *e)
{
    if (!e->reset_step && e->kind == M_NONE)
    {
        double dx = e->last.x - e->idx, dy = e->last.y - e->idy, dz = e->last.z - e->idz;
        if (e->idx > 1e8 || dx * dx + dy * dy + dz * dz > 9.0)
            e->idx = e->last.x, e->idy = e->last.y, e->idz = e->last.z, e->istep = e->steps;
    }
}

static const struct mac_switch STRIP_SW[] = {{"strip", NETH_VEC_STRIP}, {NULL, 0}};

static const struct vec_macro MAC_STRIP = {
    .name = "strip", .bit = NETH_VEC_STRIP, .sw = STRIP_SW, .observe = strip_m_observe, .cand = strip_m_cand,
    .interrupt = strip_m_interrupt, .start = strip_m_start, .drive = strip_m_drive, .label = strip_m_label,
    .end = strip_m_end, .reset = strip_m_reset, .decision = strip_m_decision,
};
