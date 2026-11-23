/* lane/rod: armor_kit (MO_ARMOR_KIT, off by default; iron: PRIVILEGED, for lane/chain's search). Iron armor for the
 * End: in the overworld with a stone or better pickaxe, the iron the pieces not had want (boots 4, leggings 7, a
 * chestplate 8; then a helmet 5 when the ingots allow) and what the iron plans spend first (a bucket 3, a flint and
 * steel 1) is dug from the nearest iron ore the client world holds within AR_R, no lava or water round it, along the dig
 * walker's tunnel (dig.c: the ore cell is the walk's last cell, so the walk in mines it); the craft macro smelts the
 * ore (iron_plan's C_SMELT, a furnace and fuel held) and makes the pieces on a table (arm_plan: C_IBOOTS .. C_IHELM);
 * a piece held is put on by a shift-click in the inventory screen (vanilla's ContainerPlayer.transferStackInSlot puts
 * armor in its slot). No switch bit is free: the option is the switch (as MAC_AIDS has bit 0).
 *
 * The books (neth_vec_kit): the pieces worn and their points (read off the inventory screen's armor slots 5 .. 8 when
 * the equip opens it), the ores dug, the steps of the first ore, of the ingots wanted held, of three and four pieces worn */
#define AR_R 32                 /* the ore's scan: across */
#define AR_YLO 5
#define AR_YHI 72
#define AR_SCAN_EVERY 20        /* the cand's look for work, steps */
#define AR_GIVEUP 3000          /* steps on one ore before it is given up */
#define AR_EQ_OPEN 30           /* steps for the inventory screen to open */
#define AR_COAL_ORE 16          /* armor_fuel's block */
#define AR_BAD_ALL 64           /* armor_giveup all: the ores held given up at most (arm_env.bad_all) */
#define AR_CF_RETRY 200         /* armor_craft=first: steps after a craft start before another with no new piece */
#define AR_BAD_MOVE 16.0       /* ... the list begun again once the feet are this far (across) from where it began */

static const int AR_ITEM[4] = {IRON_BOOTS, IRON_LEGS, IRON_CHEST, IRON_HELM};
static const int AR_COST[4] = {4, 7, 8, 5};
static const int AR_PLAN[4] = {C_IBOOTS, C_ILEGS, C_ICHEST, C_IHELM};
static const int AR_PTS[4] = {2, 5, 6, 2};
static const int AR_SLOT[4] = {8, 7, 6, 5};     /* ContainerPlayer's armor slots: 5 the helmet .. 8 the boots */

static int arm_ready(const struct neth_vec *v, const struct vec_env *e)
{
    return MOPT(MO_ARMOR_KIT) && (MOPT(MO_ARMOR_TIMING) == 0 || (e->dims & 1));
}

static int arm_on(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    return arm_ready(v, e) && r->dim == 0 && !(r->flags & PF_DEAD);
}

static int arm_had(const struct vec_env *e, const struct neth_rl_state *s, int k)
{
    return ((e->arm.worn >> k) & 1) || inv_count(s, AR_ITEM[k]) > 0;
}

static int arm_tier(const struct neth_vec *v, const struct neth_rl_state *s)
{
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    return tier;
}

/* lane/supply (armor_ipick=first): no iron or diamond pickaxe held while the craft can make one (iron_plan's C_IPICK:
 * NETH_VEC_IRON, its sticks held or two planks for them). sf, the armour base on the flint leg: 238 of 448 attempts
 * ended with no pickaxe of any kind, the iron one worn out by the armour mine and the ingots made into the set first
 * (craft_plan asks arm_plan before iron_plan) */
static int arm_ipick_wait(const struct neth_vec *v, const struct neth_rl_state *s)
{
    return MOPT(MO_ARMOR_IPICK) && (v->macros & NETH_VEC_IRON) && arm_tier(v, s) < 3 &&
           (inv_count(s, STICK) >= 2 || inv_count(s, PLANKS) >= 2);
}

/* the ingots still wanted: three pieces with iron, all four and an iron sword with full; plus the iron plans first */
static int arm_need(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    if (!arm_ready(v, e)) return 0;
    int need = 0;
    for (int k = 0; k < (MOPT(MO_ARMOR_KIT) == 2 ? 4 : 3); ++k)
        if (!arm_had(e, s, k)) need += AR_COST[k];
    if (MOPT(MO_ARMOR_KIT) == 2 && MOPT(MO_SWORD_KIT) == 2 && inv_count(s, 267) == 0) need += 2; /* iron sword */
    if (need == 0) return 0;
    if ((v->macros & NETH_VEC_IRON) && arm_tier(v, s) >= 3 &&
        inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) + inv_count(s, LAVA_BUCKET) == 0)
        need += 3;
    if ((v->macros & NETH_VEC_IRON) && inv_count(s, FLINT) > 0 && inv_count(s, FLINT_STEEL) == 0) need += 1;
    if (arm_ipick_wait(v, s)) need += 3;   /* armor_ipick first: the iron pickaxe's ingots */
    return need;
}

/* lane/supply (armor_ipick=keep): the pickaxes that dig iron ore are only iron or diamond ones and no stone pickaxe can
 * be made from what is held (3 cobblestone with 2 sticks, 2 planks or a log): the armour mine would spend the one the
 * diamonds need. sf's s2stock10 attempt 0: the start's one iron pickaxe (124 uses left, no ingots, 1 stick, no planks)
 * worn out by the armour mine's tunnels by step 2,600, nothing to make another */
static int arm_pick_last(const struct neth_vec *v, const struct neth_rl_state *s)
{
    if (MOPT(MO_ARMOR_IPICK) != 2 || inv_count(s, 274) > 0) return 0;
    int wood = inv_count(s, STICK) >= 2 || inv_count(s, PLANKS) >= 2 || inv_count(s, LOG) + inv_count(s, LOG2) > 0;
    return !(inv_count(s, COBBLE) >= 3 && wood);
}

static int arm_mine_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    int need = arm_need(v, e, s);
    return need > 0 && arm_tier(v, s) >= 2 && inv_count(s, INGOT) + inv_count(s, IRON_ORE) < need &&
           !arm_pick_last(v, s);
}

/* lane/chain2 (armor_fuel coal): the ore held is more than the fuel held smelts (a coal 8, a plank 1.5 from two up)
 * while a piece is still wanted and its ore is all dug */
static int arm_fuel_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    if (!MOPT(MO_ARMOR_FUEL) || arm_mine_want(v, e, s) || arm_need(v, e, s) == 0 || arm_tier(v, s) < 1) return 0;
    int64_t ore = inv_count(s, IRON_ORE), planks = inv_count(s, PLANKS);
    int64_t smelts = 8 * inv_count(s, COAL) + (planks >= 2 ? 3 * planks / 2 : 0);
    return ore > 0 && ore > smelts;
}

/* the block the mine digs for now: iron ore, else coal ore for the iron's fuel; 0 none */
static int arm_target(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    return arm_mine_want(v, e, s) ? IRON_ORE : arm_fuel_want(v, e, s) ? AR_COAL_ORE : 0;
}

/* a piece held and not worn: its index, else -1 */
static int arm_equip_want(const struct vec_env *e, const struct neth_rl_state *s)
{
    for (int k = 0; k < 4; ++k)
        if (!((e->arm.worn >> k) & 1) && inv_count(s, AR_ITEM[k]) > 0) return k;
    return -1;
}

/* craft.c's craft_plan: the next piece the ingots make on a table held or near (the chestplate first: the most points
 * an ingot), the helmet once the three are had */
static int arm_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                    const struct neth_rl_state *s, int64_t tables)
{
    if (!arm_on(v, e, r) || (tables == 0 && !near_table(e, r))) return -1;
    int64_t ingots = inv_count(s, INGOT);
    if (ingots >= 3 && arm_ipick_wait(v, s)) return -1;     /* armor_ipick first: iron_plan's pickaxe before a piece */
    static const int ORDER[4] = {2, 1, 0, 3};
    for (int j = 0; j < 4; ++j)
    {
        int k = ORDER[j];
        if (arm_had(e, s, k)) continue;
        if (k == 3 && !(arm_had(e, s, 0) && arm_had(e, s, 1) && arm_had(e, s, 2))) continue;
        if (ingots >= AR_COST[k]) return AR_PLAN[k];
        if (k != 3) return -1;  /* the next piece waits for its ingots */
    }
    return -1;
}

static int arm_bad(const struct arm_env *m, int x, int y, int z)
{
    for (int i = 0; i < m->nbad && i < 8; ++i)
        if (m->bad[i][0] == x && m->bad[i][1] == y && m->bad[i][2] == z) return 1;
    for (int i = 0; i < m->nbad_all; ++i)    /* armor_giveup all (empty without it) */
        if (m->bad_all[i][0] == x && m->bad_all[i][1] == y && m->bad_all[i][2] == z) return 1;
    return 0;
}

/* the nearest iron ore in the client world within AR_R across, AR_YLO .. AR_YHI, nothing hot or wet in its 3x3x3, not
 * given up: 1 found (into m->ox, oy, oz) */
static int arm_scan(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, int tgt)
{
    struct arm_env *m = &e->arm;
    m->tgt = tgt;
    /* armor_giveup all: the list of every ore given up begun again once an ore is dug or the feet have moved on */
    if (m->nbad_all > 0 && (m->ores_dug != m->bdug0 || py_hypot2(r->x - m->bx0, r->z - m->bz0) > AR_BAD_MOVE))
        m->nbad_all = 0;
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z), found = 0;
    double bd = 1e30;
    for (int y = AR_YLO; y <= AR_YHI; ++y)
        for (int dz = -AR_R; dz <= AR_R; ++dz)
            for (int dx = -AR_R; dx <= AR_R; ++dx)
            {
                int x = fx + dx, z = fz + dz;
                if (netherview_block(v->p, id, x, y, z, NULL) != tgt) continue;
                double d = (double)dx * dx + (double)dz * dz + 4.0 * (double)(y - fy) * (y - fy);
                if (d >= bd || arm_bad(m, x, y, z)) continue;
                int hot = 0;
                for (int k = 0; k < 27 && !hot; ++k)
                    hot = dgw_hot(netherview_block(v->p, id, x + k % 3 - 1, y + k / 3 % 3 - 1, z + k / 9 - 1, NULL));
                if (hot) continue;
                bd = d, found = 1;
                m->ox = x, m->oy = y, m->oz = z;
            }
    return found;
}

static void arm_give_up(const struct neth_vec *v, struct arm_env *m, const struct vec_last *r)
{
    int i = m->nbad < 8 ? m->nbad++ : m->gave_up % 8;
    m->bad[i][0] = m->ox, m->bad[i][1] = m->oy, m->bad[i][2] = m->oz;
    if (MOPT(MO_ARMOR_GIVEUP) && m->nbad_all < AR_BAD_ALL)
    {
        if (m->nbad_all == 0) m->bx0 = r->x, m->bz0 = r->z, m->bdug0 = m->ores_dug;
        int *b = m->bad_all[m->nbad_all++];
        b[0] = m->ox, b[1] = m->oy, b[2] = m->oz;
    }
    m->gave_up += 1, m->has_ore = 0;
    if (m->dw.cells == 0) m->gu_noway += 1;   /* books (lane/supply): no cell walked since the pick */
    else m->gu_walked += 1;
}

/* the books' steps (every step the switch is on) */
static void arm_books(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s)
{
    struct arm_env *m = &e->arm;
    if (m->t_ore1 < 0 && inv_count(s, IRON_ORE) > 0) m->t_ore1 = e->steps;
    int need = arm_need(v, e, s);
    if (m->t_ingots < 0 && need > 0 && inv_count(s, INGOT) >= need) m->t_ingots = e->steps;
    int n3 = (m->worn & 7) == 7, n4 = m->worn == 15;
    if (m->t_worn3 < 0 && n3) m->t_worn3 = e->steps;
    if (m->t_worn4 < 0 && n4) m->t_worn4 = e->steps;
}

static int arm_table_take(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s);
static int arm_craft_first(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                           const struct neth_rl_state *s);

/* lane/supply (armor_pick=fetch): no pickaxe in the hotbar digs TGT (iron ore a stone one's tier, coal ore a wooden
 * one's) and the main inventory holds one: the best of them swapped into the hotbar slot the macros need least (snow.c
 * sn_fetch's rule: no tool, no food, no craft material) by a number key's click under the inventory screen, opened by
 * its key (again after 40 steps unopened). al's s1cont150 iron_pickaxe: the hotbar's wooden and stone pickaxes worn
 * out, the iron one in the pack, every block after dug by hand with no drop. 1 fetching (the act set), 0 nothing to do */
static int arm_pick_fetch(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a, int tgt)
{
    struct arm_env *m = &e->arm;
    int need = tgt == IRON_ORE ? 2 : 1, hb = 0, src = -1, st = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 && s->inv[i].count > 0 ? v->T.pick[it] : 0;
        if (i < 9) hb = t > hb ? t : hb;
        else if (t >= need && t > st) st = t, src = i;
    }
    if (hb >= need || src < 0) return 0;
    if (r->screen == PS_INVENTORY)
    {
        int to = 8, best = 1 << 30;
        for (int i = 8; i >= 0; --i)
        {
            int it = s->inv[i].item, itc = it < 0 ? 0 : it > 4095 ? 4095 : it, score = 0;
            if (it >= 0 && s->inv[i].count > 0)
            {
                int tool = v->T.pick[itc] || v->T.axe[itc] || v->T.shovel[itc] || it == 268 || it == 272 ||
                           it == 267 || it == 276 || it == 283;
                int food = it == 364 || it == 320 || it == 366 || it == 297 || it == 260;
                score = tool ? 100 : food ? 50 : v->T.craft[itc] || v->T.craft_iron[itc] || it == COAL || it == TABLE ? 10 : 1;
            }
            if (score < best) best = score, to = i;
        }
        a->nops = 2;
        a->ops[0] = click(inv_slot(src, 0), to, 2);
        a->ops[1] = CLOSE;
        m->pk_press = 0, m->pk_fetched += 1;
        return 1;
    }
    if (r->screen == PS_NONE)
    {
        if (m->pk_press++ % 40 == 0) a->press[PK_INVENTORY] = 1;
        return 1;
    }
    return 0;
}

/* the run: 1 over */
static int armor(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                 struct neth_rl_act *a, double ey)
{
    struct arm_env *m = &e->arm;
    still(a);
    m->age += 1;
    if (!arm_on(v, e, r)) return 1;
    if (m->phase == AR_EQUIP)
    {
        if (r->screen == PS_INVENTORY)
        {
            /* the armor slots read: the pieces worn; a piece in the main inventory or the hotbar shift-clicked in */
            int worn = 0, pts = 0;
            for (int k = 0; k < 4; ++k)
                if (r->slot_item[AR_SLOT[k]] == AR_ITEM[k]) worn |= 1 << k, pts += AR_PTS[k];
            m->worn = worn, m->points = pts, m->read = 1;
            arm_books(v, e, s);
            if (++m->eq_age % 3 != 1) return 0;     /* a click every third step: the slots seen to move between */
            for (int k = 0; k < 4; ++k)
            {
                if ((worn >> k) & 1) continue;
                for (int i = 0; i < 36; ++i)
                    if (s->inv[i].item == AR_ITEM[k])
                    {
                        a->nops = 1;
                        a->ops[0] = click(inv_slot(i, 0), 0, 1);
                        return 0;
                    }
            }
            a->nops = 1;
            a->ops[0] = CLOSE;
            return 1;
        }
        if (r->screen != PS_NONE || m->age > AR_EQ_OPEN) return 1;
        if (m->age == 1) a->press[PK_INVENTORY] = 1;
        return 0;
    }
    /* AR_MINE */
    int tgt = arm_target(v, e, s);
    if (!tgt) return 1;
    if (arm_table_take(v, e, r, s)) return 1;       /* armor_table=take: the run ends, mac_start takes the table */
    if (e->steps % AR_SCAN_EVERY == 0 && arm_craft_first(v, e, r, s) >= 0) return 1;   /* armor_craft=first */
    if (MOPT(MO_ARMOR_PICK) && arm_pick_fetch(v, e, r, s, a, tgt)) return 0;
    if (r->screen != PS_NONE) return 1;
    int id = (int)(e - v->e);
    if (m->has_ore && m->tgt != tgt) m->has_ore = 0;     /* (armor_fuel: the iron done, the coal's turn) */
    if (m->has_ore && netherview_block(v->p, id, m->ox, m->oy, m->oz, NULL) != tgt)
        m->has_ore = 0, m->ores_dug += 1,           /* dug (by the walk in, or the vein's neighbour's) */
        m->reached += MOPT(MO_ARMOR_REACH) && m->reach_miss < 3 && m->dw.cells == 0;
    if (m->has_ore && e->steps - m->ore_t0 > AR_GIVEUP) arm_give_up(v, m, r);
    if (!m->has_ore)
    {
        if (!arm_scan(v, e, r, tgt)) return 1;
        m->has_ore = 1, m->ore_t0 = e->steps, m->picks += 1, m->reach_miss = 0, m->reach_aim = 0;
        dgw_reset(&m->dw);
    }
    /* lane/supply (armor_reach): the ore within reach and seen is dug from here: looked at; while the crosshair is on
     * it, the attack held with the best tool; a look landing elsewhere three times gives the reach up for this ore */
    if (MOPT(MO_ARMOR_REACH) && m->reach_miss < 3 && r->on_ground &&
        !((int)floor(r->x) == m->ox && (int)floor(r->z) == m->oz) &&
        py_dist3(r->x, ey, r->z, m->ox + 0.5, m->oy + 0.5, m->oz + 0.5) <= 4.0)
    {
        double yaw, pitch;
        look_at(r->x, ey, r->z, m->ox + 0.5, m->oy + 0.5, m->oz + 0.5, &yaw, &pitch);
        int on = s->mo_hit && s->mo_x == m->ox && s->mo_y == m->oy && s->mo_z == m->oz;
        if (!on && m->reach_aim) m->reach_miss += 1;
        if (m->reach_miss < 3)
        {
            still(a);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = (float)pitch;
            m->reach_aim = 1;
            if (on)
            {
                int tb = dig_tool(v, s, tgt);
                if (tb >= 0 && tb != s->current) a->hotbar = tb;
                a->hold = 1u << PK_ATTACK;
                m->reach_aim = 0;
            }
            return 0;
        }
    }
    int w = dgw_step(v, e, r, s, a, ey, &m->dw, m->ox, m->oy, m->oz);
    if (w < 0)
    {
        arm_give_up(v, m, r);
        still(a);
    }
    return 0;
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

/* every step with the option on: the books (the inventory's iron and the run, for kit_case's trace) */
static void arm_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    (void)a;
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    if (!MOPT(MO_ARMOR_KIT)) return;
    struct arm_env *m = &e->arm;
    /* An equip click can be followed by a save seam before this controller reads the open inventory again.
     * The client's worn slots are authoritative on every observation, including a resumed node. */
    {
        const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
        if (f)
        {
            int worn = 0, pts = 0;
            for (int k = 0; k < 4; ++k)
                if (f->armor_item[k] == AR_ITEM[k]) worn |= 1 << k, pts += AR_PTS[k];
            m->worn = worn, m->points = pts;
        }
    }
    m->n_ore = (int)inv_count(c->s, IRON_ORE), m->n_ingot = (int)inv_count(c->s, INGOT);
    m->n_pieces = 0;
    for (int k = 0; k < 4; ++k) m->n_pieces += (int)inv_count(c->s, AR_ITEM[k]);
    m->kind = e->kind, m->cplan = e->kind == M_CRAFT ? e->cplan : -1;
    arm_books(v, e, c->s);
}

/* lane/supply (armor_turn=station): the armour's turn ahead of the strip mine: the switch on, an iron pickaxe held,
 * pieces still wanted, and a furnace and a table each held or in reach (the macro's own near_furnace / near_table).
 * Not pearl_home's land latch (pearl.c ph_land, the cand's): it is 0 before the pearl stage, and reading the land
 * here would be a new privileged read; the station in reach stands for it */
static int arm_turn(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                    const struct neth_rl_state *s)
{
#ifdef ARM_TURN_TRACE
    if (e == v->e && e->steps % 200 == 0)
        fprintf(stderr, "ATN %lld on %d scr %d tier %d need %d fur %d/%d tab %d/%d land %d kind %d\n",
                (long long)e->steps, arm_on(v, e, r), r->screen, arm_tier(v, s), arm_need(v, e, s),
                (int)inv_count(s, FURNACE), near_furnace(e, r), (int)inv_count(s, TABLE), near_table(e, r),
                e->pearl.ph_land, e->kind);
#endif
    if (!(MOPT(MO_ARMOR_TURN) && arm_on(v, e, r) && r->screen == PS_NONE && arm_tier(v, s) >= 3 &&
          arm_need(v, e, s) > 0))
        return 0;
    int table = inv_count(s, TABLE) > 0 || near_table(e, r), furnace = inv_count(s, FURNACE) > 0 || near_furnace(e, r);
    if (MOPT(MO_ARMOR_TURN) == 2 && (!table || !furnace))
    {
        /* armor_turn=make (lane/supply): the station as it can be made from what is held (carry_gate's test): a table
         * from 4 planks or a log, a furnace from 8 cobblestone with a table (iron_plan makes it once the first ore is
         * in hand). ap: 13 of the 14 armour starts begin with no furnace, 49 of the 66 limits that never took the turn
         * never had one held or in reach */
        int64_t logs = inv_count(s, LOG) + inv_count(s, LOG2), planks = inv_count(s, PLANKS);
        table = table || planks >= 4 || logs > 0;
        furnace = furnace || (table && inv_count(s, COBBLE) >= 8);
    }
    return table && furnace;
}

/* lane/supply (armor_table=take): the placed table taken before the armour mine walks away from it: the switch on, the
 * carry and craft macros on, pieces still wanted, none held, one placed in reach (near_table), no piece craftable now,
 * the take's own backoff (take_retry) over. ap (armor_pick=fetch): 122 of 167 attempts holding 24 ingots and no boots
 * at the limit had held a table and had one in reach, and at the end had neither; 81 last left a placed one under the
 * mine. The craft macro's C_TAKE (NETH_VEC_CARRY) takes it only when it gets a start, and the armour run picks ore
 * after ore without one */
static int arm_table_take(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s)
{
    return MOPT(MO_ARMOR_TABLE) && (v->macros & NETH_VEC_CARRY) && (v->macros & NETH_VEC_CRAFT) && arm_on(v, e, r) &&
           r->screen == PS_NONE && e->steps >= e->take_retry && arm_need(v, e, s) > 0 && inv_count(s, TABLE) == 0 &&
           near_table(e, r) && arm_plan(v, e, r, s, 0) < 0;
}

/* lane/supply (armor_craft=first): the plan of an armour piece craftable now (arm_plan: its ingots held, a table held or
 * in reach), else -1; the switch on, the craft macro on, no screen open, and a start that crafted nothing (the pieces
 * had no more than at it) waited out AR_CF_RETRY steps. tm: 123 of 185 attempts stopped at 24 ingots with no boots had
 * a piece craftable while the armour mine (its want counts the sword's, the bucket's and the flint's ingots past the
 * 24), the obsidian or the strip owned, and walked off from a table nobody carried */
static int arm_had_n(const struct vec_env *e, const struct neth_rl_state *s)
{
    int n = 0;
    for (int k = 0; k < 4; ++k) n += arm_had(e, s, k);
    return n;
}

static int arm_craft_first(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                           const struct neth_rl_state *s)
{
    if (!MOPT(MO_ARMOR_CRAFT) || v->picks || !(v->macros & NETH_VEC_CRAFT) || !arm_on(v, e, r) ||
        r->screen != PS_NONE)
        return -1;
    if (e->steps < e->arm.cf_retry && arm_had_n(e, s) <= e->arm.cf_had) return -1;
    return arm_plan(v, e, r, s, inv_count(s, TABLE));
}

/* ... its commit: the plan arm_craft_first chose, the armour's retry books once, then the craft begun (mac_start) */
static void arm_craft_commit(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s, int p)
{
    e->arm.cf_had = arm_had_n(e, s), e->arm.cf_retry = e->steps + AR_CF_RETRY, e->arm.cf_starts += 1;
    craft_begin(v, e, s, p, CP_OPEN);
}

/* lane/choice: the armour craft slot's offers (read only: no field of the env, its books or any RNG written). The
 * armour piece (ACH_ARMOR) is arm_craft_first's plan; the replacement iron pickaxe (ACH_IPICK) is offered beside it
 * only on a conflict: the one iron pickaxe held is damaged, no other iron or diamond pickaxe is held, the craft could
 * make an iron pickaxe now (plan_ok's C_IPICK: 3 ingots, 2 sticks, a table held or in reach, the iron macro on), and
 * the ingots held cannot pay for both (the piece's cost + 3). Inputs: the 36-slot inventory with its damage (the
 * server's, PRIVILEGED for a pixels-only policy, as the armour's own reads are) and the macros' table memory.
 * Returns the offers as a bit mask (1 << ACH_*); *plan the armour's plan, *dmg the iron pickaxe's damage */
static int arm_choice_query(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                            const struct neth_rl_state *s, int *plan, int *dmg)
{
    int p = arm_craft_first(v, e, r, s);
    *plan = p, *dmg = -1;
    if (p < 0) return 0;
    int offers = 1 << ACH_ARMOR, iron = 0, dia = 0, d = -1;
    for (int i = 0; i < 36; ++i)
    {
        if (s->inv[i].item == IRON_PICK) iron += s->inv[i].count, d = s->inv[i].damage;
        if (s->inv[i].item == DIAMOND_PICK) dia += s->inv[i].count;
    }
    int cost = 0;
    for (int k = 0; k < 4; ++k)
        if (AR_PLAN[k] == p) cost = AR_COST[k];
    if (iron == 1 && d > 0 && dia == 0 && plan_ok(v, e, r, s, C_IPICK) && inv_count(s, INGOT) < cost + 3)
        offers |= 1 << ACH_IPICK, *dmg = d;
    return offers;
}

/* armor_choice=check's digest: every byte of the env but its choice books (FNV-1a) */
static uint64_t arm_choice_digest(const struct vec_env *e)
{
    const unsigned char *b = (const unsigned char *)e;
    size_t lo = offsetof(struct vec_env, ach), hi = lo + sizeof e->ach;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < sizeof *e; ++i)
        if (i < lo || i >= hi) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

/* lane/choice: the armour craft slot (mac_start, after furnace_kit=carry's take, before armor_table=take's): the
 * offers, then one commit. The selector is the run's armor_choice: armor (the default: today's armour piece, bit for
 * bit), ipick (the replacement pickaxe on a conflict), check (armor, with the query run twice and the env's bytes and
 * the offers compared), invalid (a test's: asks for an option never offered; the default is committed and counted) */
static int arm_choose(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    int plan, dmg;
    uint64_t h0 = MOPT(MO_ARMOR_CHOICE) == ACS_CHECK ? arm_choice_digest(e) : 0;
    int offers = arm_choice_query(v, e, r, s, &plan, &dmg);
    if (!offers) return 0;
    struct arm_choice *ch = &e->ach;
    if (MOPT(MO_ARMOR_CHOICE) == ACS_CHECK)
    {
        int plan2, dmg2, offers2 = arm_choice_query(v, e, r, s, &plan2, &dmg2);
        ch->check_n += 1;
        if (arm_choice_digest(e) != h0 || offers2 != offers || plan2 != plan || dmg2 != dmg) ch->check_diff += 1;
    }
    ch->queries += 1;
    int want = MOPT(MO_ARMOR_CHOICE) == ACS_IPICK ? ACH_IPICK : MOPT(MO_ARMOR_CHOICE) == ACS_INVALID ? ACH_N : ACH_ARMOR;
    int pick = ACH_ARMOR, why = ACHR_DEFAULT;
    if (want != ACH_ARMOR && (offers & (1 << ACH_IPICK)))
    {
        if (want < ACH_N && (offers & (1 << want))) pick = want, why = ACHR_SELECTED;
        else ch->invalid += 1, why = ACHR_INVALID;
    }
    if (offers & (1 << ACH_IPICK))
    {
        ch->conflicts += 1, ch->last_step = e->steps, ch->last_plan = plan, ch->last_dmg = dmg;
        ch->last_ingots = (int)inv_count(s, INGOT), ch->last_sticks = (int)inv_count(s, STICK);
        ch->last_chose = pick, ch->last_why = why;
    }
    ch->chose[pick] += 1;
    if (pick == ACH_IPICK) craft_begin(v, e, s, C_IPICK, CP_OPEN);   /* the armour's retry books untouched */
    else arm_craft_commit(v, e, s, plan);
    return 1;
}

static int arm_m_cand(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    if (!arm_on(v, e, c->r)) return 0;
    if (e->kind == M_MINE && e->arm.on) return 1;
    if (c->r->screen != PS_NONE) return 0;
    /* lane/chain: with pearl_home, begun only on land (its leg's check, pearl.c ph_land): from a state at sea the
     * dig toward the ore under the sea floor drowned 21 of 32 envs at y 50 (lane/pearlhome, e42y node 373) */
    if (MOPT(MO_PEARL_HOME) && !e->pearl.ph_land) return 0;
    return arm_equip_want(e, c->s) >= 0 || (e->steps % AR_SCAN_EVERY == 0 && arm_target(v, e, c->s));
}

static int arm_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    if (!arm_on(v, e, c->r) || c->r->screen != PS_NONE) return 0;
    struct arm_env *m = &e->arm;
    int phase = arm_equip_want(e, s) >= 0 ? AR_EQUIP : 0;
    int tgt = phase ? 0 : arm_target(v, e, s);
    if (!phase && e->steps % AR_SCAN_EVERY == 0 && tgt)
    {
        if (m->has_ore && m->tgt != tgt) m->has_ore = 0;
        if (!m->has_ore && !arm_scan(v, e, c->r, tgt)) return 0;
        if (!m->has_ore) m->has_ore = 1, m->ore_t0 = e->steps, m->picks += 1, dgw_reset(&m->dw);
        phase = AR_MINE;
    }
    if (!phase) return 0;
    e->kind = M_MINE;
    m->on = 1, m->phase = phase, m->age = 0, m->eq_age = 0;
    return 1;
}

static int arm_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    struct vec_env *e = c->e;
    if (e->kind != M_MINE || !e->arm.on) return -1;
    arm_books(c->v, e, c->s);
    return armor(c->v, e, c->r, c->s, a, c->ey);
}

static void arm_m_end(struct vec_env *e) { e->arm.on = 0, e->arm.phase = 0; }

static void arm_m_reset(struct vec_env *e)
{
    memset(&e->arm, 0, sizeof e->arm);
    memset(&e->ach, 0, sizeof e->ach);      /* lane/choice's books */
    e->ach.last_step = -1;
    dgw_reset(&e->arm.dw);
    e->arm.t_ore1 = e->arm.t_ingots = e->arm.t_worn3 = e->arm.t_worn4 = -1;
}

static const struct vec_macro MAC_ARMOR = {
    .name = "armor", .bit = 0, .observe = arm_m_observe, .cand = arm_m_cand, .start = arm_m_start, .drive = arm_m_drive, .end = arm_m_end,
    .abort = arm_m_end, .reset = arm_m_reset,
};

/* vecenv.h: env id's armor_kit books */
int neth_vec_kit(const struct neth_vec *v, int id, struct neth_vec_kit *out)
{
    memset(out, 0, sizeof *out);
    if (v == NULL || id < 0 || id >= v->n) return -1;
    const struct vec_env *e = &v->e[id];
    const struct arm_env *m = &e->arm;
    out->worn = m->worn, out->points = m->points, out->ores_dug = m->ores_dug, out->gave_up = m->gave_up;
    out->t_ore1 = m->t_ore1, out->t_ingots = m->t_ingots, out->t_worn3 = m->t_worn3, out->t_worn4 = m->t_worn4;
    out->plans = m->dw.plans, out->cells = m->dw.cells;
    out->pos[0] = e->last.x, out->pos[1] = e->last.y, out->pos[2] = e->last.z;
    out->dead = (e->last.flags & PF_DEAD) != 0;
    out->n_ore = m->n_ore, out->n_ingot = m->n_ingot, out->n_pieces = m->n_pieces, out->kind = m->kind;
    out->cplan = m->cplan, out->on = m->on;
    out->n_snow = m->n_snow, out->sn_dug = m->sn_dug, out->sn_gave_up = m->sn_gave_up;      /* snow_kit's (snow.c) */
    out->t_shovel = m->t_shovel, out->t_snow16 = m->t_snow16, out->t_snow64 = m->t_snow64;
    out->sn_quit = m->sn_quit, out->sn_col = m->sn_col, out->sn_col_d = m->sn_col_d;      /* lane/snowleg */
    out->sn_trip_steps = m->sn_trip_steps, out->sn_noway = m->sn_noway, out->sn_logs = m->sn_logs;
    out->sn_end_snow = m->sn_end_snow, out->sn_w0 = m->sn_w0, out->t_trip0 = m->t_trip0, out->t_trip1 = m->t_trip1;
    out->t_end = m->t_end, out->sn_hops = m->sn_hops, out->sn_dpart = m->sn_dpart, out->sn_dago = m->sn_dago;
    memcpy(out->sn_dpos, m->sn_dpos, sizeof out->sn_dpos);
    out->sn_dair = m->sn_dair, out->sn_dstep = m->sn_dead ? m->sn_dstep : -1;
    out->picks = m->picks, out->gu_noway = m->gu_noway, out->gu_walked = m->gu_walked;   /* lane/supply */
    out->furnace_near = near_furnace(e, &e->last);
    const struct arm_choice *ch = &e->ach;      /* lane/choice */
    out->ch_queries = ch->queries, out->ch_conflicts = ch->conflicts, out->ch_armor = ch->chose[ACH_ARMOR];
    out->ch_ipick = ch->chose[ACH_IPICK], out->ch_invalid = ch->invalid, out->ch_check = ch->check_n;
    out->ch_check_diff = ch->check_diff, out->ch_last_step = ch->last_step, out->ch_last_plan = ch->last_plan;
    out->ch_last_ingots = ch->last_ingots, out->ch_last_sticks = ch->last_sticks, out->ch_last_dmg = ch->last_dmg;
    out->ch_last_chose = ch->last_chose, out->ch_last_why = ch->last_why;
    return 0;
}
