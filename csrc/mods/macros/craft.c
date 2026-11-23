/* NETH_VEC_CRAFT, NETH_VEC_IRON, NETH_VEC_LATE, NETH_VEC_LIQUID, NETH_VEC_CARRY and NETH_VEC_PICK's plans: the craft macro (lane/macromod: cut from vecenv.c, unchanged) */

/* NETH_VEC_LIQUID's plans on what the look's liquid ray lands on (lane/liquidlook): -1 none */
static int liquid_plan(const struct neth_rl_state *s)
{
    if (!s->lq_hit || s->lq_meta != 0) return -1;
    int b = s->lq_block;
    if ((b == 8 || b == 9) && inv_count(s, BUCKET) > 0 && inv_count(s, WATER_BUCKET) == 0) return C_FILL;
    if ((b == 10 || b == 11) && inv_count(s, WATER_BUCKET) > 0) return C_CAST;
    return -1;
}

/* NETH_VEC_LATE's plans (lane/latecraft): -1 none */
static int late_plan(const struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s, int64_t sticks,
                     int64_t tables, int64_t tier)
{
    int64_t pearls = inv_count(s, PEARL), powder = inv_count(s, BLAZE_POWDER);
    if (inv_count(s, BLAZE_ROD) > 0) return C_POWDER;
    if (pearls > 0 && powder > 0) return C_EYE;
    if (tier < 4 && inv_count(s, DIAMOND) >= 3 && sticks >= 2 && (tables > 0 || near_table(e, r))) return C_DPICK;
    return -1;
}

/* lane/food2 (food_cook furnace): raw pork, beef and chicken smelted at a furnace in reach or held, with nothing to
 * smelt for iron, once FOOD_COOK_N are held (a cooked pork chop or steak is 8 food against 3 raw) */
#define FOOD_COOK_N 4
#define FOOD_PEARL_COOK_START 8 /* start a pearl-night stock at this many raw meats; finish smaller leftovers */
static int64_t raw_meat(const struct neth_rl_state *s) { return inv_count(s, 319) + inv_count(s, 363) + inv_count(s, 365); }
static int cook_want(const struct neth_vec *v, const struct neth_rl_state *s)
{
    return MOPT(MO_FOOD_COOK) != FK_OFF && raw_meat(s) >= FOOD_COOK_N;
}

/* lane/supply (carry_gate): furnace_kit=carry's portal gate, 1 while it holds the portal back: no furnace held and,
 * with carry_gate=release, one to be had: a placed furnace in reach (taken back), or 8 cobblestone held with a table
 * held or in reach, 4 planks or a log (the table made) */
static int carry_furnace_gate(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                              const struct neth_rl_state *s)
{
    if (MOPT(MO_FURNACE_KIT) != FU_CARRY || inv_count(s, FURNACE) > 0) return 0;
    if (!MOPT(MO_CARRY_GATE)) return 1;
    if (near_furnace(e, r)) return 1;
    int64_t logs = inv_count(s, LOG) + inv_count(s, LOG2), planks = inv_count(s, PLANKS);
    int table = inv_count(s, TABLE) > 0 || near_table(e, r) || planks >= 4 || logs > 0;
    return table && inv_count(s, COBBLE) >= 8;
}

/* lane/chain2 (furnace_kit carry): the furnace placed in reach taken back: a pickaxe held, none in the pack, and
 * nothing to smelt with it now (no iron ore or meat to cook, or no fuel) */
static int furnace_take_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                             const struct neth_rl_state *s)
{
    if (MOPT(MO_FURNACE_KIT) != FU_CARRY || inv_count(s, FURNACE) > 0 || !near_furnace(e, r)) return 0;
    if (e->steps < e->take_retry || r->screen != PS_NONE || best_tool(&v->T, s, FURNACE) < 0) return 0;
    /* Once the mined obsidian and flint are ready, the Nether kit needs the furnace in the pack even if a little ore
     * could still be smelted here. The ordinary carry rule left every normal seven-rod state without one. */
    if (r->dim == 0 && !(e->dims & 1) && inv_count(s, OBSIDIAN) >= 10 && inv_count(s, FLINT_STEEL) > 0) return 1;
    int fuel = inv_count(s, COAL) > 0 || inv_count(s, PLANKS) >= 2;
    /* After the first pearl stock batch, carry the furnace away even with a smaller cookable remainder. The pearl
     * macro otherwise leaves the placed furnace behind while five raw meats still keep cook_want true. */
    int64_t raw = raw_meat(s);
    if (MOPT(MO_FOOD_COOK) == FK_PEARL && raw >= FOOD_COOK_N && raw < FOOD_PEARL_COOK_START &&
        inv_count(s, 320) + inv_count(s, 364) + inv_count(s, 366) > 0)
        return 1;
    return !((inv_count(s, IRON_ORE) > 0 || cook_want(v, s)) && fuel);
}

/* NETH_VEC_IRON's plans after the stone pickaxe's (lane/ironmacro): -1 none */
static int iron_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_state *s, int64_t planks, int64_t sticks, int64_t tables, int64_t cobble,
                     int64_t tier)
{
    int64_t ore = inv_count(s, IRON_ORE), ingots = inv_count(s, INGOT), furnaces = inv_count(s, FURNACE);
    int64_t coal = inv_count(s, COAL), flint = inv_count(s, FLINT);
    int fuel = coal > 0 || planks >= 2, furnace_ok = furnaces > 0 || near_furnace(e, r);
    int carry_ready = MOPT(MO_FURNACE_KIT) == FU_CARRY && r->dim == 0 && !(e->dims & 1) &&
                      inv_count(s, OBSIDIAN) >= 10 && inv_count(s, FLINT_STEEL) > 0;
    if (!carry_ready && (ore > 0 || cook_want(v, s)) && furnace_ok && fuel) return C_SMELT;
    int want = -1;
    if (ore > 0 && !furnace_ok && cobble >= 8) want = C_FURNACE;
    /* (lane/chain2 food_cook make: a furnace made for the meat, the fuel held) */
    else if ((MOPT(MO_FOOD_COOK) == FK_MAKE || MOPT(MO_FOOD_COOK) == FK_PEARL) &&
             cook_want(v, s) && !furnace_ok && fuel && cobble >= 8) want = C_FURNACE;
    /* (lane/chain2 furnace_kit: a furnace made for any smelt wanted, the fuel held) */
    else if (MOPT(MO_FURNACE_KIT) && cook_want(v, s) && !furnace_ok && fuel && cobble >= 8) want = C_FURNACE;
    else if (tier < 3 && ingots >= 3 && sticks >= 2) want = C_IPICK;
    else if (tier >= 3 && ingots >= 3 && inv_count(s, BUCKET) == 0) want = C_BUCKET;
    else if (tier >= 3 && ingots >= 3 && (v->macros & NETH_VEC_BPORTAL) &&     /* lane/obsidian3: the second bucket */
             inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) + inv_count(s, LAVA_BUCKET) < 2)
        want = C_BUCKET;
    else if (ingots >= 1 && flint >= 1 && inv_count(s, FLINT_STEEL) == 0) want = C_FLINT;
    if (want < 0) return -1;
    if (want == C_FLINT) return want;      /* lane/flint: shapeless, in the inventory's grid (no table) */
    if (tables > 0 || near_table(e, r)) return want;
    if ((v->macros & NETH_VEC_TABLE2) && tables == 0 && planks >= 4 && e->has_table) return C_TABLE;
    return -1;
}

/* macros._craft_plan: the plan's kind, -1 none */
static int st_spick_want(const struct neth_vec *v, const struct neth_rl_state *s, int tier);   /* strip2.c */
static int sh_spick_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s);   /* stronghold.c (lane/stronghold3) */
static int sh_spick_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s);
static int prov_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_state *s);   /* food.c (lane/chain food_oracle: the portal waits for food) */

/* lane/bportal (food_prov_cook): the smelt's meat before its iron ore while the provisions are wanted */
static int smelt_meat_first(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    return MOPT(MO_FOOD_PROV_COOK) && prov_want(v, e, &e->last, s);
}

/* the portal macro's frame wanted (its switch, the screen and the retry aside): the overworld's from OB_KEEP_N obsidian
 * (lane/chain obsidian_carry: 20, so the 10 it does not spend are carried), or the Nether's way home
 * (nether_portal_home, common.c) */
static int portal_plan_ok(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s)
{
#ifdef CR_PLAN_TRACE
    if (r->dim == -1 && inv_count(s, BLAZE_ROD) >= 7)
        fprintf(stderr, "PPGATE world=%lld step=%lld chamber=%d yield=%d exit_done=%d sp=%d sp_y=%d player_y=%.1f site=%d retry=%lld kind=%d\n",
                (long long)r->wtime, (long long)e->steps, e->chamber_on, nt_chamber_yield(v, e, r),
                e->exit_cover_done, e->nt.has_sp, e->nt.sy, r->y, pt_site(v, e, r),
                (long long)e->pp_retry, e->kind);
#endif
    /* A second portal must wait for the Nether macro's safe chamber handoff after the blaze haul.
     * The craft plan otherwise digs an exposed room beside the spawner while blazes attack. */
    if (MOPT(MO_NT_EXIT_BACK) && r->dim == -1 && MOPT(MO_NT_CHAMBER) && inv_count(s, BLAZE_ROD) >= 7 &&
        !nt_chamber_yield(v, e, r)) return 0;
    if (nt_portal_home(v, e, r, s)) return 1;
    if (carry_furnace_gate(v, e, r, s)) return 0;
    /* (nether_portal_home on: none in the overworld once home, the carried obsidian not spent on a way back in) */
    if (MOPT(MO_NT_PORTAL_HOME) && (e->dims & 1)) return 0;
    return r->dim == 0 && inv_count(s, OBSIDIAN) >= OB_KEEP_N &&
           inv_count(s, FLINT_STEEL) > 0 && inv_count(s, COBBLE) >= 7 && !prov_want(v, e, r, s);
}
#ifdef ST3_KEEP6
/* lane/strip3's measured alternative to the wood errand (a VARIANT build only: XFLAGS=-DST3_KEEP6): no trip, the wood
 * kept instead (strip2.c st3_keep6) */
static int st3_keep6(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, int p);
#define craft_plan craft_plan0
#endif

static int craft_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s)
{
    int pm = provision_mode(v, e, r, s);
    if (pm) return pm == PV_STOCK ? provision_food_plan(v, e, r, s) : -1;
    /* lane/stronghold3: the stronghold's way short of pickaxe uses: a stone pickaxe first (a table made for it if none
     * is held or near, and taken back after) */
    if (v->macros & NETH_VEC_STRONGHOLD)
    {
        int sp = sh_spick_plan(v, e, r, s);
        if (sp >= 0) return sp;
    }
    if (v->macros & NETH_VEC_LIQUID)
    {
        int lp = liquid_plan(s);
        if (lp >= 0) return lp;
    }
    if ((v->macros & NETH_VEC_END) && s->mo_hit && s->mo_block == END_FRAME && !(s->mo_meta & 4) &&
        inv_count(s, EYE) > 0 && r->screen == PS_NONE)
        return C_FRAME;
    int64_t logs = 0, planks = 0, sticks = 0, tables = 0, cobble = 0, tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, c = s->inv[i].count;
        if (it == LOG || it == LOG2) logs += c;
        if (it == PLANKS) planks += c;
        if (it == STICK) sticks += c;
        if (it == TABLE) tables += c;
        if (it == COBBLE) cobble += c;
        int t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    int reserve = MOPT(MO_FOOD_WOOD_RESERVE) && e->arm.points < 15;
    int64_t reserve_wood = planks + 4 * logs;
    /* The early strip's extra wood is a post-Nether armor station. Carry the table through the portal. */
    if (reserve && r->dim == 0 && !(e->dims & 1) && inv_count(s, OBSIDIAN) >= 10 &&
        inv_count(s, FLINT_STEEL) > 0 && tables == 0)
    {
        if (near_table(e, r) && e->steps >= e->take_retry) return C_TAKE;
        if (planks >= 4 && reserve_wood >= 20) return C_TABLE;
    }
    if ((v->macros & NETH_VEC_PORTAL) && r->screen == PS_NONE && e->steps >= e->pp_retry && portal_plan_ok(v, e, r, s))
        return C_PORTAL;
    if (MOPT(MO_FURNACE_KIT) == FU_CARRY && r->dim == 0 && !(e->dims & 1) &&
        inv_count(s, OBSIDIAN) >= 10 && inv_count(s, FLINT_STEEL) > 0 && inv_count(s, FURNACE) == 0 &&
        tables == 0 && !near_table(e, r) && planks >= 4)
        return C_TABLE;
    /* Make a new station if the old one was left behind. It must be held before the overworld portal starts. */
    if (MOPT(MO_FURNACE_KIT) == FU_CARRY && r->dim == 0 && !(e->dims & 1) &&
        inv_count(s, OBSIDIAN) >= 10 && inv_count(s, FLINT_STEEL) > 0 && inv_count(s, FURNACE) == 0 &&
        !near_furnace(e, r) && cobble >= 8 && (tables > 0 || near_table(e, r)))
        return C_FURNACE;
    if (logs > 0 && planks < (reserve ? 16 : 12)) return C_PLANKS;
    /* lane/chain3: the one-reserve option can make its missing station from held wood. At a far snow start the
     * player can hold cobblestone and sticks but no table; the old reserve plan only ran beside one. */
    if (MOPT(MO_SWORD_STOCK) == 2 && r->dim == 0 && inv_count(s, EYE) >= 12 &&
        sword_stock_fresh(s) < 1 && tables == 0 && !near_table(e, r) && planks >= 4 && sticks >= 1 &&
        (inv_count(s, INGOT) >= 2 || cobble >= 2))
        return C_TABLE;
    /* Planks fuel a furnace without a crafting table. In the opt-in wood-food path, cook before the generic table
     * plan spends four of the twelve planks gathered from three logs. */
    if (MOPT(MO_FOOD_FUEL) == FF_WOOD && cook_want(v, s) && inv_count(s, IRON_ORE) == 0 &&
        inv_count(s, COAL) == 0 && planks >= 2 && (!reserve || reserve_wood >= 18) &&
        (inv_count(s, FURNACE) > 0 || near_furnace(e, r)))
        return C_SMELT;
    if (tables == 0 && !e->has_table && planks >= 4 && (!reserve || reserve_wood >= 20)) return C_TABLE;
    if (sticks < 2 && planks >= 2 && (!reserve || reserve_wood >= 18)) return C_STICK;
    if (tier < 1 && planks >= 3 && sticks >= 2 && (tables > 0 || near_table(e, r))) return C_WPICK;
    if ((v->macros & NETH_VEC_TABLE2) && tier < 2 && cobble >= 3 && sticks >= 2 && tables == 0 && planks >= 4 &&
        e->has_table && !near_table(e, r))
        return C_TABLE;
    if (tier < 2 && cobble >= 3 && sticks >= 2 && (tables > 0 || near_table(e, r))) return C_SPICK;
    /* lane/rod (armor_kit): the iron armor's pieces before the iron plans (arm-t12: one stray ore held after the smelt,
     * the furnace left behind, iron_plan's furnace plan took every start from 7,500 steps on with 19 ingots held) */
    /* lane/chain: sword_kit, a sword (iron with 2 ingots, else stone) planned first while none is held in the
     * overworld (the dragon macro wants one in the hotbar; e43y/e43z: 45 of 51 End and stronghold states without one, the
     * chain's sword spent in the Nether) */
    if (MOPT(MO_SWORD_KIT) && r->dim == 0 && sticks >= 1 && (tables > 0 || near_table(e, r)) &&
        inv_count(s, 267) + inv_count(s, 276) + inv_count(s, 268) + inv_count(s, 283) == 0 &&
        (MOPT(MO_SWORD_KIT) == 2 || inv_count(s, 272) == 0))
    {
        if (inv_count(s, INGOT) >= 2) return C_ISWORD;
        if (inv_count(s, 272) == 0 && cobble >= 2) return C_SSWORD;
    }
    /* lane/armed (spick_keep=first|make): strip2's stone pickaxe beside an iron one ahead of the armour's, the iron's
     * and the late plans (behind them it waited on a smelt, a bucket or a flint and steel the ingots did not make while
     * the stone wore the iron pickaxe out) */
    if (MOPT(MO_SPICK_KEEP) && st_spick_want(v, s, (int)tier) && cobble >= 3 && sticks >= 2 &&
        (tables > 0 || near_table(e, r)))
        return C_SPICK;
    int p = arm_plan(v, e, r, s, tables);
    int sword_need = sword_stock_need(v);
    if (p < 0 && sword_need && r->dim == 0 && inv_count(s, EYE) >= 12 &&
        sword_stock_fresh(s) < sword_need && sticks >= 1 && (tables > 0 || near_table(e, r)))
    {
        if (inv_count(s, INGOT) >= 2) p = C_ISWORD;
        else if (cobble >= 2) p = C_SSWORD;
    }
    if (p < 0) p = sn_plan(v, e, r, s, planks, sticks, tables);     /* lane/rod (snow_kit): the wooden shovel */
    if (p < 0) p = (v->macros & NETH_VEC_IRON) ? iron_plan(v, e, r, s, planks, sticks, tables, cobble, tier) : -1;
    if (p < 0 && (v->macros & NETH_VEC_LATE)) p = late_plan(e, r, s, sticks, tables, tier);
    /* lane/nether: the rods kept whole in the Nether (NETH_VEC_NETHER counts them home), and the sword plans */
    if (p == C_POWDER && (v->macros & NETH_VEC_NETHER) && r->dim == -1) p = -1;
    if (p < 0) p = nt_sword_plan(v, e, r, s, sticks, tables, cobble, tier);
    /* lane/strip2: a stone pickaxe kept beside an iron one (the strip's stone spends it, not the iron) */
    if (p < 0 && st_spick_want(v, s, (int)tier) && cobble >= 3 && sticks >= 2 && (tables > 0 || near_table(e, r)))
        p = C_SPICK;            /* never a table made for it: its 4 planks are the diamond pickaxe's table */
    /* lane/armed (spick_keep=make): made for it after all, the carry macro taking it back (an earned start knows no
     * table, and the stone wore the iron pickaxe out) */
    if (p < 0 && MOPT(MO_SPICK_KEEP) == 2 && st_spick_want(v, s, (int)tier) && cobble >= 3 && sticks >= 2 &&
        tables == 0 && !near_table(e, r) && planks >= 4)
        p = C_TABLE;
    /* lane/strip2: and for the diamond pickaxe (late_plan waits for a table held or near: three diamonds with the
     * table left behind ended the strip's want with no pickaxe made) */
    if (p < 0 && (v->macros & NETH_VEC_STRIP2) && (v->macros & NETH_VEC_LATE) && tier < 4 &&
        inv_count(s, DIAMOND) >= 3 && tables == 0 && !near_table(e, r) && planks >= 4 && (sticks >= 2 || planks >= 6))
        p = C_TABLE;
    if (p < 0 && (v->macros & NETH_VEC_CARRY) && tier >= 1 && tables == 0 && near_table(e, r) &&
        e->steps >= e->take_retry && r->screen == PS_NONE)
        p = C_TAKE;
    if (p < 0 && furnace_take_want(v, e, r, s)) p = C_TAKE;    /* lane/chain2 (furnace_kit carry) */
    return p;
}
#ifdef ST3_KEEP6
#undef craft_plan
static int craft_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s)
{
    return st3_keep6(v, e, s, craft_plan0(v, e, r, s));
}
#endif

/* lane/picks: pick k is plan k - 1 */
static const char *const PICK_NAMES[NETH_VEC_PICKS] = {
    "none", "planks", "table", "stick", "wooden_pickaxe", "stone_pickaxe", "furnace", "smelt", "iron_pickaxe",
    "bucket", "flint_and_steel", "diamond_pickaxe", "blaze_powder", "ender_eye", "fill", "cast", "portal", "frame",
    "throw", "take"};

const char *neth_vec_pick_name(int k)
{
    return k >= 0 && k < NETH_VEC_PICKS ? PICK_NAMES[k] : NULL;
}

/* lane/picks: the craft macro could make plan now: craft_plan's conditions (its parts held, its table or furnace at
 * hand, its macro switch on) without its order or its wants (a second table, planks past 12, a pickaxe below the one
 * held are all allowed: what to make is the policy's). C_THROW stays the throw key's, never a pick. */
static int plan_ok(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                   const struct neth_rl_state *s, int plan)
{
    int64_t logs = 0, planks = 0, sticks = 0, tables = 0, cobble = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, c = s->inv[i].count;
        if (it == LOG || it == LOG2) logs += c;
        if (it == PLANKS) planks += c;
        if (it == STICK) sticks += c;
        if (it == TABLE) tables += c;
        if (it == COBBLE) cobble += c;
    }
    int table = tables > 0 || near_table(e, r), iron = (v->macros & NETH_VEC_IRON) != 0;
    int late = (v->macros & NETH_VEC_LATE) != 0, idle = r->screen == PS_NONE;
    switch (plan)
    {
    case C_PLANKS: return logs > 0;
    case C_TABLE: return planks >= 4;
    case C_STICK: return planks >= 2;
    case C_WPICK: return planks >= 3 && sticks >= 2 && table;
    case C_SPICK: return cobble >= 3 && sticks >= 2 && table;
    case C_FURNACE: return iron && cobble >= 8 && table;
    case C_SMELT:
        return iron && (inv_count(s, IRON_ORE) > 0 || cook_want(v, s)) && (inv_count(s, FURNACE) > 0 || near_furnace(e, r)) &&
               (inv_count(s, COAL) > 0 || planks >= 2);
    case C_IPICK: return iron && inv_count(s, INGOT) >= 3 && sticks >= 2 && table;
    case C_BUCKET: return iron && inv_count(s, INGOT) >= 3 && table;
    case C_FLINT: return iron && inv_count(s, INGOT) >= 1 && inv_count(s, FLINT) >= 1;
    case C_DPICK: return late && inv_count(s, DIAMOND) >= 3 && sticks >= 2 && table;
    case C_POWDER: return late && inv_count(s, BLAZE_ROD) > 0;
    case C_EYE: return late && inv_count(s, PEARL) > 0 && inv_count(s, BLAZE_POWDER) > 0;
    case C_FILL:
    case C_CAST: return (v->macros & NETH_VEC_LIQUID) && liquid_plan(s) == plan;
    case C_PORTAL:
        return (v->macros & NETH_VEC_PORTAL) && idle && e->steps >= e->pp_retry && portal_plan_ok(v, e, r, s);
    case C_FRAME:
        return (v->macros & NETH_VEC_END) && s->mo_hit && s->mo_block == END_FRAME && !(s->mo_meta & 4) &&
               inv_count(s, EYE) > 0 && idle;
    case C_TAKE:
        return (((v->macros & NETH_VEC_CARRY) || MOPT(MO_FOOD_WOOD_RESERVE)) && tables == 0 && near_table(e, r) &&
                e->steps >= e->take_retry && idle) ||
               furnace_take_want(v, e, r, s);
    default: return 0;
    }
}

/* lane/picks: the next action's allowed picks into m[NETH_VEC_PICKS]: none always, a plan when picks are on, no macro
 * runs (the policy's pick would be ignored) and plan_ok */
static void pick_mask(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s, uint8_t *m)
{
    memset(m, 0, NETH_VEC_PICKS);
    m[0] = 1;
    if (!v->picks || e->kind != M_NONE) return;
    for (int k = 1; k < NETH_VEC_PICKS; ++k) m[k] = (uint8_t)plan_ok(v, e, r, s, k - 1);
}

/* the plans' parts: item sets (up to 2 ids) and grid slots */
struct part { int16_t ids[2]; int nslot; int8_t slots[8]; };
static int plan_parts(int plan, struct part *pt)
{
    switch (plan)
    {
    case C_PLANKS: pt[0] = (struct part){{LOG, LOG2}, 1, {1}}; return 1;
    case C_TABLE: pt[0] = (struct part){{PLANKS, -1}, 4, {1, 2, 3, 4}}; return 1;
    case C_STICK: pt[0] = (struct part){{PLANKS, -1}, 2, {1, 3}}; return 1;
    case C_WPICK:
        pt[0] = (struct part){{PLANKS, -1}, 3, {1, 2, 3}};
        pt[1] = (struct part){{STICK, -1}, 2, {5, 8}};
        return 2;
    case C_FURNACE: pt[0] = (struct part){{COBBLE, -1}, 8, {1, 2, 3, 4, 6, 7, 8, 9}}; return 1;
    case C_IPICK:
        pt[0] = (struct part){{INGOT, -1}, 3, {1, 2, 3}};
        pt[1] = (struct part){{STICK, -1}, 2, {5, 8}};
        return 2;
    case C_BUCKET: pt[0] = (struct part){{INGOT, -1}, 3, {1, 3, 5}}; return 1;
    case C_FLINT:
        pt[0] = (struct part){{INGOT, -1}, 1, {1}};
        pt[1] = (struct part){{FLINT, -1}, 1, {2}};
        return 2;
    case C_DPICK:
        pt[0] = (struct part){{DIAMOND, -1}, 3, {1, 2, 3}};
        pt[1] = (struct part){{STICK, -1}, 2, {5, 8}};
        return 2;
    case C_POWDER: pt[0] = (struct part){{BLAZE_ROD, -1}, 1, {1}}; return 1;
    case C_ISWORD:      /* lane/nether: the blade in the middle column, the stick under it */
    case C_SSWORD:
        pt[0] = (struct part){{plan == C_ISWORD ? INGOT : COBBLE, -1}, 2, {2, 5}};
        pt[1] = (struct part){{STICK, -1}, 1, {8}};
        return 2;
    case C_EYE:
        pt[0] = (struct part){{PEARL, -1}, 1, {1}};
        pt[1] = (struct part){{BLAZE_POWDER, -1}, 1, {2}};
        return 2;
    /* lane/rod (armor_kit): the iron armor, the table's grid 1..9 row by row */
    case C_IBOOTS: pt[0] = (struct part){{INGOT, -1}, 4, {4, 6, 7, 9}}; return 1;
    case C_ILEGS: pt[0] = (struct part){{INGOT, -1}, 7, {1, 2, 3, 4, 6, 7, 9}}; return 1;
    case C_ICHEST: pt[0] = (struct part){{INGOT, -1}, 8, {1, 3, 4, 5, 6, 7, 8, 9}}; return 1;
    case C_IHELM: pt[0] = (struct part){{INGOT, -1}, 5, {1, 2, 3, 4, 6}}; return 1;
    case C_WSHOVEL:     /* lane/rod (snow_kit): the plank on top of the middle column, two sticks under it */
        pt[0] = (struct part){{PLANKS, -1}, 1, {2}};
        pt[1] = (struct part){{STICK, -1}, 2, {5, 8}};
        return 2;
    default:
        pt[0] = (struct part){{COBBLE, -1}, 3, {1, 2, 3}};
        pt[1] = (struct part){{STICK, -1}, 2, {5, 8}};
        return 2;
    }
}

static int plan_bench(int plan)
{
    return plan == C_WPICK || plan == C_SPICK || plan == C_ISWORD || plan == C_SSWORD ||
           (plan >= C_IBOOTS && plan <= C_WSHOVEL) ||   /* lane/rod (armor_kit, snow_kit) */
           (plan >= C_FURNACE && plan != C_SMELT && plan != C_POWDER && plan != C_EYE && plan != C_FLINT && plan < C_FILL);
}

/* lane/ladder12: a crafting plan (the 2x2 and 3x3 recipes and the smelt: the plans with a window; not the bucket,
 * portal, frame, throw or take), the ones NETH_VEC_NOTABLE and NETH_VEC_NORECIPE split; and the screen its clicks
 * fit (a 2x2 plan the inventory, a 3x3 one the table, the smelt the furnace). The craft labeller's (shadow.c) */
static int plan_gui(int plan)
{
    return plan >= 0 && (plan <= C_EYE || plan == C_ISWORD || plan == C_SSWORD || (plan >= C_IBOOTS && plan <= C_WSHOVEL));
}
static int plan_fits(int plan, int screen)
{
    return plan_gui(plan) &&
           (plan == C_SMELT ? screen == PS_FURNACE : plan_bench(plan) ? screen == PS_CRAFTING : screen == PS_INVENTORY);
}
/* lane/ladder17: a 2x2 recipe (a crafting plan the inventory's grid makes: actions.recipe_2x2's) */
static int plan_2x2(int plan) { return plan_fits(plan, PS_INVENTORY); }

/* macros.inv_slot */
static int inv_slot(int i, int bench)
{
    if (bench) return i < 9 ? 37 + i : i + 1;
    return i < 9 ? 36 + i : i;
}

/* macros._recipe_ops into e->ops: 0, or -1 (None: the inventory lacks a part) */
static int recipe_ops(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s, int plan)
{
    struct part pt[2];
    int np = plan_parts(plan, pt), bench = plan_bench(plan), n = 0;
    for (int q = 0; q < np; ++q)
    {
        int src = -1;
        for (int i = 0; i < 36 && src < 0; ++i)
        {
            int it = s->inv[i].item;
            if ((it == pt[q].ids[0] || it == pt[q].ids[1]) && s->inv[i].count >= pt[q].nslot) src = i;
        }
        if (src < 0) return -1;
        int cs = inv_slot(src, bench);
        e->ops[n++] = click(cs, 0, 0);
        if (pt[q].nslot == 1 && !bench)
        {
            e->ops[n++] = click(pt[q].slots[0], 0, 0);
            continue;
        }
        for (int j = 0; j < pt[q].nslot; ++j) e->ops[n++] = click(pt[q].slots[j], 1, 0);
        e->ops[n++] = click(cs, 0, 0);
    }
    e->ops[n++] = click(0, 0, 1);
    /* lane/pearlgo (craft_return on): a whole stack put in one grid slot shift-clicked back after the output (vanilla's
     * close drops the grid's stacks: ContainerPlayer and ContainerWorkbench.onContainerClosed) */
    if (MOPT(MO_CRAFT_RETURN))
        for (int q = 0; q < np; ++q)
            if (pt[q].nslot == 1 && !bench) e->ops[n++] = click(pt[q].slots[0], 0, 1);
    e->nops = n;
    e->op_at = 0;
    return 0;
}

/* NETH_VEC_IRON: the crosshair on a block whose use opens a screen (a
 * table, a chest, a furnace): a place there would open it instead */
static int blocks_use(const struct neth_rl_state *s)
{
    int b = s->mo_block;
    return s->mo_hit && (b == TABLE || b == CHEST || b == FURNACE || b == FURNACE_LIT || b == 146 || b == 23);
}

/* NETH_VEC_IRON: a block's item for placing when it is not in the hotbar
 * (lane/ironfix): placed already (still looking where it went until the
 * client has the block: a packet's tick or two), else swapped from the main
 * inventory into the hotbar slot the macros need least (no tool, no craft
 * material) with a number key's click (mode 2) under the inventory screen.
 * The macro's return (0 going on, 1 over) */
static int fetch_item(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, int item)
{
    if (e->placed)
    {
        a->look_mode = 1;
        a->look[0] = r->yaw;
        a->look[1] = 60.0f;
        return e->age > 50;
    }
    int src = -1;
    for (int i = 9; i < 36 && src < 0; ++i)
        if (s->inv[i].item == item) src = i;
    if (src < 0) return 1;
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
                score = tool ? 100 : v->T.craft[itc] || v->T.craft_iron[itc] || it == COAL || it == TABLE ? 10 : 1;
            }
            if (score < best) best = score, to = i;
        }
        a->nops = 2;
        a->ops[0] = click(inv_slot(src, 0), to, 2);
        a->ops[1] = CLOSE;
        return 0;
    }
    if (r->screen == PS_NONE && !e->pressed)
    {
        a->press[PK_INVENTORY] = 1;
        e->pressed = 1;
    }
    return e->age > 50;
}

static int best_tool(const struct vec_tabs *T, const struct neth_rl_state *s, int block);

/* NETH_VEC_IRON: ITEM (in hotbar slot hb) placed on the floor beside the
 * player (lane/placefix): each side in turn looked at 60 degrees down, used
 * only when the crosshair is on the floor block of the cell beside the
 * player's (the cell above it free, as the ray passed through it) and not on
 * a block whose use opens a screen; with no such side (a 1x1 shaft, walls
 * all round) the block at the feet on the current side is dug (the best tool,
 * attack held) and the side tried again. The macro's return */
static int place_item(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s, struct neth_rl_act *a, double ey, int hb)
{
    int px = (int)floor(r->x), py = (int)floor(r->y), pz = (int)floor(r->z);
    if (e->pdir < 0) e->pdir = ((int)lround((double)r->yaw / 90.0) % 4 + 4) % 4;
    static const int dx[4] = {0, -1, 0, 1}, dz[4] = {1, 0, -1, 0};    /* yaw 0, 90, 180, 270: south, west, north, east */
    a->hotbar = hb;
    a->look_mode = 1;
    if (e->pdig)
    {
        int here = s->mo_hit && s->mo_x == e->pdx && s->mo_y == e->pdy && s->mo_z == e->pdz;
        /* lane/obsid (place_dig skip): a table, furnace or chest in the cell is not dug (a table by hand outlasts the
         * 60 steps: the smelt plan dug the run's own table a thousand times a run); the next side's cell, four at most */
        if (MOPT(MO_PLACE_DIG) && here && blocks_use(s) && e->ptries < 8)
        {
            e->ptries += 1;
            e->pdir = (e->pdir + 1) % 4;
            e->pdx = px + dx[e->pdir];
            e->pdz = pz + dz[e->pdir];
            e->pdig = 1;
            return e->age > 70;
        }
        if ((e->pdig > 3 && !here) || e->pdig > 60)
        {
            e->pdig = 0;
            e->ptries = 0;
            e->aimed = 0;
            return e->age > 70;
        }
        double yaw, pitch;
        look_block(r, ey, e->pdx, e->pdy, e->pdz, &yaw, &pitch);
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        a->hold = 1u << PK_ATTACK;
        int best = here ? best_tool(&v->T, s, s->mo_block) : -1;
        a->hotbar = best >= 0 ? best : -1;
        e->pdig += 1;
        return e->age > 70;
    }
    a->look[0] = (float)(e->pdir * 90);
    a->look[1] = 60.0f;
    if (e->aimed < 2)
    {
        e->aimed += 1;
        return e->age > 70;
    }
    int ok = s->mo_hit && s->mo_y == py - 1 && abs(s->mo_x - px) <= 1 && abs(s->mo_z - pz) <= 1 &&
             !(s->mo_x == px && s->mo_z == pz) && !blocks_use(s);
    if (ok && s->current == hb && !e->placed)
    {
        a->press[PK_USE] = 1;
        e->placed = 1;
        return 0;
    }
    if (!ok)
    {
        e->aimed = 0;
        if (++e->ptries < 4)
            e->pdir = (e->pdir + 1) % 4;
        else
        {
            e->pdig = 1;
            e->pdx = px + dx[e->pdir];
            e->pdy = py;
            e->pdz = pz + dz[e->pdir];
        }
    }
    return e->age > 70;
}

/* macros._open_bench */
static int open_bench(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    if (r->screen == PS_CRAFTING)
    {
        e->cphase = CP_CLICKS;
        return 0;
    }
    if (s->mo_hit && s->mo_block == TABLE)
    {
        if (!e->used)
        {
            a->press[PK_USE] = 1;
            e->used = 1;
            e->has_table = 1;
            e->tx = (double)s->mo_x + 0.5;
            e->ty = (double)s->mo_y + 0.5;
            e->tz = (double)s->mo_z + 0.5;
        }
        else if (e->age > 50)
            return 1;
        return 0;
    }
    if (e->has_table && near_table(e, r))
    {
        double yaw, pitch;
        if (v->macros & NETH_VEC_IRON)
            look_try(r, ey, (int)floor(e->tx), (int)floor(e->ty), (int)floor(e->tz), e->aimed++ / 2, &yaw, &pitch);
        else
            look_at(r->x, ey, r->z, e->tx, e->ty, e->tz, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        e->used = 0;
        /* lane/craftfix: the table remembered there and never under the crosshair (broken and picked up, or out of
         * sight): forgotten, so the next try places the one held or makes one (it was looked for again and again,
         * 50 steps every 55, for the rest of the run: 22 of 256 runs of an hour, mostly past their first ingot) */
        if (e->age > 50 && (v->macros & NETH_VEC_IRON)) e->has_table = 0;
        return e->age > 50;
    }
    int hb = -1;
    for (int i = 0; i < 9 && hb < 0; ++i)
        if (s->inv[i].item == TABLE) hb = i;
    if (hb < 0 && (v->macros & NETH_VEC_IRON)) return fetch_item(v, e, r, s, a, TABLE);
    if (hb < 0) return 1;
    if (v->macros & NETH_VEC_IRON) return place_item(v, e, r, s, a, ey, hb);
    a->hotbar = hb;
    a->look_mode = 1;
    a->look[0] = r->yaw;
    a->look[1] = 60.0f;
    if (s->current == hb && e->aimed >= 2 && !e->placed)
    {
        a->press[PK_USE] = 1;
        e->placed = 1;
    }
    e->aimed += 1;
    return e->age > 50;
}

/* NETH_VEC_IRON: the furnace container's slot of inventory slot i (0 the
 * input, 1 the fuel, 2 the output, then the 27 main slots and the hotbar) */
static int furnace_slot(int i) { return i < 9 ? 30 + i : i - 6; }

/* open_bench's way for a furnace: the one under the crosshair, the placed one
 * looked at, else one from the hotbar placed at the feet */
static int open_furnace(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    if (r->screen == PS_FURNACE)
    {
        e->cphase = CP_CLICKS;
        return 0;
    }
    if (s->mo_hit && (s->mo_block == FURNACE || s->mo_block == FURNACE_LIT))
    {
        if (!e->used)
        {
            a->press[PK_USE] = 1;
            e->used = 1;
            e->has_furnace = 1;
            e->fx = (double)s->mo_x + 0.5;
            e->fy = (double)s->mo_y + 0.5;
            e->fz = (double)s->mo_z + 0.5;
        }
        else if (e->age > 50)
            return 1;
        return 0;
    }
    if (e->has_furnace && near_furnace(e, r))
    {
        double yaw, pitch;
        look_try(r, ey, (int)floor(e->fx), (int)floor(e->fy), (int)floor(e->fz), e->aimed++ / 2, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        e->used = 0;
        if (e->age > 50) e->has_furnace = 0;    /* lane/craftfix: as open_bench's table */
        return e->age > 50;
    }
    int hb = -1;
    for (int i = 0; i < 9 && hb < 0; ++i)
        if (s->inv[i].item == FURNACE) hb = i;
    if (hb < 0) return fetch_item(v, e, r, s, a, FURNACE);
    return place_item(v, e, r, s, a, ey, hb);
}

/* the smelt plan's clicks into e->ops: the first ore stack shift-clicked to
 * the input, then its fuel placed one at a time (coal: 8 items each, else
 * planks: 1.5 each, at most 6); 0, or -1 without ore or fuel */
static int smelt_ops(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s)
{
    int ore = -1, fuel = -1, n = 0, meat = -1;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (s->inv[i].count <= 0) continue;
        if (it == IRON_ORE && ore < 0) ore = i;
        if ((it == 319 || it == 363 || it == 365) && (meat < 0 || s->inv[i].count > s->inv[meat].count)) meat = i;
        if (it == COAL && (fuel < 0 || s->inv[fuel].item != COAL)) fuel = i;
        if (it == PLANKS && fuel < 0 && s->inv[i].count >= 2) fuel = i;
    }
    if ((provision_mode(v, e, &e->last, s) == PV_STOCK || smelt_meat_first(v, e, s)) && cook_want(v, s)) ore = meat;
    else if (ore < 0 && cook_want(v, s)) ore = meat;     /* lane/food2: the largest raw meat stack */
    if (ore < 0 || fuel < 0) return -1;
    int k = s->inv[ore].count, need = s->inv[fuel].item == COAL ? (k + 7) / 8 : (2 * k + 2) / 3;
    need = need > s->inv[fuel].count ? s->inv[fuel].count : need > 6 ? 6 : need;
    e->ops[n++] = click(furnace_slot(ore), 0, 1);
    e->ops[n++] = click(furnace_slot(fuel), 0, 0);
    for (int j = 0; j < need; ++j) e->ops[n++] = click(1, 1, 0);
    e->ops[n++] = click(furnace_slot(fuel), 0, 0);
    e->nops = n;
    e->op_at = 0;
    return 0;
}

/* NETH_VEC_LIQUID's bucket plans: the bucket selected (fetched from the main
 * inventory when it is not in the hotbar), the look kept on the liquid, one
 * use, a few steps for the server's answer */
static int bucket_use(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s, struct neth_rl_act *a)
{
    int item = e->cplan == C_FILL ? BUCKET : e->cplan == C_FRAME || e->cplan == C_THROW ? EYE : WATER_BUCKET, hb = -1;
    if (e->used) return e->age > e->aimed + 4;
    if (blocks_use(s)) return 1;        /* a use there would open the table, chest or furnace instead */
    if (e->cplan == C_FRAME && !(s->mo_hit && s->mo_block == END_FRAME && !(s->mo_meta & 4))) return 1;
    for (int i = 0; i < 9 && hb < 0; ++i)
        if (s->inv[i].item == item) hb = i;
    if (hb < 0) return fetch_item(v, e, r, s, a, item);
    a->hotbar = hb;
    if (s->current == hb && r->screen == PS_NONE)
    {
        a->press[PK_USE] = 1;
        e->used = 1;
        e->aimed = e->age;
    }
    return e->age > 30;
}

/* macros._craft */
static int take_table(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s, struct neth_rl_act *a, double ey);

static int craft(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                 struct neth_rl_act *a, double ey)
{
    still(a);
    /* lane/ladder12: the half this call acts in (the phase it starts in: open_bench's step that sees the window open
     * is the table's; CP_OPEN's inventory falls through to the clicks, the recipe's) */
    e->chalf = !plan_gui(e->cplan) ? 0 : e->cphase == CP_OPEN ? 1 : 2;
    /* lane/ladder15: the table half's first step with no screen open (half 4: NETH_VEC_TABLEKEY's key) */
    if (e->chalf == 1 && !e->tfirst && r->screen == PS_NONE) e->chalf = 4;
    if (e->chalf == 1 || e->chalf == 4) e->tfirst = 1;
    if (e->age > e->ccap)
    {
#ifdef CR_TRACE
        /* lane/obsid: a VARIANT build (XFLAGS=-DCR_TRACE): each plan that ran out of steps, a diamond pickaxe held */
        if (inv_count(s, DIAMOND_PICK) > 0)
            fprintf(stderr, "CRT %d %ld cap plan %d phase %d screen %d hasf %d near %d f %.1f %.1f %.1f at %.1f %.1f %.1f "
                            "mo %d:%d,%d,%d furn %d ore %d coal %d pdig %d ptries %d placed %d used %d ground %d\n",
                    (int)(e - v->e), (long)e->steps, e->cplan, e->cphase, r->screen, e->has_furnace, near_furnace(e, r),
                    e->fx, e->fy, e->fz, r->x, r->y, r->z, s->mo_hit ? s->mo_block : -1, s->mo_x, s->mo_y, s->mo_z,
                    (int)inv_count(s, FURNACE), (int)inv_count(s, IRON_ORE), (int)inv_count(s, COAL), e->pdig,
                    e->ptries, e->placed, e->used, r->on_ground);
#endif
        if (r->screen != 0)
        {
            a->nops = 1;
            a->ops[0] = CLOSE;
        }
        return 1;
    }
    if (e->cplan == C_FILL || e->cplan == C_CAST || e->cplan == C_FRAME || e->cplan == C_THROW)
        return bucket_use(v, e, r, s, a);
    if (e->cplan == C_PORTAL) return portal_build(v, e, r, s, a, ey);
    if (e->cplan == C_TAKE) return take_table(v, e, r, s, a, ey);
    if (e->cphase == CP_OPEN)
    {
        if (e->cplan == C_SMELT)
            return open_furnace(v, e, r, s, a, ey);
        if (!plan_bench(e->cplan))
        {
            if (r->screen == PS_INVENTORY)
                e->cphase = CP_CLICKS;
            else
            {
                if (!e->pressed)
                {
                    a->press[PK_INVENTORY] = 1;
                    e->pressed = 1;
                }
                return 0;
            }
        }
        else
            return open_bench(v, e, r, s, a, ey);
    }
    /* lane/ladder17 (actions.recipe_2x2 clicks): a 2x2 plan's recipe is never the macro's under NETH_VEC_NORECIPE */
    int x2own = MOPT(MO_RECIPE_2X2) == R2_CLICKS && plan_2x2(e->cplan);
    if ((v->macros & NETH_VEC_NORECIPE) && plan_gui(e->cplan) &&
        (x2own || !((v->macros & NETH_VEC_RECIPEKEY) && e->rjump)))
    {
        /* lane/ladder12: the recipe half off: the window open, the step and the clicks the policy's (decode); with
         * NETH_VEC_RECIPEKEY the policy's jump held here goes on with the clicks */
        e->chand = 1;
        return 1;
    }
    e->chalf = !plan_gui(e->cplan) ? 0 : e->rfirst ? 2 : 3;
    if (e->chalf == 3) e->gb[e->cplan == C_SMELT ? 10 : plan_bench(e->cplan) ? 9 : 8] += 1;   /* lane/ladder13 */
    if (x2own) e->chalf = 9;    /* lane/ladder17: a 2x2 recipe's step (its clicks, the result, the close): half 9 */
    e->rfirst = 1;
    if (e->cphase == CP_CLICKS)
    {
        if (!e->has_ops)
        {
            e->has_ops = 1;
            if (e->cplan == C_SMELT ? smelt_ops(v, e, s) : recipe_ops(v, e, s, e->cplan))
            {
                a->nops = 1;
                a->ops[0] = CLOSE;
                return 1;
            }
        }
        int k = e->nops - e->op_at < 4 ? e->nops - e->op_at : 4;
        a->nops = k;
        for (int j = 0; j < k; ++j) a->ops[j] = e->ops[e->op_at + j];
        e->op_at += k;
        if (e->op_at == e->nops) e->cphase = e->cplan == C_SMELT ? CP_WAIT : CP_CLOSE;
        return 0;
    }
    if (e->cphase == CP_WAIT)
    {
        /* the smelt: the ingots out once the input is empty, or the fire out with ore left */
        if (r->screen != PS_FURNACE) return 1;
        int in = r->slot_item[0] >= 0 && r->slot_count[0] > 0, fuel = r->slot_item[1] >= 0 && r->slot_count[1] > 0;
        int lit = s->mo_hit && s->mo_block == FURNACE_LIT;
        if (in && (fuel || lit || e->age < 20)) return 0;
        if (r->slot_item[2] >= 0 && r->slot_count[2] > 0)
        {
            e->gb[14] += 1;     /* lane/ladder13 */
            a->nops = 1;
            a->ops[0] = click(2, 0, 1);
        }
        e->cphase = CP_CLOSE;
        return 0;
    }
    if (e->cphase == CP_CLOSE)
    {
        a->nops = 1;
        a->ops[0] = CLOSE;
        e->cphase = CP_CLOSED;
        return 0;
    }
    return r->screen == PS_NONE;
}

/* NETH_VEC_CARRY (lane/carry): the hotbar slot to break a table with: an
 * axe, else an empty slot, else one holding no tool (-1: none) */
static int table_slot(const struct neth_vec *v, const struct neth_rl_state *s)
{
    int best = best_tool(&v->T, s, TABLE);
    if (best >= 0) return best;
    for (int i = 0; i < 9; ++i)
        if (s->inv[i].item < 0 || s->inv[i].count <= 0) return i;
    for (int i = 0; i < 9; ++i)
    {
        int it = s->inv[i].item > 4095 ? 4095 : s->inv[i].item;
        if (!(v->T.pick[it] || v->T.axe[it] || v->T.shovel[it] || it == 268 || it == 272 || it == 267 || it == 276 ||
              it == 283))
            return i;
    }
    return -1;
}

/* NETH_VEC_CARRY: the placed table taken (C_TAKE). CP_OPEN: the look moved
 * over the table's faces (look_try) until the crosshair is on it, then held
 * there with attack held (never on another block); the table's cell under the
 * crosshair no longer a table, it is broken: CP_CLICKS, the walk to where it
 * stood (forward, a jump when it stood above the feet) until the item is
 * picked up. The macro's return */
static int take_table(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    /* (lane/chain2 furnace_kit carry: take_f, the furnace's take: its cell, either furnace block, a pickaxe) */
    int fu = e->take_f;
    if (inv_count(s, fu ? FURNACE : TABLE) > e->take_before)
    {
        if (fu) e->has_furnace = 0;
        else e->has_table = 0;
        e->take_retry = e->steps;               /* taken: the backoff is for a failed take, not the next one */
        return 1;
    }
    int bx = (int)floor(fu ? e->fx : e->tx), by = (int)floor(fu ? e->fy : e->ty), bz = (int)floor(fu ? e->fz : e->tz);
    double yaw, pitch;
    a->look_mode = 1;
    if (e->cphase == CP_OPEN)
    {
        int cell = s->mo_hit && s->mo_x == bx && s->mo_y == by && s->mo_z == bz;
        int isb = fu ? s->mo_block == FURNACE || s->mo_block == FURNACE_LIT : s->mo_block == TABLE;
        int block_now = fu ? obsview_block(v->p, (int)(e - v->e), bx, by, bz, NULL) : -1;
        if (cell && isb)
        {
            look_try(r, ey, bx, by, bz, e->aimed / 2, &yaw, &pitch);
            a->hold = 1u << PK_ATTACK;
            int hb = fu ? best_tool(&v->T, s, FURNACE) : table_slot(v, s);
            if (fu && hb < 0) return 1;
            if (hb >= 0 && hb != s->current) a->hotbar = hb;
            e->used = 1;
        }
        else if ((fu && block_now >= 0 && block_now != FURNACE && block_now != FURNACE_LIT) ||
                 (!fu && e->used && (!cell || !isb)))
        {
            /* A remembered furnace whose cell is known air may already be broken. Walk to its drop once, then
             * clear the memory; requiring this take attempt to have attacked it trapped seed 2 in repeated aims. */
            e->cphase = CP_CLICKS;
            if (fu) e->has_furnace = 0;
            else e->has_table = 0;
            return 0;
        }
        else
        {
            if (e->aimed > 24) return 1;
            look_try(r, ey, bx, by, bz, e->aimed++ / 2, &yaw, &pitch);
        }
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        return 0;
    }
    double dx = bx + 0.5 - r->x, dz = bz + 0.5 - r->z;
    look_at(r->x, ey, r->z, bx + 0.5, by + 0.2, bz + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    if (dx * dx + dz * dz > 0.25) a->hold |= 1u << PK_FORWARD;
    if (by > (int)floor(r->y)) a->hold |= 1u << PK_JUMP;
    return 0;
}


/* the craft macro started on plan from phase (macro_start's, CP_OPEN; lane/ladder12's recipe_start, CP_CLICKS or
 * CP_WAIT) */
static void craft_begin(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s, int plan, int phase)
{
    e->kind = M_CRAFT;
    e->cplan = plan;
    e->ccap = plan == C_SMELT ? 80 + 210 * (int)((provision_mode(v, e, &e->last, s) == PV_STOCK || smelt_meat_first(v, e, s)) &&
                                                         cook_want(v, s) ? raw_meat(s) :
                                                inv_count(s, IRON_ORE) > 0 || !cook_want(v, s) ? inv_count(s, IRON_ORE) : raw_meat(s))
              : plan == C_PORTAL ? 1600
              : (v->macros & NETH_VEC_IRON) ? 160 : 80;
    if (plan == C_PORTAL) e->pp_step = -1;
    if (plan == C_TAKE)
    {
        e->ccap = 200;
        /* A placed table can be near when the furnace's take made this plan. Honor that request first. */
        e->take_f = furnace_take_want(v, e, &e->last, s) ||
                    !(((v->macros & NETH_VEC_CARRY) || MOPT(MO_FOOD_WOOD_RESERVE)) &&
                      inv_count(s, TABLE) == 0 && near_table(e, &e->last));
        e->take_before = (int)inv_count(s, e->take_f ? FURNACE : TABLE);
        e->take_retry = e->steps + 400;
    }
    e->cphase = phase;
    e->pressed = e->used = e->aimed = e->placed = 0;
    e->pdir = -1;
    e->ptries = e->pdig = 0;
    e->has_ops = 0;
    e->rfirst = 0;
    e->tfirst = 0;
}

/* lane/ladder12 (NETH_VEC_NOTABLE): under the inventory, crafting or furnace window the policy opened, tidy (the cursor
 * empty, the grid empty: the recipe's clicks assume it), the plan craft_plan names started from its clicks where it fits
 * the window (plan_fits); the table or furnace under the crosshair (the one the policy used) remembered first, as
 * open_bench and open_furnace remember the ones they use, so a 3x3 plan or the smelt is named with it at hand. 1 if
 * started; a window none fits stays the policy's */
static int recipe_start(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s)
{
    int sc = r->screen;
    if (sc != PS_INVENTORY && sc != PS_CRAFTING && sc != PS_FURNACE) return 0;
    if (r->cur_item >= 0 && r->cur_count > 0) return 0;
    int n = r->nslots > 90 ? 90 : r->nslots, grid = sc == PS_INVENTORY ? 4 : sc == PS_CRAFTING ? 9 : 0;
    for (int k = 1; k <= grid && k < n; ++k)
        if (r->slot_item[k] >= 0 && r->slot_count[k] > 0) return 0;
    if (sc == PS_CRAFTING && s->mo_hit && s->mo_block == TABLE)
    {
        e->has_table = 1;
        e->tx = (double)s->mo_x + 0.5;
        e->ty = (double)s->mo_y + 0.5;
        e->tz = (double)s->mo_z + 0.5;
    }
    if (sc == PS_FURNACE && s->mo_hit && (s->mo_block == FURNACE || s->mo_block == FURNACE_LIT))
    {
        e->has_furnace = 1;
        e->fx = (double)s->mo_x + 0.5;
        e->fy = (double)s->mo_y + 0.5;
        e->fz = (double)s->mo_z + 0.5;
    }
    int p = craft_plan(v, e, r, s);
    if (!plan_fits(p, sc)) return 0;
    /* a furnace already smelting: its wait, then the ingots (the craft labeller's start) */
    int busy = p == C_SMELT && r->nslots > 0 && r->slot_item[0] >= 0 && r->slot_count[0] > 0;
    craft_begin(v, e, s, p, busy ? CP_WAIT : CP_CLICKS);
    return 1;
}

/* lane/ladder14 (NETH_VEC_LBL_FITS): recipe_start's test without its start: the inventory, crafting or furnace window
 * tidy and the plan craft_plan names fitting it, the table or furnace under the crosshair taken as at hand for the
 * plan (as recipe_start remembers it) and e left as it was. At any macros: the teacher's env (its screen macro closes
 * the window the policy opened) says where the student's own window would start the recipe */
static int window_fits(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                       const struct neth_rl_state *s)
{
    int sc = r->screen;
    if (sc != PS_INVENTORY && sc != PS_CRAFTING && sc != PS_FURNACE) return 0;
    if (r->cur_item >= 0 && r->cur_count > 0) return 0;
    int n = r->nslots > 90 ? 90 : r->nslots, grid = sc == PS_INVENTORY ? 4 : sc == PS_CRAFTING ? 9 : 0;
    for (int k = 1; k <= grid && k < n; ++k)
        if (r->slot_item[k] >= 0 && r->slot_count[k] > 0) return 0;
    int ht = e->has_table, hf = e->has_furnace;
    double tx = e->tx, ty = e->ty, tz = e->tz, fx = e->fx, fy = e->fy, fz = e->fz;
    if (sc == PS_CRAFTING && s->mo_hit && s->mo_block == TABLE)
        e->has_table = 1, e->tx = s->mo_x + 0.5, e->ty = s->mo_y + 0.5, e->tz = s->mo_z + 0.5;
    if (sc == PS_FURNACE && s->mo_hit && (s->mo_block == FURNACE || s->mo_block == FURNACE_LIT))
        e->has_furnace = 1, e->fx = s->mo_x + 0.5, e->fy = s->mo_y + 0.5, e->fz = s->mo_z + 0.5;
    int p = craft_plan(v, e, r, s);
    /* lane/ladder17 (actions.recipe_2x2 clicks): a 2x2 plan's window is the policy's clicks, not the jump's */
    int fits = plan_fits(p, sc) && !(MOPT(MO_RECIPE_2X2) == R2_CLICKS && plan_2x2(p));
    e->has_table = ht, e->has_furnace = hf;
    e->tx = tx, e->ty = ty, e->tz = tz, e->fx = fx, e->fy = fy, e->fz = fz;
    return fits;
}


/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static void craft_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    const struct neth_vec *v = c->v;
    const int32_t *kq = c->act + 2;
    c->e->rjump = kq[5] > 0;                /* lane/ladder12: NETH_VEC_RECIPEKEY's (read only with the bit) */
    /* lane/ladder15: NETH_VEC_TABLEKEY's (read only with the bit), actions.table_key's key */
    c->e->tkey = kq[MOPT(MO_TABLE_KEY) == TK_PAUSED ? NETH_VEC_KEY_PAUSED : NETH_VEC_KEY_INV] > 0;
}

/* a plan's part held (on the craft macro's cadence), or the policy's pick; NETH_VEC_CARRY's table at hand */
static int craft_m_cand(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    int cand = 0;
    if (v->picks)
        cand |= c->pick > 0;
    else if ((v->macros & NETH_VEC_CRAFT) && e->steps % 5 == 0)
        for (int j = 0; j < 36 && !cand; ++j)
            cand = s->inv[j].item >= 0 && s->inv[j].item < 4096 &&
                   (v->T.craft[s->inv[j].item] ||
                    ((v->macros & (NETH_VEC_IRON | NETH_VEC_LATE | NETH_VEC_LIQUID | NETH_VEC_PORTAL |
                                   NETH_VEC_END | NETH_VEC_NETHER)) &&
                     v->T.craft_iron[s->inv[j].item]));
    if ((v->macros & NETH_VEC_CARRY) && e->steps % 5 == 0 && e->steps >= e->take_retry) cand |= near_table(e, c->r);
    if (e->steps % 5 == 0) cand |= furnace_take_want(v, e, c->r, s);     /* lane/chain2 (furnace_kit carry) */
    return cand;
}

static int craft_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    int picked = c->picked;
    /* lane/ladder15: NETH_VEC_TABLEKEY, the policy's key 12 starts a crafting plan's table half on any step */
    int tk = (v->macros & NETH_VEC_TABLEKEY) && (v->macros & NETH_VEC_NOTABLE) && !v->picks && e->tkey;
    int cad = v->picks ? picked >= 0 : e->steps % 5 == 0;
    if ((v->macros & NETH_VEC_CRAFT) && (cad || tk))
    {
        int plan = v->picks ? picked : craft_plan(v, e, c->r, s);
        /* lane/ladder12: NETH_VEC_NOTABLE, a crafting plan's window the policy's to open (recipe_start below);
         * lane/ladder15: off the cadence only a crafting plan by the key */
        int gui = plan_gui(plan);
        if (plan >= 0 && (cad ? !((v->macros & NETH_VEC_NOTABLE) && gui && !tk) : gui))
        {
            craft_begin(v, e, s, plan, CP_OPEN);
            if (tk && gui) e->gb[plan == C_SMELT ? 36 : plan_bench(plan) ? 35 : 34] += 1;   /* lane/ladder15 */
        }
    }
    return e->kind != M_NONE;
}

static int craft_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_CRAFT) return -1;
#ifdef CR_PLAN_TRACE
    if (c->r->dim == -1 && inv_count(c->s, BLAZE_ROD) >= 7 && c->e->steps % 50 == 0)
        fprintf(stderr, "CRPLAN world=%lld step=%lld plan=%d phase=%d age=%d screen=%d health=%.1f food=%d ore=%lld coal=%lld furnace=%lld near=%d provision=%d\n",
                (long long)c->r->wtime, (long long)c->e->steps, c->e->cplan, c->e->cphase, c->e->age,
                c->r->screen, c->r->health, c->r->food, (long long)inv_count(c->s, IRON_ORE),
                (long long)inv_count(c->s, COAL), (long long)inv_count(c->s, FURNACE),
                near_furnace(c->e, c->r), provision_mode(c->v, c->e, c->r, c->s));
#endif
    if (c->e->cplan == C_PORTAL && nt_trim(c->v, c->e, c->r, c->s, a, 1)) return 0;
#ifdef CR_TRACE
    {
        /* lane/obsid: a VARIANT build (XFLAGS=-DCR_TRACE): each smelt or take plan's end, a diamond pickaxe held */
        const struct neth_vec *v = c->v;
        struct vec_env *e = c->e;
        const struct vec_last *r = c->r;
        const struct neth_rl_state *s = c->s;
        int ph0 = e->cphase;
        int d = craft(v, e, r, s, a, c->ey);
        if (d && (e->cplan == C_SMELT || e->cplan == C_TAKE) && inv_count(s, DIAMOND_PICK) > 0)
            fprintf(stderr, "CRE %d %ld end plan %d age %d phase %d>%d screen %d hasf %d near %d f %.1f %.1f %.1f at %.1f "
                            "%.1f %.1f mo %d:%d,%d,%d furn %d ore %d coal %d pdig %d ptries %d placed %d used %d aimed %d "
                            "cur %d take_f %d block_now %d\n",
                    (int)(e - v->e), (long)e->steps, e->cplan, e->age, ph0, e->cphase, r->screen, e->has_furnace,
                    near_furnace(e, r), e->fx, e->fy, e->fz, r->x, r->y, r->z, s->mo_hit ? s->mo_block : -1, s->mo_x,
                    s->mo_y, s->mo_z, (int)inv_count(s, FURNACE), (int)inv_count(s, IRON_ORE), (int)inv_count(s, COAL),
                    e->pdig, e->ptries, e->placed, e->used, e->aimed, s->current, e->take_f,
                    obsview_block(v->p, (int)(e - v->e), (int)floor(e->fx), (int)floor(e->fy),
                                  (int)floor(e->fz), NULL));
        return d;
    }
#endif
    return craft(c->v, c->e, c->r, c->s, a, c->ey);
}

static uint32_t craft_m_label(const struct vec_env *e) { return e->kind == M_CRAFT ? NETH_VEC_CRAFT : 0; }

static int craft_m_half(struct mac_step *c, int done) { return c->e->chalf; }   /* lane/ladder12 */

/* lane/ladder12 (NETH_VEC_NORECIPE): the window open where the clicks would start: this step is the policy's, as on a
 * step no macro drives */
static int craft_m_after(struct mac_step *c, struct neth_rl_act *a, const struct neth_rl_act *a0)
{
    struct vec_env *e = c->e;
    int rec_off = (c->v->macros & NETH_VEC_NORECIPE) && c->kind0 == M_CRAFT;
    if (!(rec_off && e->chand)) return 0;
    e->chand = 0;
    e->gb[!e->rfirst ? 11 : e->cplan == C_SMELT ? 13 : 12] += 1;     /* lane/ladder13 */
    mac_end(e);
    *a = *a0;
    a->hotbar = c->ph;
    return 1;
}

static void craft_m_end(struct vec_env *e) { e->has_ops = 0; }

static void craft_m_reset(struct vec_env *e)
{
    e->has_ops = 0;
    e->has_table = 0;
    e->has_furnace = 0;
    e->pp_retry = 0;
    e->take_retry = 0;
    e->take_f = 0;
    e->x2st = e->x2n = e->x2at = 0;         /* lane/ladder17 */
}

/* lane/ladder14 (NETH_VEC_LBL_FITS): the decision's state, before decode's macros move anything (only under a window) */
static int craft_m_fits(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s)
{
    return window_fits(v, e, &e->last, s);
}

/* lane/ladder15 (table_chance on): the policy's step with no screen and a crafting plan named */
static int craft_m_chance(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *st, int kind,
                          int half)
{
    if (MOPT(MO_TABLE_CHANCE) && kind == M_NONE && e->last.screen == PS_NONE && plan_gui(craft_plan(v, e, &e->last, st)))
        return 5;
    return half;
}

/* the craft macro's GUI books (vecenv.h neth_vec_gui_books [18] to [30], [37], [38]; actions.gui cursor) */
static void craft_m_books(struct mac_books *b)
{
    if (b->at == MB_LABEL)
    {
        /* lane/ladder17: the 2x2 labeller (x2label.c), its label the step's at a level */
        const struct neth_vec *v = b->v;
        struct vec_env *e = b->e;
        if (MOPT(MO_RECIPE_2X2) == R2_CLICKS && MOPT(MO_GUI) == GUI_CURSOR &&
            ((e->lvl ? v->lv : v)->macros & NETH_VEC_NORECIPE))
            x2_label(e->lvl ? v->lv : v, e, b->s, b->a, b->kind, e->lvl ? b->l : NULL);
        return;
    }
    if (b->at != MB_GUI) return;
    const struct neth_vec *v = b->v;
    struct vec_env *e = b->e;
    const struct vec_last *r = &e->last;
    const struct neth_rl_state *s = b->s;
    int kind = b->kind;
    if (kind == M_NONE && (r->screen == PS_CRAFTING || r->screen == PS_INVENTORY))
    {
        /* lane/ladder14: what recipe_start finds under the policy's own window (its side effects already made) */
        int sc = r->screen, base = sc == PS_CRAFTING ? 18 : 23, why;
        int n = r->nslots > 90 ? 90 : r->nslots, grid = sc == PS_INVENTORY ? 4 : 9, tidy = 1;
        if (r->cur_item >= 0 && r->cur_count > 0) tidy = 0;
        for (int k = 1; k <= grid && k < n; ++k)
            if (r->slot_item[k] >= 0 && r->slot_count[k] > 0) tidy = 0;
        int p = tidy ? craft_plan(v, e, r, s) : -1;
        why = !tidy ? 0 : p < 0 ? 1 : plan_fits(p, sc) ? 2 : plan_gui(p) ? 3 : 4;
        e->gb[base + why] += 1;
        if (why == 3 && sc == PS_CRAFTING && (p == C_PLANKS || p == C_TABLE || p == C_STICK))
            e->gb[p == C_PLANKS ? 28 : p == C_TABLE ? 29 : 30] += 1;
    }
    if (kind == M_NONE)
    {
        const struct neth_vec *ev = e->lvl ? v->lv : v;
        if ((ev->macros & NETH_VEC_TABLEKEY) && r->screen == PS_NONE)
        {
            /* lane/ladder15: a policy step with no screen open while a crafting plan is named (the key's chance) */
            int p = craft_plan(ev, e, r, s);
            if (plan_gui(p))
            {
                e->gb[37] += 1;
                e->gb[38] += p == C_SMELT || plan_bench(p);
            }
        }
    }
}

/* lane/picks: the next action's allowed picks */
static void craft_m_mask(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, uint8_t *m)
{
    if (v->picks) pick_mask(v, e, &e->last, s, m);
}

/* the craft macro's switches: its plans past the base ones, the carried table, its halves off and their keys */
static const struct mac_switch CRAFT_SW[] = {
    {"craft", NETH_VEC_CRAFT}, {"table-anywhere", NETH_VEC_TABLE2}, {"iron", NETH_VEC_IRON}, {"late", NETH_VEC_LATE},
    {"liquid", NETH_VEC_LIQUID}, {"portal", NETH_VEC_PORTAL}, {"carry", NETH_VEC_CARRY},
    {"notable", NETH_VEC_NOTABLE}, {"norecipe", NETH_VEC_NORECIPE},      /* lane/ladder12: the table half off, the recipe's */
    {"recipekey", NETH_VEC_RECIPEKEY},  /* lane/ladder12: the recipe started by the policy's jump under a window */
    {"tablekey", NETH_VEC_TABLEKEY},    /* lane/ladder15: the table half started by the policy's inventory key */
    {NULL, 0}};

static const struct vec_macro MAC_CRAFT = {
    .name = "craft", .bit = NETH_VEC_CRAFT, .sw = CRAFT_SW, .observe = craft_m_observe, .cand = craft_m_cand, .start = craft_m_start,
    .drive = craft_m_drive, .label = craft_m_label, .half = craft_m_half, .after = craft_m_after, .end = craft_m_end,
    .reset = craft_m_reset, .books = craft_m_books, .fits = craft_m_fits, .chance = craft_m_chance, .mask = craft_m_mask,
};
