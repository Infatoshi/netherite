/* lane/strip2: NETH_VEC_STRIP2, the strip mine kept mining (included by macros.c before strip.c; off by default, so
 * the default digests are unchanged). Measured from gold-g5-s42 (32 envs, the random policy): the strip stopped at
 * about 6,000 ticks in 28 of 32 envs on the same 123 cobblestone, for two causes, both in the strip's own rules:
 *
 * - The pickaxe. best_tool reads the hotbar only. gold-g5's iron pickaxe sits in the main inventory (slot 16: the
 *   craft's shift-click fills from the hotbar's end, and that hotbar was full), so the strip dug with the stone
 *   pickaxe until it broke (step about 2,100), then the wooden one (about 4,100), then with the hand: stone dug by
 *   hand takes 150 ticks and drops nothing, and the strip's 60-step progress rule turned it before any broke. The
 *   strip's want counts the whole inventory (the iron pickaxe), so it never gave the step up: thousands of steps
 *   punching stone. Now the strip digs with the cheapest pickaxe that mines the block (stone before wood, twice the
 *   speed; iron and diamond last, kept for the ore that needs them), fetched from the main inventory into the hotbar
 *   under the inventory screen (a number key's click, as fetch_item) when the hotbar has none, and craft_plan keeps a
 *   stone pickaxe made while an iron one is held (st_spick_want).
 * - The floor going down. The staircase judges the floor of the next step (three blocks under the eye, one over)
 *   with the crosshair on its centre; from where the walk stops (the feet just over the edge of the new cell) that ray
 *   clips the corner of the block the player stands on, so the floor read as bad, the strip turned a quarter, and the
 *   descent became a 2x2 spiral (a level every 160 steps, four bad floors and the strip off for 400). Now the look
 *   goes to the far part of the floor's top face (ST_FLOOR_FAR), which the ray reaches over the near edge.
 *
 * And the inventory kept from filling (a full inventory picks up no ore): with under ST_FREE_MIN free slots the
 * surplus (cobblestone past ST_KEEP_COBBLE, dirt, gravel, saplings) is thrown out of the inventory screen behind the
 * player, the look turned back along the tunnel first.
 *
 * What it reads is the player's own: the inventory (the screen's), the crosshair, the look. */

#define ST_FLOOR_FAR 0.35       /* the floor's look: this far past its top face's centre, away from the player */
#define ST_FLOOR_TOP 0.98       /* and this high in the block (just under the top face) */
enum {
    ST_FETCH = 1,               /* stf: a pickaxe to the hotbar */
    ST_DROP = 2,                /* stf: the surplus out */
    ST_WAIT = 10,               /* steps the inventory screen is waited for after its key */
    ST_RETRY = 200,             /* no errand again for this many steps after one that failed */
    ST_GAP = 40,                /* nor for this many after one that ran */
    ST_KEEP_COBBLE = 128,       /* cobblestone kept (furnaces, the portal's 7, stone pickaxes) */
    ST_FREE_MIN = 3,            /* free main-inventory slots under which the surplus goes */
    ST_PACK_FREE_MIN = 5,       /* the late kit's spare room before a table, furnace, meat or sword is picked up */
    ST_PACK_HARD_MIN = 2,       /* interrupt a long-running macro before another pickup can fill the last slot */
    ST_DROP_MAX = 3,            /* stacks thrown a screen (two clicks each, then the close: NETH_RL_OPS 8) */
    ST_HOT_R = 8,               /* lava in view this far ahead along the strip (at the head's level or below) */
    ST_HOT_SIDE = 2,            /* and within this of its line: a quarter turn, as at a liquid ahead */
    ST_HOT_EVERY = 5,           /* the strip's steps between those looks (obsview_scan, the cast's own cost) */
    ST_NEAR_R = 4,              /* lava in view this near the feet across (overworld lava flows about 4 blocks) */
    ST_NEAR_UP = 3,             /* and up to this over the feet (it flows down into the tunnel): the strip heads away */
};
/* lane/strip3's (the climb, below) */
#define ST3_SURF_Y 56           /* the feet at or over this with the way ahead open (no stair up): the surface */
enum {
    ST3_WOOD = 1,               /* the climb's wants (st3_want) */
    ST3_OUT = 2,
    ST3_OUT_Y = 40,             /* the strip's want over with the feet under this: the climb out begins */
    ST3_FALL_WAIT = 20,         /* steps a set waits after a falling block was dug in it before its walk */
    ST3_UP = 0, ST3_FLAT = 1, ST3_DOWN = 2,   /* a set's mode (e->st3_m), tried in this order */
    ST3_STICK_WOOD = 2,         /* planks kept for sticks (4 sticks: two pickaxes) */
    ST3_SPARE_WOOD = 1,         /* lane/diamond (strip_wood chain): planks kept past the iron and diamond pickaxes' sticks */
    ST3_EARLY_WOOD = 10,       /* lane/chain2: a surface reserve for a later table and diamond pickaxe */
    ST3_ARMOR_WOOD = 24,       /* lane/chain3: 16 planks, a table's four, and four for early wood spending */
    ST3_SWIM_AFTER = 40,        /* lane/diamond (climb_swim): the eyes under water this soon after a climb step: the swim */
    ST3_NIGHT0 = 12500,         /* lane/diamond (strip_wood_night wait): the night the errand waits out, the day's time: */
    ST3_NIGHT1 = 23500,         /* dusk a little before mobs spawn (13000) to just before dawn */
    ST3_EXTRA = 12,             /* planks past the need the errand brings back (three logs) */
    ST3_PIL_COBBLE = 8,         /* cobblestone held to pillar a level, also a barehand climb's reserve */
    ST3_FOE_R = 5,              /* the guard: a hostile in view this near is fought */
    ST3_BACK_GAP = 20,          /* and the look turned back for a hurt at most once in this many steps */
};

/* a pickaxe of tier t for a block needing need: lower is better, -1 not one that mines it. Stone first (wood is half
 * its speed), then wood, then iron and diamond (their uses kept for the ore only they mine) */
static int st_rank(int t, int need)
{
    if (t <= 0 || t < need) return -1;
    return t == 2 ? 0 : t == 1 ? 1 : t;
}

/* the slot in [i0, i1) holding the best-ranked pickaxe for need (its rank in *rank, 1 << 30 none), -1 none */
static int st_pick_r(const struct vec_tabs *T, const struct neth_rl_state *s, int need, int i0, int i1, int *rank)
{
    int best = -1, br = 1 << 30;
    for (int i = i0; i < i1; ++i)
    {
        int it = s->inv[i].item;
        if (it < 0 || it > 4095 || s->inv[i].count <= 0) continue;
        int k = st_rank(T->pick[it], need);
        if (k >= 0 && k < br) best = i, br = k;
    }
    if (rank) *rank = br;
    return best;
}

static int st_pick(const struct vec_tabs *T, const struct neth_rl_state *s, int need, int i0, int i1)
{
    return st_pick_r(T, s, need, i0, i1, NULL);
}

/* the strip's tool for block b: the hotbar slot (-1 the hand), or ST_FETCH when the main inventory has a pickaxe that
 * mines it ranked above the hotbar's best (none there; or only iron while a stone one sits in the main inventory: the
 * spare stone pickaxe the craft made went to slot 34 and the iron one wore on) */
static int st_tool(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, int b, int *fetch)
{
    *fetch = 0;
    if (b < 0 || b > 4095 || v->T.tool_of[b] != 2) return best_tool(&v->T, s, b);
    int need = v->T.picky[b] > 0 ? v->T.picky[b] : 1, hr, mr;
    int h = st_pick_r(&v->T, s, need, 0, 9, &hr);
    if (e->steps >= e->stf_off && st_pick_r(&v->T, s, need, 9, 36, &mr) >= 0 && mr < hr) *fetch = 1;
    return h >= 0 ? h : best_tool(&v->T, s, b);
}

/* craft_plan's line: a stone pickaxe kept while only iron or diamond ones are held, so the strip's stone does not
 * spend them */
static int st_spick_want(const struct neth_vec *v, const struct neth_rl_state *s, int tier)
{
    if (!(v->macros & NETH_VEC_STRIP2) || tier < 3) return 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (it >= 0 && it < 4096 && s->inv[i].count > 0 && v->T.pick[it] == 2) return 0;
    }
    return 1;
}

/* dirt, gravel, saplings (lane/chain pearl_trim: netherrack too) */
static int st_junk(int it, int rack) { return it == 3 || it == 13 || it == 6 || (rack && it == 87); }

/* the surplus's slots, the smallest stacks first (at most max): junk, then cobblestone past the kept */
static int st_surplus(const struct neth_vec *v, const struct neth_rl_state *s, int *out, int max, int late)
{
    int n = 0, rack = MOPT(MO_PEARL_TRIM);
    if (late)
    {
        int64_t dirt_extra = inv_count(s, 3) - 64;
        int64_t snow_extra = inv_count(s, SNOWBALL) - 64;
        int picks = 0, fresh_swords = sword_stock_fresh(s);
        for (int i = 0; i < 36; ++i)
        {
            int it = s->inv[i].item;
            picks += s->inv[i].count > 0 && it >= 0 && it < 4096 && v->T.pick[it] > 0;
        }
        for (int pass = 0; pass < 5 && n < max; ++pass)
            for (int j = 0; j < 36 && n < max; ++j)
            {
                int i = (j + 9) % 36, it = s->inv[i].item;
                if (s->inv[i].count <= 0) continue;
                int drop = pass == 0 ? (it == 3 && s->inv[i].count <= dirt_extra) || it == 13 || it == 6 :
                           pass == 1 ? it == 112 || it == 12 || it == 288 || it == 344 || it == 334 ||
                                       it == 87 || (it == 261 && s->inv[i].damage > 0) :
                           pass == 2 ? picks > 1 && ((it == 270 && s->inv[i].damage >= 45) ||
                                                     (it == 274 && s->inv[i].damage >= 100)) :
                           pass == 3 ? it == 272 && s->inv[i].damage > 0 && fresh_swords >= 2 :
                                       it == SNOWBALL && s->inv[i].count <= snow_extra;
                if (!drop) continue;
                out[n++] = i;
                if (it == 3) dirt_extra -= s->inv[i].count;
                if (pass == 2) picks -= 1;
                if (it == SNOWBALL) snow_extra -= s->inv[i].count;
            }
    }
    else
        for (int i = 0; i < 36 && n < max; ++i)
            if (s->inv[i].count > 0 && st_junk(s->inv[i].item, rack)) out[n++] = i;
    int64_t extra = inv_count(s, COBBLE) - ST_KEEP_COBBLE;
    while (n < max && extra > 0)
    {
        int best = -1;
        for (int i = 0; i < 36; ++i)
        {
            if (s->inv[i].item != COBBLE || s->inv[i].count <= 0 || s->inv[i].count > extra) continue;
            int dup = 0;
            for (int j = 0; j < n; ++j) dup |= out[j] == i;
            if (!dup && (best < 0 || s->inv[i].count < s->inv[best].count)) best = i;
        }
        if (best < 0) break;
        extra -= s->inv[best].count;
        out[n++] = best;
    }
    return n;
}

static int st_free(const struct neth_rl_state *s)
{
    int n = 0;
    for (int i = 0; i < 36; ++i) n += s->inv[i].item < 0 || s->inv[i].count <= 0;
    return n;
}

/* the errand begun: the inventory's key (the look as it is: a drop turns it back along the tunnel first) */
static void st_begin(struct vec_env *e, struct neth_rl_act *a, int kind)
{
    e->stf = kind;
    e->stf_age = e->age;
    a->press[PK_INVENTORY] = 1;
}

/* the errand's step under strip(): 0 going on (the strip's return) */
static int st_errand(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                     struct neth_rl_act *a)
{
    if (r->screen == PS_INVENTORY)
    {
        int n = 0;
        if (e->stf == ST_FETCH)
        {
            int src = st_pick(&v->T, s, 1, 9, 36);
            if (src >= 0)
            {
                /* the hotbar slot the macros need least (fetch_item's rule): empty, else no tool, no craft part */
                int to = 8, best = 1 << 30;
                for (int i = 8; i >= 0; --i)
                {
                    int it = s->inv[i].item, itc = it < 0 ? 0 : it > 4095 ? 4095 : it, score = 0;
                    if (it >= 0 && s->inv[i].count > 0)
                    {
                        int tool = v->T.pick[itc] || v->T.axe[itc] || v->T.shovel[itc] || it == 268 || it == 272 ||
                                   it == 267 || it == 276 || it == 283;
                        score = tool ? 100 : v->T.craft[itc] || v->T.craft_iron[itc] || it == COAL || it == TABLE ? 10 : 1;
                    }
                    if (score < best) best = score, to = i;
                }
                a->ops[n++] = click(inv_slot(src, 0), to, 2);
            }
        }
        else
        {
            int sl[ST_DROP_MAX], k = st_surplus(v, s, sl, ST_DROP_MAX, e->st_pack);
            for (int j = 0; j < k; ++j)
            {
                a->ops[n++] = click(inv_slot(sl[j], 0), 0, 0);
                a->ops[n++] = click(-999, 0, 0);
            }
        }
        a->ops[n++] = CLOSE;
        a->nops = n;
        e->stf = 0;
        e->stf_off = e->steps + ST_GAP;               /* never a screen every step, whatever the click did */
        e->sk = -1;                                   /* the look moved: the next set picked again */
        return 0;
    }
    if (e->age - e->stf_age > ST_WAIT)
    {
        e->stf = 0;
        e->stf_off = e->steps + ST_RETRY;
        if (r->screen != PS_NONE)
        {
            a->nops = 1;
            a->ops[0] = CLOSE;
        }
        return 1;
    }
    return 0;
}

/* at a new set: the surplus thrown when the inventory is nearly full. 1 when the errand began (the look turned back
 * along the tunnel on this step, the key pressed on the next: the throw goes where the look is) */
static int st_drop_start(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a)
{
    int sl[ST_DROP_MAX];
    int free_min = e->st_pack ? ST_PACK_FREE_MIN : ST_FREE_MIN;
    if (e->steps < e->stf_off || st_free(s) >= free_min || st_surplus(v, s, sl, ST_DROP_MAX, e->st_pack) == 0)
        return 0;
    a->look_mode = 1;
    a->look[0] = (float)(((e->sdir + 2) & 3) * 90);
    a->look[1] = 0.0f;
    if (fabs(remainder((double)r->yaw - (double)a->look[0], 360.0)) > 5.0 || fabs((double)r->pitch) > 5.0) return 1;
    st_begin(e, a, ST_DROP);
    return 1;
}

/* lane/diamond (dia_first on): the bucket portal waits while the strip has diamonds to dig: no diamond pickaxe held, an
 * iron one or three diamonds held. Measured (lane/dbench's 256 runs from spawn, master): the bucket portal began in 38
 * runs, chose a site in 1 and lit none, and drove 1.87 M steps, a fifth of the steps of the runs past flint and steel
 * that made no diamond pickaxe */
static int st_dia_wait(const struct neth_vec *v, const struct neth_rl_state *s)
{
    if (!MOPT(MO_DIA_FIRST) || !(v->macros & NETH_VEC_STRIP2) || inv_count(s, DIAMOND_PICK) > 0) return 0;
    return inv_count(s, IRON_PICK) > 0 || inv_count(s, DIAMOND) >= 3;
}

/* lane/diamond: the link's books (struct neth_vec_strip dia; no act reads them), from flint and steel held to a diamond
 * pickaxe held */
static void st_dia_books(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, int tier)
{
    (void)v;
    int64_t *d = e->st_dia;
    if (r->flags & PF_DEAD) return;
    if (!e->st_dia_on && inv_count(s, FLINT_STEEL) > 0 && inv_count(s, DIAMOND_PICK) == 0)
    {
        e->st_dia_on = 1;
        d[4] = -1;
        d[7] = 1 << 30;
        e->st_dia_pk = -1;
    }
    if (e->st_dia_on && inv_count(s, DIAMOND_PICK) > 0) e->st_dia_on = 2;
    if (e->st_dia_on != 1) return;
    int64_t wood = inv_count(s, PLANKS) + 4 * (inv_count(s, LOG) + inv_count(s, LOG2)) + inv_count(s, STICK) / 2;
    int npk = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (it >= 0 && it < 4096 && s->inv[i].count > 0 && v->T.pick[it]) npk += s->inv[i].count;
    }
    d[r->y < 16.0 ? 0 : r->y < 40.0 ? 1 : 2] += 1;
    if (tier == 0) d[3] += 1;
    if (tier == 0 && d[4] < 0)
    {
        d[4] = e->steps;
        d[5] = (int64_t)floor(r->y);
        d[6] = wood;
    }
    if (wood < d[7]) d[7] = wood;
    if (s->mo_hit && s->mo_block == 56 &&
        !(e->st_dia_cell[0] == s->mo_x && e->st_dia_cell[1] == s->mo_y && e->st_dia_cell[2] == s->mo_z))
    {
        d[8] += 1;
        e->st_dia_cell[0] = s->mo_x, e->st_dia_cell[1] = s->mo_y, e->st_dia_cell[2] = s->mo_z;
    }
    if (e->st_dia_pk >= 0 && npk < e->st_dia_pk && r->screen == PS_NONE) d[9] += e->st_dia_pk - npk;
    e->st_dia_pk = npk;
    d[10] = (int64_t)floor(r->y);
    if (inv_count(s, INGOT) >= 3 && tier < 3 && inv_count(s, STICK) < 2 && inv_count(s, PLANKS) < 2) d[11] += 1;
    if (e->kind == M_MINE && e->seek >= 1 && e->seek <= 4) d[11 + e->seek] += 1;
}

/* the books (the switch on or off; no act reads them): each good's rises since the episode began */
static const int16_t ST_GOODS[6] = {COAL, IRON_ORE, 14, 264, 331, COBBLE};   /* coal, iron ore, gold ore, diamond,
                                                                                * redstone, cobblestone */
static void st_observe(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s)
{
    for (int k = 0; k < 6; ++k)
    {
        int32_t c = (int32_t)inv_count(s, ST_GOODS[k]);
        if (e->st_had[k] >= 0 && c > e->st_had[k]) e->st_got[k] += c - e->st_had[k];
        e->st_had[k] = c;
    }
    e->st_dpick = (int32_t)inv_count(s, DIAMOND_PICK);
    /* lane/strip3: what is held, the table, the first diamond pickaxe's step */
    static const int16_t HELD[6] = {DIAMOND, PLANKS, STICK, LOG, TABLE, INGOT};
    int32_t logs0 = e->st_held[3];
    for (int k = 0; k < 6; ++k) e->st_held[k] = (int32_t)inv_count(s, HELD[k]);
    e->st_held[3] += (int32_t)inv_count(s, LOG2);
    if (e->st3 == ST3_WOOD && e->st_held[3] > logs0) e->st_err[2] += e->st_held[3] - logs0;
    if (e->st_err[0] > 0 && e->st_err[3] == 0 && e->st_dpick > 0 && e->st_dpick_step < 0) e->st_err[3] = 1;
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 && s->inv[i].count > 0 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    e->st_held[6] = tier;
    e->st_table = !e->has_table ? 0 : near_table(e, r) ? 2 : 1;
    /* lane/strip3's guard: a hurt with no fire (the screen's health, its flames) */
    if (r->flags & PF_DEAD) e->st3_lhp = 20.0f;
    else
    {
        if (r->health < e->st3_lhp - 0.5f && !r->fire) e->st3_hurt = e->steps;
        e->st3_lhp = r->health;
    }
    if (e->st_dpick > 0 && e->st_dpick_step < 0) e->st_dpick_step = e->steps;
    st_dia_books(v, e, r, s, tier);
}

/* the strip walked into its next cell */
static void st_walked(struct vec_env *e, const struct vec_last *r)
{
    e->st_cells += 1;
    e->st_last = e->steps;
    e->st_lpos[0] = (float)r->x, e->st_lpos[1] = (float)r->y, e->st_lpos[2] = (float)r->z;
}

int neth_vec_strip(const struct neth_vec *v, int id, struct neth_vec_strip *out)
{
    if (v == NULL || id < 0 || id >= v->n || out == NULL) return -1;
    const struct vec_env *e = &v->e[id];
    memset(out, 0, sizeof *out);
    for (int k = 0; k < 6; ++k) out->got[k] = e->st_got[k];
    out->cells = e->st_cells;
    out->dpick = e->st_dpick;
    out->last = e->st_last;
    out->strip_steps = e->st_steps;
    out->steps = e->steps;
    for (int k = 0; k < 3; ++k) out->lpos[k] = e->st_lpos[k];
    out->pos[0] = e->last.x, out->pos[1] = e->last.y, out->pos[2] = e->last.z;
    out->dim = e->last.dim;
    for (int k = 0; k < 7; ++k) out->held[k] = e->st_held[k];
    for (int k = 0; k < 4; ++k) out->errand[k] = e->st_err[k];
    out->table = e->st_table;
    out->out_steps = e->st_out_steps;
    out->dpick_step = e->st_dpick_step;
    for (int k = 0; k < NETH_VEC_DIA_N; ++k) out->dia[k] = e->st_dia[k];
    out->dia[16] = e->cw_levels, out->dia[17] = e->cw_quit, out->dia[18] = e->st3_swims;
    return 0;
}

/* lava in view ahead (obsview's OV_HOT: a lava cell, source or flowing, whose centre the eye sees from the look the
 * player has, never through a wall): at the head's level or below, along the strip's direction within ST_HOT_R and
 * ST_HOT_SIDE of its line. The obsidian search's rule (ob_hot_cell), for the strip at every want */
struct st_ctx { const struct vec_env *e; };

static int st_hot_skip(void *ctx, int kind, int x, int y, int z)
{
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
    const struct vec_env *e = ((const struct st_ctx *)ctx)->e;
    if (kind != OV_HOT) return 1;
    int fx = (int)floor(e->last.x), fy = (int)floor(e->last.y + 0.01), fz = (int)floor(e->last.z), d = e->sdir;
    int along = (x - fx) * DX[d] + (z - fz) * DZ[d], side = abs((x - fx) * DZ[d] - (z - fz) * DX[d]);
    return !(y <= fy + 1 && along >= 0 && along <= ST_HOT_R && side <= ST_HOT_SIDE);
}

static int st_hot(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, double ey)
{
    struct obsview_hit h[OV_KINDS];
    struct st_ctx c = {e};
    if (obsview_scan(v->p, (int)(e - v->e), r->x, ey, r->z, r->yaw, r->pitch, ST_HOT_R + 1, 1 << OV_HOT, st_hot_skip,
                     &c, h) != 0)
        return 0;
    return h[OV_HOT].d > 0.0f;
}

/* the stair back up to NETH_VEC_STRIP_Y (the feet below it: a pit, a seek's hole, a fall): cell k of the set, 0 the
 * block over the head (the jump's room), 1 the next column's head, 2 its feet (a block up from the feet now); the floor
 * is the next column's block at the feet's height, and the walk into it jumps */
static void st_up_cell(const struct vec_env *e, int k, int *x, int *y, int *z)
{
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
    *x = k == 0 ? e->scx - DX[e->sdir] : e->scx;
    *z = k == 0 ? e->scz - DZ[e->sdir] : e->scz;
    *y = k == 2 ? e->scy + 1 : e->scy + 2;
}

/* lava in view near the feet, at any height it can flow down from (ST_NEAR_R across, ST_NEAR_UP over the feet, one
 * under): the strip heads the cardinal way most away from it (back along its own tunnel when it is ahead). The trace:
 * lava two blocks over the head ahead, out of st_hot's window, a quarter turn, the tunnel dug on beside it, the flow
 * down into the tunnel behind the player and the escape walked back into it (8 first deaths at one cell in 64 envs) */
static int st_near_skip(void *ctx, int kind, int x, int y, int z)
{
    const struct vec_env *e = ((const struct st_ctx *)ctx)->e;
    if (kind != OV_HOT) return 1;
    int fx = (int)floor(e->last.x), fy = (int)floor(e->last.y + 0.01), fz = (int)floor(e->last.z);
    return !(abs(x - fx) <= ST_NEAR_R && abs(z - fz) <= ST_NEAR_R && y >= fy - 1 && y <= fy + ST_NEAR_UP);
}

/* 1 when the strip's direction was changed (sdir set away from the lava seen) */
static int st_flee(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, double ey)
{
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
    struct obsview_hit h[OV_KINDS];
    struct st_ctx c = {e};
    if (obsview_scan(v->p, (int)(e - v->e), r->x, ey, r->z, r->yaw, r->pitch, ST_NEAR_R + ST_NEAR_UP + 1, 1 << OV_HOT,
                     st_near_skip, &c, h) != 0 ||
        h[OV_HOT].d <= 0.0f)
        return 0;
    int ddx = h[OV_HOT].x - (int)floor(r->x), ddz = h[OV_HOT].z - (int)floor(r->z), best = e->sdir, bd = 1 << 30;
    for (int d = 0; d < 4; ++d)
    {
        int dot = DX[d] * ddx + DZ[d] * ddz;
        if (dot < bd) bd = dot, best = d;
    }
    if (best == e->sdir) return 0;
    e->sdir = best;
    return 1;
}

/* ------------------------------------------------------------------------------------------------------------------
 * lane/strip3 (still NETH_VEC_STRIP2): the climb. Measured from gold-g5-s42 under strip2 (32 envs, the random policy):
 * 12 of 32 envs ended alive at y 12 holding 3 or more diamonds with no diamond pickaxe, no table and too few planks
 * (a table is 4 planks, the pickaxe's 2 sticks 2 more), and nothing went up for wood; most lava first deaths began on
 * the random policy's steps after the strip's want ended, beside the y 10 lakes. The climb is the strip's own walk in
 * a mode that prefers up: each set tries a stair up (the block over the head, the next column's head and feet, the
 * floor the next column's block at the feet's height), then flat (the floor a block under the feet), then a step down
 * (two under), else the strip's quarter turn. Underground every set is a stair up (rock ahead); at the surface it
 * follows the ground (hills up, flat, a block down), with the strip's lava, liquid and floor rules all the way. Two
 * wants run it:
 * - ST3_WOOD, the wood errand: three diamonds held, no diamond pickaxe, and the planks short of the table (none held or
 *   in reach) and the sticks (a log is 4 planks): up to the surface and along it until the seek's wood want (seek_want,
 *   a log in view) takes a log; the craft then makes the planks, the table, the sticks and the pickaxe where it stands.
 * - ST3_OUT, out of the depth: the strip's own want over (no obsidian search, nothing to dig for) with the feet under
 *   ST3_OUT_Y: up until the surface (the feet at ST3_SURF_Y or over with the way ahead open), then the step given back.
 * And a falling block (gravel, sand) dug in a set holds the walk ST3_FALL_WAIT steps and judges the cells again (what
 * fell from above is dug before the player walks under it); one over the head on a stair up turns the stair.
 * What it reads: the inventory, the feet's height (F3), the crosshair and the liquid ray, obsview's lava in view.
 * Its constants are at the top of the file. */

static int st3_tier(const struct neth_vec *v, const struct neth_rl_state *s)
{
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 && s->inv[i].count > 0 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    return tier;
}

/* 1 while the strip has digging to do: iron still needed (strip_want's count), diamonds with an iron pickaxe, or three
 * diamonds and the diamond pickaxe still to make */
static int st3_digs(const struct neth_vec *v, const struct neth_rl_state *s, int tier)
{
    int64_t need = (inv_count(s, IRON_PICK) + inv_count(s, DIAMOND_PICK) == 0 ? 3 : 0) +
                   (inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) + inv_count(s, LAVA_BUCKET) == 0 ? 3 : 0) +
                   (inv_count(s, FLINT_STEEL) == 0 ? 1 : 0);
    if (tier >= 2 && need > inv_count(s, IRON_ORE) + inv_count(s, INGOT)) return 1;
    return inv_count(s, DIAMOND_PICK) == 0 && ((tier >= 3 && inv_count(s, DIAMOND) < 3) ||
                                               ((v->macros & NETH_VEC_LATE) && inv_count(s, DIAMOND) >= 3));
}

/* the wood held in planks (a log 4, two sticks 1) and the wood the next pickaxes need (the table's 4 when none is held
 * or in reach, ST3_STICK_WOOD for their sticks) */
static int64_t st3_have(const struct neth_rl_state *s)
{
    return inv_count(s, PLANKS) + 4 * (inv_count(s, LOG) + inv_count(s, LOG2)) + inv_count(s, STICK) / 2;
}

#define ST3_FOOD_WOOD 12   /* three logs crafted to planks, eleven fuel planks for sixteen meats */
#define ST3_FOOD_FLOOR 160
static int st3_food_fuel_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    int64_t planks = inv_count(s, PLANKS) + 4 * (inv_count(s, LOG) + inv_count(s, LOG2));
    return MOPT(MO_FOOD_FUEL) == FF_WOOD && raw_meat(s) >= FOOD_COOK_N &&
           food_points(s) < ST3_FOOD_FLOOR && inv_count(s, COAL) == 0 &&
           (inv_count(s, FURNACE) > 0 || near_furnace(e, &e->last)) &&
           (e->last.y >= ST3_SURF_Y || e->st3 == ST3_WOOD) && planks < ST3_FOOD_WOOD;
}

/* The exclusive food stage also needs a table before it can make a missing furnace. Seed 2 returned with one plank,
 * no logs or furnace and enough raw meat: the fuel-only wood want never ran, so no cook could begin. */
static int st3_provision_wood_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    if (st3_food_fuel_want(v, e, s)) return 1;
    return (raw_meat(s) >= FOOD_COOK_N || food_points(s) >= ST3_FOOD_FLOOR) &&
           inv_count(s, FURNACE) == 0 && !near_furnace(e, &e->last) &&
           inv_count(s, COBBLE) >= 8 && inv_count(s, TABLE) == 0 && !near_table(e, &e->last) &&
           inv_count(s, PLANKS) < 4 && inv_count(s, LOG) + inv_count(s, LOG2) == 0;
}

static int64_t st3_need(const struct vec_env *e, const struct neth_rl_state *s)
{
    return (inv_count(s, TABLE) > 0 || near_table(e, &e->last) ? 0 : 4) + ST3_STICK_WOOD;
}

/* 1 when the wood errand is wanted: the strip has digging to do, a pickaxe to climb with, and the wood held short of
 * the next pickaxes' (the stone pickaxes the strip wears out, the diamond pickaxe's table and sticks); begun, it runs
 * until ST3_EXTRA more is held */
static int st3_wood_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    if (!(v->macros & NETH_VEC_STRIP2) || e->last.dim != 0) return 0;
    int tier = st3_tier(v, s);
    if (tier >= 1 && st3_food_fuel_want(v, e, s)) return 1;
    int chain = MOPT(MO_STRIP_WOOD) >= SW_CHAIN && inv_count(s, DIAMOND_PICK) == 0;
    if (MOPT(MO_STRIP_WOOD) == SW_EARLY && chain && tier >= 1 &&
        (e->last.y >= ST3_SURF_Y || e->st3 == ST3_WOOD) &&
        st3_have(s) < ST3_EARLY_WOOD + (MOPT(MO_FOOD_WOOD_RESERVE) ? ST3_ARMOR_WOOD : 0))
        return 1;
    /* lane/diamond (strip_wood_night wait): no errand for wood at night (the sky's time, as the pearl hunt reads it) with
     * a pickaxe held: the strip digs on below. From the library's flint-and-steel starts the chain's errands took the
     * low-wood starts up into the night: deaths a run 0.18 to 0.67, zombies 2 to 18 */
    if (chain && tier >= 1 && MOPT(MO_STRIP_WOOD_NIGHT) == SWN_WAIT)
    {
        int64_t t = ((e->last.wtime % 24000) + 24000) % 24000;
        if (t >= ST3_NIGHT0 && t < ST3_NIGHT1) return 0;
    }
    /* lane/diamond (strip_wood chain): no pickaxe at all past the stone pickaxe's stage (cobblestone or ingots held: a
     * pickaxe worn out), the wood for the table, a wooden pickaxe and the sticks of the next ones; no climb without a
     * pickaxe (st3_want), so only the seek's walk to a log in view (dug by hand). 31 of 59 runs past flint and steel
     * that timed out held no pickaxe at the end, the policy's own keys driving */
    if (chain && tier == 0 && (inv_count(s, COBBLE) >= 3 || inv_count(s, INGOT) > 0))
        return st3_have(s) < (inv_count(s, TABLE) > 0 || near_table(e, &e->last) ? 0 : 4) + 3 + 1 + 1 + ST3_SPARE_WOOD;
    if (tier < 1) return 0;
    int64_t need = st3_need(e, s);
    if (chain)
    {
        /* lane/diamond (strip_wood chain): st3_digs held no want for the iron pickaxe's own sticks (3 ingots or more
         * held, stone the best pickaxe: no iron left to dig), so the run sat on its ingots with no wood; and every
         * pickaxe the chain still needs is counted: the iron one, the diamond one and a spare stone one, a plank's
         * worth of sticks each */
        if (tier < 2 && inv_count(s, INGOT) < 3 && inv_count(s, DIAMOND) < 3) return 0;
        need = (inv_count(s, TABLE) > 0 || near_table(e, &e->last) ? 0 : 4) + (tier < 3 ? 1 : 0) + 1 + ST3_SPARE_WOOD;
    }
    else if (!st3_digs(v, s, tier))
        return 0;
    return st3_have(s) < need + (e->st3 == ST3_WOOD ? ST3_EXTRA : 0);
}

#ifdef ST3_KEEP6
/* the measured alternative (a VARIANT build: XFLAGS=-DST3_KEEP6): no wood errand; until the diamond pickaxe is made, no
 * plan spends the wood held under 6 planks' worth (a table and its sticks), the pickaxe's own table and sticks aside: a
 * table, a wooden or stone pickaxe, a smelt on planks with no coal */
static int st3_keep6(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, int p)
{
    (void)e;
    if (!(v->macros & NETH_VEC_STRIP2) || p < 0 || inv_count(s, DIAMOND_PICK) > 0 || inv_count(s, DIAMOND) >= 3) return p;
    int cost = p == C_TABLE ? 4 : p == C_WPICK ? 4 : p == C_SPICK ? 1 : p == C_SMELT && inv_count(s, COAL) == 0 ? 2 : 0;
    return cost > 0 && st3_have(s) - cost < 6 ? -1 : p;
}
#endif

/* lane/bportal (food_scout_sky on): the provisions wanted and the feet not under the sky (the census's pl_sky, the
 * server's height map, PRIVILEGED as the scout's census is): the climb goes on past PEARL_SURF_Y to the sky, where the
 * provisions' scout may begin (food.c scout_want). Seed 2's obsidian start: 88% of the scout's rows had no sky over the
 * head, the walk going to and fro in the strip's stair, 10 of 23 scouting attempts never 16 blocks from the start */
static int st3_sky_due(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s)
{
    if (!MOPT(MO_FOOD_SCOUT_SKY) || !prov_want(v, e, r, s)) return 0;
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    return f && f->census.on && !scout_sky(v, e, r, f);
}

/* the climb's want (0 none, ST3_WOOD, ST3_OUT); proper the strip's own want (strip_want's before lane/strip3) */
static int st3_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                    const struct neth_rl_state *s, int proper)
{
    int pm = provision_mode(v, e, r, s), climb = pm && provision_under(v, e, r);
    int food_surface = climb || (MOPT(MO_SH_FOOD) == SHW_SURFACE && e->sh.fwait &&
                                 (r->y < FOOD_SCOUT_Y || e->st3_out));
    /* lane/armed (wood_bare=climb): the wood errand's climb with no pickaxe too, on the bare exit's cobblestone */
    int bare_wood = MOPT(MO_WOOD_BARE) && !pm && st3_tier(v, s) < 1 && inv_count(s, COBBLE) >= ST3_PIL_COBBLE;
    if (!(v->macros & NETH_VEC_STRIP2) || r->dim != 0 || r->screen != PS_NONE || s->eye_water || r->y < 4.0 ||
        (e->steps < e->soff && !climb) || (sh_owns(v, e, r) && !food_surface && !pm) ||
        (st3_tier(v, s) < 1 && !(climb && inv_count(s, COBBLE) >= ST3_PIL_COBBLE) &&
         !(bare_wood && st3_wood_want(v, e, s))))
        return 0;
    if (climb) return ST3_OUT;
    if (food_surface) return ST3_OUT; /* only the climb is allowed while the stronghold waits for food */
    if (pm == PV_STOCK && st3_provision_wood_want(v, e, s)) return ST3_WOOD;
    if (pm) return 0;
#ifndef ST3_KEEP6
    if (st3_wood_want(v, e, s)) return ST3_WOOD;
#else
    (void)st3_wood_want;
#endif
    if (proper) return 0;
    /* lane/pearlgo: pearl_night_first, the night's hunt wanted: out from under the surface's height, not only the depth */
    /* (lane/chain food_oracle: the provisions too) */
    return r->y < (pg_surface(v, r, s) || prov_want(v, e, r, s) ? PEARL_SURF_Y : ST3_OUT_Y) || e->st3_out ||
                   st3_sky_due(v, e, r, s)
               ? ST3_OUT
               : 0;
}

/* the climb's step, under strip() before its set is judged: the mode latched and the books */
static void st3_step(struct vec_env *e, int m3)
{
    if (m3 == ST3_WOOD && e->st3 != ST3_WOOD)
    {
        e->st_err[0] += 1;
        e->st3_top = -1;
        e->st3_at = e->steps;
    }
    if (m3 == ST3_OUT)
    {
        e->st_out_steps += 1;
        if (!e->st3_out) e->st3_top = -1, e->st3_at = e->steps;
        e->st3_out = 1;
    }
    e->st3 = m3;
}

/* a block a stair up never digs over the head: sand, gravel (they fall on the player) and clay, the floors of lakes and
 * seas (the first round's climbs dug up under water and drowned: 10 first deaths at y 60 to 63, all the surface
 * macro's swim) */
static int st3_lakebed(int b) { return b == GRAVEL || b == 12 || b == 82; }

/* ------------------------------------------------------------------------------------------------------------------
 * climb_way pillar (lane/pearlhunt's st3_pillar, unmerged there, ported by lane/diamond for both climbs: out of the depth
 * and the wood errand): a block dug a level. Each level: the look straight up, the block over the head (the feet's cell
 * + 2) dug with the strip's pickaxe; then the look straight down, cobblestone in the hand, a jump, and use pressed once
 * the feet are a block up (the block placed where the feet stood); landed a block higher, the next level. The stair
 * (st_up_cell's set) digs three a level and turns often (lane/pearlhunt measured 5.1 uses a level): a stone pickaxe's
 * 131 uses never took a wood errand from y 12 to the surface. What it reads: the feet's height, the crosshair's block,
 * the liquid ray, the inventory. Its constants: */
#define ST3_PIL_TOP 52.0        /* pillared only with the feet under this (the stair walks the last levels out) */
#define ST3_PIL_BARE_TOP 80.0   /* the opt-in food exit's hand cannot finish the stair's stone before its turn */
enum {
    ST3_PIL_LIQ = 3,            /* a liquid in the column this far over the head's cell: the stair instead */
    ST3_PIL_OFF = 400,          /* steps the pillar waits after it gave a level up (the stair climbs meanwhile) */
    ST3_PIL_STEP = 160,         /* steps a level may take */
    ST3_PIL_BARE_STEP = 240,    /* the emergency hand's stone takes 150 ticks before it breaks */
    ST3_PIL_FAILS = 2,          /* jumps that came down short before a level is given up */
    ST3_PIL_AIR = 40,           /* lane/diamond: steps a level may wait to land before it begins (off the ground: the
                                 * shaft dug into water under a lake and the player afloat in it; the wait had no end
                                 * and held 13 of 57 timed-out runs of the 40-minute eval at y 50 to 52) */
};
enum { CWQ_LIQUID, CWQ_BLOCK, CWQ_TIME, CWQ_JUMPS, CWQ_AIR };   /* a give-up's why (cw_qwhy) */

static int st3_pillar_quit(struct vec_env *e, int why)
{
    e->cw_ph = 0;
    e->cw_fail = 0;
    e->cw_off = e->steps + ST3_PIL_OFF;
    e->cw_quit += 1;
    e->cw_qwhy[why] += 1;
    e->sk = -1;
    return 0;
}

/* lane/bportal (climb_land centre): the move keys toward the middle of the level's column (along the look and across
 * it; the look's yaw is the player's own); 1 when the feet are within 0.18 of it on both axes, the 0.6-wide body inside
 * the column */
static int st3_centre(const struct vec_env *e, const struct vec_last *r, struct neth_rl_act *a)
{
    double yr = (double)r->yaw * M_PI / 180.0, fwx = -sin(yr), fwz = cos(yr), rx = -cos(yr), rz = -sin(yr);
    double ox = r->x - (e->cw_cx + 0.5), oz = r->z - (e->cw_cz + 0.5);
    double fw = ox * fwx + oz * fwz, ac = ox * rx + oz * rz;
    if (fw > 0.08) a->hold |= 1u << PK_BACK;
    else if (fw < -0.08) a->hold |= 1u << PK_FORWARD;
    if (ac > 0.08) a->hold |= 1u << PK_LEFT;
    else if (ac < -0.08) a->hold |= 1u << PK_RIGHT;
    return fabs(ox) < 0.18 && fabs(oz) < 0.18;
}

/* 1 when the step is the pillar's (the act set), 0 the stair's */
static int st3_pillar(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a)
{
    int hb = -1;
    for (int i = 0; i < 9 && hb < 0; ++i)
        if (s->inv[i].item == COBBLE && s->inv[i].count > 0) hb = i;
    /* lane/diamond: the top judged by the level's base while it jumps and places (the feet in the air pass y 52 before
     * the place at base + 1.05: from a base at 51 the pillar stopped mid-jump, nothing placed, the player fell back to
     * 51 every 16 steps for the rest of the episode, no give-up counted; 13 of 57 timed-out runs of the 40-minute eval
     * ended at y 50 to 52) */
    double top_y = e->cw_ph >= 2 && e->steps - e->cw_last <= 1 ? e->cw_jbase : r->y;
    int bare_exit = (provision_mode(v, e, r, s) == PV_SURFACE || MOPT(MO_WOOD_BARE)) && st3_tier(v, s) < 1;
    double top_limit = MOPT(MO_PEARL_SHORE_EXIT) && e->pearl.ph_leg >= 2 ? e->pearl.ph_ly :
                       bare_exit ? ST3_PIL_BARE_TOP : ST3_PIL_TOP;
    if (e->steps < e->cw_off || top_y >= top_limit || hb < 0 ||
        inv_count(s, COBBLE) < ST3_PIL_COBBLE || s->eye_water)
    {
        e->cw_ph = 0;
        return 0;
    }
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    if (e->steps - e->cw_last > 1) e->cw_ph = 0;     /* the step before was not the pillar's: the level from its start */
    e->cw_last = e->steps;
    if (e->cw_ph == 0)
    {
        if (!r->on_ground)
        {
            if (++e->cw_fail > ST3_PIL_AIR) return st3_pillar_quit(e, CWQ_AIR);
            a->look_mode = 0;
            return 1;                                 /* landed first */
        }
        e->cw_ph = 1;
        e->cw_cx = fx, e->cw_cz = fz;               /* lane/bportal (climb_land centre): the level's column */
        e->cw_t = e->steps;
        e->cw_look = e->steps;      /* lane/diamond: steps, not the strip's age (it begins again at every restart) */
        e->cw_fail = 0;
    }
    if (e->steps - e->cw_t > (bare_exit ? ST3_PIL_BARE_STEP : ST3_PIL_STEP))
        return st3_pillar_quit(e, CWQ_TIME);
    a->look_mode = 1;
    a->look[0] = r->yaw;
    if (e->cw_ph == 1)
    {
        a->look[1] = -90.0f;                          /* straight up: the head's cell + 1 */
        int centre = MOPT(MO_CLIMB_LAND) == 2, ctr = centre ? st3_centre(e, r, a) : 1;
        if (e->steps - e->cw_look < 2) return 1;
        if (s->lq_hit && s->lq_block >= 8 && s->lq_block <= 11 && s->lq_x == fx && s->lq_z == fz && s->lq_y >= fy + 2 &&
            s->lq_y <= fy + 1 + ST3_PIL_LIQ)
            return st3_pillar_quit(e, CWQ_LIQUID);
        if (s->mo_hit && s->mo_x == fx && s->mo_z == fz && s->mo_y == fy + 2)
        {
            int b = s->mo_block;
            if (b == 7 || b == OBSIDIAN || st3_lakebed(b)) return st3_pillar_quit(e, CWQ_BLOCK);
            int fetch = 0, best = st_tool(v, e, s, b, &fetch);
            if (fetch)
            {
                st_begin(e, a, ST_FETCH);
                return 1;
            }
            a->hold = (centre ? a->hold : 0u) | 1u << PK_ATTACK;
            if (best >= 0 && best != s->current) a->hotbar = best;
            return 1;
        }
        if (!ctr) return 1;                           /* lane/bportal (climb_land centre): a jump begins centred */
        e->cw_ph = 2;                                 /* clear over the head: the jump */
        e->cw_look = e->steps;
        e->cw_jbase = r->y;
        e->cw_jpeak = r->y;
    }
    a->look[1] = 90.0f;                               /* straight down: the floor's top */
    if (hb != s->current) a->hotbar = hb;
    if (e->cw_ph == 2)
    {
        if (e->steps - e->cw_look < 2) return 1;
        a->hold = 1u << PK_JUMP;
        if (MOPT(MO_CLIMB_LAND) == 2) st3_centre(e, r, a);   /* lane/bportal (climb_land centre) */
        if (r->y > e->cw_jpeak) e->cw_jpeak = r->y;
        if (r->y > e->cw_jbase + 1.05 && s->current == hb)
        {
            a->press[PK_USE] = 1;
            e->cw_ph = 3;
            return 1;
        }
        if (r->on_ground && e->cw_jpeak > e->cw_jbase + 0.1 && r->y < e->cw_jbase + 0.05)
        {
            e->cw_jpeak = r->y;
            if (++e->cw_fail >= ST3_PIL_FAILS) return st3_pillar_quit(e, CWQ_JUMPS);
        }
        return 1;
    }
    /* cw_ph 3: placed; landed a block up, the level done; landed where it jumped from, the jump again */
    /* lane/bportal (climb_land cell): the level by the feet's cell, one over the base's: a column stood on at y n.0156
     * gave every base that fraction, and the block placed under it landed the feet at n + 1.0000, 0.0156 off the exact
     * test; the level waited out its 160 steps, the stair took over and gave up with the player on the pillar's top
     * (seed 2's obsidian start under obsidian_carry fallback: 29 of 40 deaths a fall of 12+ blocks from y 34) */
    int landed = MOPT(MO_CLIMB_LAND) ? (int)floor(r->y + 0.01) == (int)floor(e->cw_jbase + 0.01) + 1
                                     : fabs(r->y - (e->cw_jbase + 1.0)) < 0.01;
    if (r->on_ground && landed)
    {
        e->cw_ph = 0;
        e->cw_levels += 1;
        e->sk = -1;
        return 1;
    }
    if (r->on_ground && r->y < e->cw_jbase + 0.5)
    {
        e->cw_ph = 2;
        e->cw_jpeak = r->y;
        if (++e->cw_fail >= ST3_PIL_FAILS) return st3_pillar_quit(e, CWQ_JUMPS);
    }
    return 1;
}

/* the climb walked into its set's cell: the surface reached with the feet at ST3_SURF_Y (a stair up there too: the first
 * round waited for a set that was not one, and in hills the follow ran on for up to 17,000 steps, wearing the diamond
 * pickaxe out on the way) */
static void st3_walked(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, double surf_y, int pm)
{
    if (pm && provision_under(v, e, r)) return;
    if (r->y < surf_y || e->st3_top >= 0) return;     /* (surf_y: ST3_SURF_Y, or lane/pearlgo's PEARL_SURF_Y) */
    e->st3_top = e->steps;
    if (e->st3 == ST3_WOOD) e->st_err[1] += 1;
    e->st3_out = 0;                     /* out of the depth: the step given back */
}

/* the guard (lane/strip3, every strip step): the case's "other" first deaths on strip steps were a mob out of view
 * (traced, s9 seed 1 env 3: health down 3 every 12 steps or so in a clear 1x2 tunnel, nothing in view, the strip digging
 * on to the death). A hostile in view within ST3_FOE_R (not a creeper, not an enderman) takes the fight lock (fight(),
 * the hotbar's best weapon); a hurt with no fire and none in view turns the look back once (vis is the look's cone).
 * 1 when the step is the guard's (the act set; *fid the living to lock on, 0 none) */
static int st3_guard(const struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s, struct neth_rl_act *a,
                     int *fid)
{
    *fid = 0;
#ifdef ST3_NOGUARD
    (void)e, (void)r, (void)s, (void)a;
    return 0;                           /* a VARIANT build, measurement only: the guard off */
#endif
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
    {
        int k = s->vis[i].kind;
        if (!fight_kind(k) || k == HK_CREEPER || k == HK_ENDERMAN || k == HK_PIGMAN) continue;
        if (py_dist3(r->x, r->y, r->z, s->vis[i].x, s->vis[i].y, s->vis[i].z) <= ST3_FOE_R)
        {
            *fid = s->vis[i].id;
            return 1;
        }
    }
    if (e->steps - e->st3_hurt > 1 || e->steps - e->st3_back < ST3_BACK_GAP) return 0;
    a->look_mode = 1;
    a->look[0] = (float)remainder((double)r->yaw + 180.0, 360.0);
    a->look[1] = 0.0f;
    return 1;
}

/* the floor's look going down: the far part of its top face (the centre's ray clips the block under the feet) */
static void st_floor_point(const struct vec_env *e, int fl, double *x, double *y, double *z)
{
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0};
    *x = e->scx + 0.5 + ST_FLOOR_FAR * DX[e->sdir];
    *y = fl + ST_FLOOR_TOP;
    *z = e->scz + 0.5 + ST_FLOOR_FAR * DZ[e->sdir];
}


/* ------------------------------------------------------------------------------------------------------------------
 * lane/chain (diamond_oracle on, PRIVILEGED: the search's hint, never a benchmark's): the strip steered to the client
 * world's nearest diamond ore, seen or not. While the strip digs for diamonds (an iron pickaxe, no diamond pickaxe, under
 * 3 diamonds, no obsidian search), every DO_EVERY steps the ore within DO_R across, from DO_BOT to DO_TOP, with no lava
 * in the 3x3x3 round it, nearest first, is the target; the strip's level is the ore's (its tunnel's feet cell) and at
 * each new set its direction is the cardinal axis with more to go. The strip's own rules stay: a quarter turn of theirs
 * (lava, liquid, a floor, no progress) holds the steering DO_HOLD steps, and a target that cost DO_TURNS of them or
 * DO_GIVEUP steps is given up (a ring of 4). Once the ore is in view the seek (seek.c's diamond want) digs it */
#define DO_EVERY 100
#define DO_R 40
#define DO_BOT 5                /* the strip's want ends with the feet under 4 */
#define DO_TOP 15
#define DO_HOLD 30
#define DO_TURNS 8
#define DO_GIVEUP 3000
#define DO_STEER_Y 30.0         /* the feet under this before the target steers */

static int st_do_active(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s)
{
    if (!MOPT(MO_DIA_ORACLE) || !(v->macros & NETH_VEC_STRIP2) || r->dim != 0) return 0;
    /* underground first: steered from the surface, the staircase down went the ore's way into a lake (gold-g5-s42,
     * sampler seed 1: 4 drownings at y 61 within 4 cells of the start, none with the option off) */
    if (r->y >= DO_STEER_Y) return 0;
    if ((v->macros & NETH_VEC_OBSIDIAN) && e->ob.search) return 0;
    return inv_count(s, IRON_PICK) > 0 && inv_count(s, DIAMOND_PICK) == 0 && inv_count(s, DIAMOND) < 3;
}

static int st_do_bad(const struct vec_env *e, int x, int y, int z)
{
    for (int i = 0; i < e->do_nbad && i < 4; ++i)
        if (e->do_bad[i][0] == x && e->do_bad[i][1] == y && e->do_bad[i][2] == z) return 1;
    return 0;
}

static int st_do_bad_cb(const void *ctx, int x, int y, int z)
{
    return st_do_bad((const struct vec_env *)ctx, x, y, z);
}

/* the nearest diamond ore round the feet with no lava in its 3x3x3: 1 found (into e->do_x, do_y, do_z) */
static int st_do_scan(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    return obsview_find_diamond(v->p, id, fx, fy, fz, DO_R, DO_BOT, DO_TOP, st_do_bad_cb, e, &e->do_x, &e->do_y,
                                &e->do_z);
}

/* the strip's level: the target's while one is on, else def */
static int st_do_level(const struct vec_env *e, int def) { return e->do_on ? e->do_y : def; }

/* at the strip's new set: the target kept, given up or picked again, and the direction toward it */
static void st_do_steer(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s)
{
    if (!st_do_active(v, e, r, s))
    {
        e->do_on = 0;
        return;
    }
    int id = (int)(e - v->e);
    if (e->do_on && (e->sturns - e->do_turns > DO_TURNS || e->steps - e->do_t0 > DO_GIVEUP))
    {
        int i = e->do_nbad < 4 ? e->do_nbad++ : (int)(e->do_gaveup % 4);
        e->do_bad[i][0] = e->do_x, e->do_bad[i][1] = e->do_y, e->do_bad[i][2] = e->do_z;
        e->do_gaveup += 1;
        e->do_on = 0;
    }
    if (e->do_on && obsview_block(v->p, id, e->do_x, e->do_y, e->do_z, NULL) != 56) e->do_on = 0;   /* dug */
    if (!e->do_on || e->steps - e->do_at >= DO_EVERY)
    {
        int ox = e->do_x, oy = e->do_y, oz = e->do_z, was = e->do_on;
        e->do_at = e->steps;
        if (st_do_scan(v, e, r))
        {
            if (!was || ox != e->do_x || oy != e->do_y || oz != e->do_z)
            {
                e->do_t0 = e->steps;
                e->do_turns = e->sturns;
                e->do_picks += 1;
            }
            e->do_on = 1;
        }
        else
            e->do_on = 0;
    }
    if (e->sturns != e->do_last)
    {
        e->do_last = e->sturns;
        e->do_hold = e->steps + DO_HOLD;      /* the strip's own turn: its new way walked first */
    }
    if (!e->do_on || e->steps < e->do_hold) return;
    int dx = e->do_x - (int)floor(r->x), dz = e->do_z - (int)floor(r->z);
    if (dx == 0 && dz == 0) return;
    if (abs(dx) >= abs(dz)) e->sdir = dx > 0 ? 3 : 1;
    else e->sdir = dz > 0 ? 0 : 2;
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static void strip2_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    st_observe(c->v, c->e, c->r, c->s);     /* lane/strip2: books only */
}

static int strip2_pack_want(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    if (!(c->v->macros & NETH_VEC_STRIP2) || !MOPT(MO_SH_PACK) || !c->e->sh.fwait || c->r->dim != 0 ||
        c->r->screen != PS_NONE || (c->r->flags & PF_DEAD) || st_free(c->s) >= ST_PACK_FREE_MIN)
        return 0;
    int slots[ST_DROP_MAX];
    return st_surplus(c->v, c->s, slots, ST_DROP_MAX, 1) > 0;
}

static int strip2_m_cand(struct mac_step *c) { return strip2_pack_want(c); }

static int strip2_m_interrupt(struct mac_step *c)
{
    if (c->e->kind != M_NONE && !c->e->st_pack && st_free(c->s) < ST_PACK_HARD_MIN && strip2_pack_want(c))
        mac_end(c->e);
    return 0;
}

static int strip2_m_start(struct mac_step *c)
{
    if (!strip2_pack_want(c)) return 0;
    c->e->st_pack = 1;
    c->e->kind = M_MINE;
    return 1;
}

/* the mine lock's errand (st_tool's fetch begun in mine.c) */
static int strip2_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_MINE || !(c->v->macros & NETH_VEC_STRIP2)) return -1;
    if (c->e->st_pack)
    {
        still(a);
        if (c->e->stf) return st_errand(c->v, c->e, c->r, c->s, a);
        if (st_drop_start(c->v, c->e, c->r, c->s, a)) return 0;
        c->e->st_pack = 0;
        return 1;
    }
    if (!c->e->stf) return -1;
    return st_errand(c->v, c->e, c->r, c->s, a);
}

static uint32_t strip2_m_label(const struct vec_env *e)
{
    return e->kind == M_MINE && e->st_pack ? NETH_VEC_STRIP2 : 0;
}

static void strip2_m_end(struct vec_env *e) { e->st_pack = 0; }

static void strip2_m_reset(struct vec_env *e)
{
    e->st_pack = e->stf = e->stf_age = 0;
    e->stf_off = 0;
    for (int k = 0; k < 6; ++k) e->st_had[k] = -1, e->st_got[k] = 0;
    e->st_cells = e->st_dpick = 0;
    e->st_last = -1;
    e->st_steps = 0;
    e->st_lpos[0] = e->st_lpos[1] = e->st_lpos[2] = 0.0f;
    memset(e->st_held, 0, sizeof e->st_held);
    memset(e->st_err, 0, sizeof e->st_err);
    e->st_table = 0;
    e->st_out_steps = 0;
    e->st_dpick_step = -1;
    e->st3 = e->st3_m = e->st3_out = 0;
    e->st3_at = e->st3_fall = 0;
    e->st3_top = -1;
    e->st3_lhp = 20.0f;
    e->st3_hurt = e->st3_back = -1000;
    e->st3_climb_at = 0, e->st3_swims = 0;      /* lane/diamond: climb_swim */
    e->cw_ph = e->cw_fail = 0;                  /* lane/diamond: climb_way pillar */
    e->cw_t = e->cw_look = e->cw_off = e->cw_last = 0;
    e->cw_jbase = e->cw_jpeak = 0.0;
    e->cw_levels = e->cw_quit = 0;
    memset(e->cw_qwhy, 0, sizeof e->cw_qwhy);
    memset(e->st_dia, 0, sizeof e->st_dia);     /* lane/diamond's books */
    e->st_dia[4] = -1;
    e->st_dia_on = 0, e->st_dia_pk = -1;
    e->st_dia_cell[0] = e->st_dia_cell[1] = e->st_dia_cell[2] = -(1 << 30);
    e->do_on = e->do_x = e->do_y = e->do_z = 0;   /* lane/chain: diamond_oracle */
    e->do_at = e->do_t0 = e->do_hold = 0;
    e->do_turns = e->do_last = e->do_nbad = 0;
    e->do_picks = e->do_gaveup = 0;
    memset(e->do_bad, 0, sizeof e->do_bad);
}

static const struct mac_switch STRIP2_SW[] = {{"strip2", NETH_VEC_STRIP2}, {NULL, 0}};

static const struct vec_macro MAC_STRIP2 = {
    .name = "strip2", .bit = NETH_VEC_STRIP2, .sw = STRIP2_SW, .observe = strip2_m_observe, .cand = strip2_m_cand,
    .interrupt = strip2_m_interrupt, .start = strip2_m_start, .drive = strip2_m_drive, .label = strip2_m_label,
    .end = strip2_m_end,
    .reset = strip2_m_reset,
};
