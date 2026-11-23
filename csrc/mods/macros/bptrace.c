/* lane/bportal: the portal build's trace (vecenv.h neth_vec_bp_trace), included last by macros.c (it reads the bucket
 * portal's, the obsidian macro's and the frame plan's statics). Read only: each start test is recomputed from the state
 * the last step left (the chain peek's inventory, the env's last result), with none of the side effects the tests
 * themselves have (bp_lava_search's site given up), so a trace on moves no row. What it answers: on a step another macro
 * or the policy drives with the portal still wanted, which test kept the bucket portal, the obsidian macro and the frame
 * plan from starting. The codes are vecenv.h's BPT_*. */

/* bp_want's clause that failed (BPW_OK when it holds) */
static int bpt_bp_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s)
{
    if (!(v->macros & NETH_VEC_BPORTAL)) return BPW_SWITCH;
    if (r->dim != 0) return BPW_DIM;
    if (e->dims & 1) return BPW_VISITED;
    if (r->flags & PF_DEAD) return BPW_DEAD;
    if (carry_furnace_gate(v, e, r, s)) return BPW_FURNACE;
    if (pg_nether_done(v, s)) return BPW_DONE;
    int n = bp_buckets(s);
    if (inv_count(s, FLINT_STEEL) == 0) return BPW_FLINT;
    if (n < (MOPT(MO_BP_BUCKETS) == BB_TWO ? 2 : 1)) return BPW_BUCKETS;
    if (st_dia_wait(v, s)) return BPW_DIA;
    if (n < 2 && (v->macros & NETH_VEC_OBSIDIAN) && inv_count(s, DIAMOND_PICK) > 0 && MOPT(MO_BP_DPICK) != BD_BPORTAL)
        return BPW_ONE_DPICK;
    if (MOPT(MO_BP_DPICK) == BD_ALWAYS && (v->macros & NETH_VEC_OBSIDIAN) && inv_count(s, DIAMOND_PICK) > 0)
        return BPW_ALWAYS;
    return BPW_OK;
}

/* bp_lava_search without its side effect (a site kept past BP_SITE_KEEP is read as kept) */
static int bpt_lava_search(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                           const struct neth_rl_state *s)
{
    const struct bp_env *b = &e->bp;
    if (!bp_want(v, e, r, s) || r->screen != PS_NONE) return 0;
    if (inv_count(s, LAVA_BUCKET) > 0 ||
        (inv_count(s, BUCKET) == 0 && !(bp_one(v, s) && inv_count(s, WATER_BUCKET) > 0)))
        return 0;
    if (b->site && !bp_site_needs_lava(v, e)) return 0;
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    if (tier < BP_SEARCH_TIER) return 0;
    return bp_mem_pick(e, 1) < 0 && !bp_roam_on(v, e);
}

/* bp_start's test that failed with the want held (BPS_* ; BPS_GO it would start, *mode the mode it would take) */
static int bpt_bp_start(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s, int *mode)
{
    const struct bp_env *b = &e->bp;
    *mode = -1;
    if (r->screen != PS_NONE) return BPS_SCREEN;
    if (s->eye_water) return BPS_WATER;
    if (e->steps < b->off) return BPS_REST;
    if (!r->on_ground) return BPS_AIR;
    if (r->food <= BP_FOOD && eat_want(v, r, s)) return BPS_EAT;
    if (prov_want(v, e, r, s)) return BPS_PROV;
    {
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        if (ob_lava(bp_blk(v, e, fx, fy, fz, NULL)) || ob_lava(bp_blk(v, e, fx, fy + 1, fz, NULL)) || r->fire)
            return BPS_LAVA;
    }
    int climb = bpt_lava_search(v, e, r, s) && (int)floor(r->y + 0.01) < BP_SEARCH_FLOOR && e->steps >= b->climb_off;
    int roam = bp_roam_want(v, e, s);
    if (b->site && inv_count(s, LAVA_BUCKET) == 0 && bp_mem_pick(e, 1) < 0 && bp_site_needs_lava(v, e) && !climb &&
        !roam)
        return BPS_SITE_LAVA;
    if (roam) return *mode = bp_sky(v, e, r) ? BM_ROAM : BM_UP, BPS_GO;
    if (climb) return *mode = BM_CLIMB, BPS_GO;
    if (b->site) return *mode = BM_BUILD, BPS_GO;
    int water = inv_count(s, WATER_BUCKET) > 0, lava = inv_count(s, LAVA_BUCKET) > 0, empty = inv_count(s, BUCKET) > 0;
    int k = -1;
    if (!water && empty) k = 0;
    else if (!lava && empty) k = 1;
    int due = !b->lk_set || py_hypot2(r->x - b->lkx, r->z - b->lkz) > BP_LOOK_DIST ||
              e->steps - b->lk_step > BP_LOOK_EVERY;
    if (k >= 0)
    {
        if (bp_mem_pick(e, k) >= 0) return *mode = k ? BM_LAVA : BM_WATER, BPS_GO;
        if (due) return *mode = BM_LOOK, BPS_GO;
        return k ? BPS_NO_LAVA : BPS_NO_WATER;
    }
    if (water && lava && bp_blocks(s) >= 2)
    {
        int lx = b->lx, ly = b->ly, lz = b->lz;
        if ((lx == 0 && ly == 0 && lz == 0) || bp_bad(e, lx, ly, lz))
        {
            int m = bp_mem_pick(e, 1);
            if (m >= 0) lx = b->mem[1][m].x, ly = b->mem[1][m].y, lz = b->mem[1][m].z;
            else lx = ly = lz = 0;
        }
        if (lx == 0 && ly == 0 && lz == 0 &&
            bp_bad(e, (int)floor(r->x), (int)floor(r->y + 0.01), (int)floor(r->z)))
            return due ? (*mode = BM_LOOK, BPS_GO) : BPS_FULL_BAD;
        return *mode = BM_GO, BPS_GO;
    }
    if (bp_one(v, s) && water && !lava && bp_blocks(s) >= 2)
    {
        if (bp_mem_pick(e, 1) >= 0) return *mode = BM_GO, BPS_GO;
        if (due) return *mode = BM_LOOK, BPS_GO;
        return BPS_NO_LAVA;
    }
    if (bp_one(v, s) && lava && !water && !empty)
        return bp_mem_pick(e, 1) >= 0 ? (*mode = BM_DUMP, BPS_GO) : BPS_NO_POOL;
    return BPS_NONE;
}

/* ob_want's clause that failed (OBW_OK when it holds) */
static int bpt_ob_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s)
{
    if (!(v->macros & NETH_VEC_OBSIDIAN)) return OBW_SWITCH;
    if (r->dim != 0) return OBW_DIM;
    if (e->dims & 1) return OBW_VISITED;
    if (r->flags & PF_DEAD) return OBW_DEAD;
    if (inv_count(s, OBSIDIAN) >= OB_KEEP_N) return OBW_HELD;
    if (pg_nether_done(v, s)) return OBW_DONE;
    if (OB_TRIP && inv_count(s, DIAMOND_PICK) == 0)
        return inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0 ? OBW_OK : OBW_TRIP;
    if (inv_count(s, FLINT_STEEL) == 0 && !(MOPT(MO_OB_FLINT) && inv_count(s, DIAMOND_PICK) > 0)) return OBW_FLINT;
    if (inv_count(s, DIAMOND_PICK) > 0)
    {
        int64_t nb = inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET);
        if (nb + inv_count(s, LAVA_BUCKET) == 1 && MOPT(MO_BP_DPICK) == BD_BPORTAL && (v->macros & NETH_VEC_BPORTAL) &&
            MOPT(MO_BP_BUCKETS) != BB_TWO)
            return OBW_BPORTAL;
        return nb > 0 ? OBW_OK : OBW_BUCKET;
    }
    if (OB_EARLY_FILL) return inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0 ? OBW_OK : OBW_EARLY;
    return OBW_DPICK;
}

/* portal_plan_ok's overworld clause that failed (PPW_OK when it holds; the Nether's way home is not read) */
static int bpt_pp_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s)
{
    if (!(v->macros & NETH_VEC_PORTAL)) return PPW_SWITCH;
    if (r->dim != 0) return PPW_DIM;
    if (carry_furnace_gate(v, e, r, s)) return PPW_FURNACE;
    if (MOPT(MO_NT_PORTAL_HOME) && (e->dims & 1)) return PPW_VISITED;
    if (inv_count(s, OBSIDIAN) < OB_KEEP_N) return PPW_OBSIDIAN;
    if (inv_count(s, FLINT_STEEL) == 0) return PPW_FLINT;
    if (inv_count(s, COBBLE) < 7) return PPW_COBBLE;
    if (prov_want(v, e, r, s)) return PPW_PROV;
    if (e->steps < e->pp_retry) return PPW_RETRY;
    return PPW_OK;
}

int neth_vec_bp_trace(const struct neth_vec *v, int id, struct neth_vec_bp_trace *out)
{
    if (v == NULL || id < 0 || id >= v->n || out == NULL || !v->chain) return -1;
    const struct vec_env *e = &v->e[id];
    const struct vec_last *r = &e->last;
    const struct neth_rl_state *s = &e->cpk.st;
    memset(out, 0, sizeof *out);
    out->obs = (int32_t)inv_count(s, OBSIDIAN), out->keep = OB_KEEP_N;
    out->bucket = (int32_t)inv_count(s, BUCKET), out->water = (int32_t)inv_count(s, WATER_BUCKET);
    out->lava = (int32_t)inv_count(s, LAVA_BUCKET), out->flint = (int32_t)inv_count(s, FLINT_STEEL);
    out->dpick = (int32_t)inv_count(s, DIAMOND_PICK), out->cobble = (int32_t)inv_count(s, COBBLE);
    out->dirt = (int32_t)inv_count(s, 3), out->furnace = (int32_t)inv_count(s, FURNACE);
    out->kind = e->kind;
    out->bp_want = bpt_bp_want(v, e, r, s);
    out->bp_start = out->bp_want == BPW_OK ? bpt_bp_start(v, e, r, s, &out->bp_mode_next) : BPS_NOWANT;
    if (out->bp_want != BPW_OK) out->bp_mode_next = -1;
    out->bp_on = e->bp.on, out->bp_mode = e->bp.mode, out->bp_site = e->bp.site, out->bp_step = e->bp.step;
    out->bp_mem_w = e->bp.nmem[0], out->bp_mem_l = e->bp.nmem[1];
    out->bp_pick_w = bp_mem_pick(e, 0) >= 0, out->bp_pick_l = bp_mem_pick(e, 1) >= 0;
    out->bp_off = e->bp.off;
    out->ob_want = bpt_ob_want(v, e, r, s);
    out->ob_on = e->ob.on, out->ob_job = e->ob.job, out->ob_who = ob_who(e), out->ob_search = e->ob.search;
    out->ob_off = e->ob.off;
    out->ob_mem_w = e->ob.nmem[0], out->ob_mem_l = e->ob.nmem[1];
    out->ob_why = e->ob.why, out->ob_why_step = e->ob.why_step;
    out->hunt_vis = hunt_want(v, e, s), out->meat = (int32_t)meat_count(s), out->hunt_cap = hunt_cap(v);
    out->hunt_off = e->hunt_off;
    out->pv = provision_mode(v, e, r, s), out->fo_have = e->fo_wt.have, out->fo_n = e->fo_wt.path.n;
    out->hunt_d = e->hunt ? (float)py_hypot2(e->hx - r->x, e->hz - r->z) : -1.0f;
    /* (strip_want0 is not called: its ob_search writes; with the provisions wanted it is 0 before it) */
    out->st3 = st3_want(v, e, r, s, 0), out->st_night = shore_night_yield(v, e, r, s), out->st_yield = e->ob.yield;
    out->st_on = e->strip, out->soff = e->soff;
    out->hk_vis = -1, out->hkind = -1, out->esteps = e->steps;
    if (e->hunt == 1)
    {
        out->hk_vis = 0;
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
            if (s->vis[i].id == e->fid) out->hk_vis = 1, out->hkind = s->vis[i].kind;
    }
    out->flost = e->flost, out->hpk = e->hpk, out->hkilled = e->hunt == 1 && s->mob_kills > e->hk0;
    out->hage = (int32_t)e->age, out->gstuck = e->gstuck;
    out->raw = (int32_t)raw_meat(s), out->coal = (int32_t)inv_count(s, COAL), out->planks = (int32_t)inv_count(s, PLANKS);
    out->cplan_next = craft_plan(v, e, r, s);
    out->screen = r->screen;
    out->fin = out->fin_n = out->ffuel = out->ffuel_n = out->fout = out->fout_n = -1;
    if (r->screen == PS_FURNACE && r->nslots >= 3)
    {
        out->fin = r->slot_item[0], out->fin_n = r->slot_count[0], out->ffuel = r->slot_item[1];
        out->ffuel_n = r->slot_count[1], out->fout = r->slot_item[2], out->fout_n = r->slot_count[2];
    }
    out->cplan = e->kind == M_CRAFT ? e->cplan : -1, out->cphase = e->kind == M_CRAFT ? e->cphase : -1;
    {
        double ey = r->y + 1.62, bd = -1.0, sd = -1.0;
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        out->obm_d = -1.0f, out->obm_dy = 0, out->ob_scs = e->ob.sc_strip;
        for (int i = 0; i < e->ob.nmem[1] && i < OB_MEM; ++i)
        {
            int x = e->ob.mem[1][i].x, y = e->ob.mem[1][i].y, z = e->ob.mem[1][i].z;
            double d = py_dist3(r->x, ey, r->z, x + 0.5, y + 0.5, z + 0.5);
            out->obm_n += 1;
            if (ob_bad(e, x, y, z)) out->obm_bad += 1;
            else if (y >= fy) out->obm_up += 1;
            else if (abs(x - fx) <= 1 && abs(z - fz) <= 1) out->obm_adj += 1;
            else if (d >= OB_MEM_GO) out->obm_far += 1;
            else out->obm_ok += 1;
            if (bd < 0 || d < bd) bd = d, out->obm_dy = y - fy;
            int meta = 0, b = obsview_block(v->p, (int)(e - v->e), x, y, z, &meta);
            if (b != 10 && b != 11) out->obm_gone += 1;
            else if (meta != 0) out->obm_flow += 1;
            else
            {
                out->obm_src += 1;
                if (sd < 0 || d < sd) sd = d;
            }
        }
        out->obm_d = (float)bd, out->obm_srcd = (float)sd;
    }
    out->tables = (int32_t)inv_count(s, TABLE), out->has_table = e->has_table;
    out->near_tb = near_table(e, r), out->near_fu = near_furnace(e, r);
    out->pp_want = bpt_pp_want(v, e, r, s);
    out->pp_on = e->kind == M_CRAFT && e->cplan == C_PORTAL;
    out->pp_step = out->pp_on ? e->pp_step : -1;
    out->pp_retry = e->pp_retry;
    out->food_pts = (int32_t)food_points(s), out->prov = prov_want(v, e, r, s);
    out->fs_on = e->fs_on, out->fs_n = e->fs_n, out->fs_off = e->fs_off, out->hunt = e->hunt;
    out->food_n = -1, out->food_d = -1.0f;
    const struct neth_rl_far *f = neth_rl_far(v->h, id);
    out->sw = -1, out->pl_sky = -1, out->on_ground = r->on_ground, out->eye_water = s->eye_water;
    if (f && f->census.on)
    {
        out->pl_sky = f->census.pl_sky;
        out->sw = scout_want(v, e, r, s);   /* (its one write, the census on, is only with the census off) */
        out->food_n = f->census.food_n, out->food_sky = f->census.food_sky;
        if (f->census.food_n > 0) out->food_d = (float)py_hypot2(f->census.food_x - r->x, f->census.food_z - r->z);
    }
    return 0;
}
