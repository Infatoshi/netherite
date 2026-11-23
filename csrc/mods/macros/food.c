/* NETH_VEC_FOOD: the hunt and eating (lane/macromod: cut from vecenv.c, unchanged; lane/food2: its options) */

/* lane/food2's constants (the options are vecopts.c's food_eat, food_hunt and food_break) */
#define FOOD_VIEW_MEAT 6        /* food_hunt view: a hunt starts with fewer meats than this held */
#define FOOD_STOCK 16           /* food_hunt stock: a hunt starts with fewer meats than this held */
#define FOOD_FLESH_AT 6         /* food_eat any: rotten flesh eaten at this food level or under, nothing else held */
#define FOOD_BREAK_AT 6         /* food_break on: eating cuts into a running macro at this food level or under (6: the
                                 * sprint stops) */

/* food_eat any: what is eaten, the most filling first (1.7.10's ItemFood heal amounts: 8 cooked pork, steak, pumpkin
 * pie; 6 cooked chicken, stew, baked potato; 5 bread, cooked fish; 4 apple, golden apple, carrot; 3 raw beef and pork;
 * 2 raw chicken and fish, melon, cookie; 1 potato), rotten flesh last (FOOD_FLESH_AT). Not the spider eye nor the
 * poisonous potato */
#define FLESH 367
static const int16_t EDIBLE[] = {320, 364, 400, 366, 282, 393, 297, 350, 260, 391, 363, 319, 365, 349, 360, 357, 392,
                                 322, FLESH};
#define EDIBLE_N ((int)(sizeof EDIBLE / sizeof *EDIBLE))

/* the j-th edible may be eaten at this food level and with what is held (rotten flesh only when nothing else is) */
static int edible_ok(const struct neth_rl_state *s, int j, int food)
{
    if (EDIBLE[j] != FLESH) return 1;
    if (food > FOOD_FLESH_AT) return 0;
    for (int k = 0; k < EDIBLE_N - 1; ++k)
        if (inv_count(s, EDIBLE[k]) > 0) return 0;
    return 1;
}

/* food held that the macro would eat now (food_eat's set) */
static int food_held(const struct neth_vec *v, const struct neth_rl_state *s, int food)
{
    if (MOPT(MO_FOOD_EAT) == FE_MEAT) return meat_count(s) > 0;
    for (int j = 0; j < EDIBLE_N; ++j)
        if (inv_count(s, EDIBLE[j]) > 0 && edible_ok(s, j, food)) return 1;
    return 0;
}

/* lane/chain (food_oracle on, PRIVILEGED: the search's, never a benchmark's): the provisions before the Nether. The
 * hunger points of the food held (1.7.10's ItemFood heal amounts, EDIBLE's order; rotten flesh none), and the
 * provisions wanted: the portal's kit held (10 obsidian, the flint and steel, a diamond pickaxe), the overworld, no
 * Nether yet, under FOOD_PROV points. While wanted the portal waits (craft.c), the strip's climb goes to the surface
 * (strip2.c) and the scout walks to the census's animal (scout) */
#define FOOD_PROV 40
#define FO_FLOOR 40.0           /* the provisions' scout given up with the feet under this (a cave; low land is not) */
static const int8_t EDIBLE_HEAL[] = {8, 8, 8, 6, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 2, 2, 1, 4, 0};
static int64_t food_points(const struct neth_rl_state *s)
{
    int64_t n = 0;
    for (int j = 0; j < EDIBLE_N; ++j) n += EDIBLE_HEAL[j] * inv_count(s, EDIBLE[j]);
    return n;
}
/* ... and first, early (the golden playthrough's third segment, pigs hunted and the pork cooked): from the stone
 * pickaxe until the iron pickaxe, FOOD_STOCK meats (raw or cooked) held */
static int prov_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_state *s)
{
    if (!MOPT(MO_FOOD_ORACLE) || !(v->macros & NETH_VEC_FOOD) || r->dim != 0 || (e->dims & 1) || (r->flags & PF_DEAD))
        return 0;
    if (inv_count(s, OBSIDIAN) >= OB_KEEP_N && inv_count(s, FLINT_STEEL) > 0 && inv_count(s, DIAMOND_PICK) > 0 &&
        food_points(s) < FOOD_PROV)
        return 1;
    if (inv_count(s, IRON_PICK) + inv_count(s, DIAMOND_PICK) > 0 ||
        meat_count(s) >= FOOD_STOCK)
        return 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (it >= 0 && it < 4096 && v->T.pick[it] >= 2) return 1;
    }
    return 0;
}

/* lane/food (NETH_VEC_FOOD): the food animal in view to hunt (its vis index), -1 none: under 6
 * meat held and a wooden pickaxe or better. No run of s64-w2-it55 ate: of 26 recorded 20-minute runs that died, the
 * starvation damage began 9,000 to 23,000 steps in, and 1 HP (normal's floor) made any fall or burn the end */
/* lane/chain2 (food_night): the pearl stage's night: the Nether done (blaze rods, powder or eyes held), pearls and eyes
 * under 12, in the overworld, the clock 13000 .. 23000 (pearl.c's PEARL_NIGHT0, PEARL_NIGHT1) */
static int food_pearl_stage(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    int64_t pe = inv_count(s, 368) + inv_count(s, 381);
    return (v->macros & NETH_VEC_PEARL) && r->dim == 0 && pe < 12 &&
           inv_count(s, 369) + inv_count(s, 377) + inv_count(s, 381) > 0;
}
#define FOOD_PEARL_COOKED 8     /* cooked meals held for the next pearl night */
static int64_t food_cooked_meat(const struct neth_rl_state *s)
{
    return inv_count(s, 320) + inv_count(s, 364) + inv_count(s, 366) + inv_count(s, 350);
}
static int food_pearl_night(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    int64_t t = ((r->wtime % 24000) + 24000) % 24000;
    return food_pearl_stage(v, r, s) && t >= 13000 && t < 23000;
}

/* the meats held that end a hunt's want (food_hunt) */
static int64_t hunt_cap(const struct neth_vec *v)
{
    return MOPT(MO_FOOD_HUNT) == FH_STOCK32 ? 32 : MOPT(MO_FOOD_HUNT) == FH_STOCK ? FOOD_STOCK : FOOD_VIEW_MEAT;
}

static int hunt_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    if (!(v->macros & NETH_VEC_FOOD) || e->steps < e->hunt_off ||
        (meat_count(s) >= hunt_cap(v) &&
         !prov_want(v, e, &e->last, s)))
        return -1;
    /* lane/chain2 (food_night pearl): a pearl night is the endermen's, with FOOD_VIEW_MEAT meats held */
    if (MOPT(MO_FOOD_NIGHT) && food_pearl_night(v, &e->last, s) && meat_count(s) >= FOOD_VIEW_MEAT) return -1;
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    if (tier < 1) return -1;
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
    {
        int k = s->vis[i].kind;
        if (k == AK_PIG || k == AK_COW || k == AK_MOOSHROOM || k == AK_CHICKEN) return i;
    }
    return -1;
}

/* lane/food (NETH_VEC_FOOD): 1 when a meat is to be eaten: the food level at 17 or under with health missing
 * (regeneration wants 18), or at 10 or under */
static int eat_want(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    /* lane/nether: in the Nether too with NETH_VEC_NETHER (the arrival's food is the trip's) */
    if (!(v->macros & NETH_VEC_FOOD) || r->screen != PS_NONE || (r->flags & PF_DEAD) ||
        (r->dim != 0 && !(r->dim == -1 && (v->macros & NETH_VEC_NETHER))))
        return 0;
    return (r->food <= 10 || (r->food <= 17 && r->health < 20.0f)) && food_held(v, s, r->food);
}

/* food_oracle's provisioning (prov_want): the meat is stocked, eaten only at FO_EAT_AT food or under or under
 * FO_EAT_HP health (the hunt's meat was eaten as got at 10 and under: e42l, easy, 302 meat got and none of 412
 * kit states holding any) */
#define FO_EAT_AT 6
#define FO_EAT_HP 10.0f
static int eat_want_e(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s)
{
    if (!eat_want(v, r, s)) return 0;
    return !prov_want(v, e, r, s) || r->food <= FO_EAT_AT || r->health < FO_EAT_HP;
}

/* lane/food: a meat eaten: cooked first, fetched into the hotbar when only the main inventory holds one, the use held
 * until the food level rises (32 ticks) */
static int eat(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
               struct neth_rl_act *a)
{
    a->hold = 0;
    memset(a->press, 0, sizeof a->press);
    if (r->food > e->efood) e->fb.eats += 1;   /* (lane/food2's census) */
    if (r->food > e->efood || e->age > 60) return 1;
    int hb = -1, item = -1;
    if (MOPT(MO_FOOD_EAT) == FE_ANY)
    {
        /* lane/food2 (food_eat any): the most filling food the hotbar holds, else the most filling held fetched */
        for (int j = 0; j < EDIBLE_N && hb < 0; ++j)
            for (int i = 0; i < 9 && hb < 0; ++i)
                if (s->inv[i].item == EDIBLE[j] && s->inv[i].count > 0 && edible_ok(s, j, r->food)) hb = i;
        if (hb < 0)
        {
            for (int j = 0; j < EDIBLE_N && item < 0; ++j)
                if (inv_count(s, EDIBLE[j]) > 0 && edible_ok(s, j, r->food)) item = EDIBLE[j];
            return item < 0 || fetch_item(v, e, r, s, a, item);
        }
    }
    for (int j = 5; j >= 0 && hb < 0; --j)
        for (int i = 0; i < 9 && hb < 0; ++i)
            if (s->inv[i].item == MEATS[j] && s->inv[i].count > 0) hb = i;
    if (hb < 0)
    {
        for (int j = 5; j >= 0 && item < 0; --j)
            if (inv_count(s, MEATS[j]) > 0) item = MEATS[j];
        return item < 0 || fetch_item(v, e, r, s, a, item);
    }
    a->hotbar = hb;
    if (s->current == hb && r->screen == PS_NONE) a->hold = 1u << PK_USE;
    return 0;
}


/* ------------------------------------------------------------ the mods (lane/macromod: macro.h's hooks): eating and
 * the hunt, one switch at two places in the start order */

static int eat_m_cand(struct mac_step *c) { return eat_want_e(c->v, c->e, c->r, c->s); }

static int eat_m_start(struct mac_step *c)
{
    struct vec_env *e = c->e;
    /* lane/food: eating, then a hunt while an animal is in view */
    if (eat_want_e(c->v, e, c->r, c->s))
    {
        e->kind = M_CRAFT;
        e->eat = 1;
        e->efood = c->r->food;
        e->fb.eat_starts += 1;
        e->placed = e->pressed = 0;
        return 1;
    }
    return 0;
}

/* lane/food2 (food_break on): eating cuts into a running macro that walks or digs (the strip but its lava escape, the
 * mine lock, the seek, the surface walk, the loot) at FOOD_BREAK_AT food or under with food held: that macro ended (its
 * own start rule takes it up again after), eating started this step */
static uint32_t mac_label(const struct vec_env *e, const struct vec_macro **who);
#define FOOD_BREAKS (NETH_VEC_STRIP | NETH_VEC_MINE | NETH_VEC_SEEK | NETH_VEC_SURFACE | NETH_VEC_LOOT)

static int eat_m_interrupt(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    if (MOPT(MO_FOOD_BREAK) == 0 || e->kind == M_NONE || r->food > FOOD_BREAK_AT || e->esc || e->fid ||
        e->kind == M_CRAFT || !eat_want_e(v, e, r, c->s))
        return 0;
    const struct vec_macro *who;
    if (!(mac_label(e, &who) & FOOD_BREAKS)) return 0;
    mac_abort(e);
    mac_end(e);
    return eat_m_start(c);
}

static int eat_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_CRAFT || !c->e->eat) return -1;
    return eat(c->v, c->e, c->r, c->s, a);
}

static uint32_t eat_m_label(const struct vec_env *e) { return e->kind == M_CRAFT && e->eat ? NETH_VEC_FOOD : 0; }

static void food_m_end(struct vec_env *e)
{
    if (e->hunt == 1) e->fb.hunt_end[3] += 1;   /* (the census: a hunt ended otherwise; fight.c books the rest as 2) */
    e->hunt = e->eat = 0;
}

static void food_census_reset(struct vec_env *e);

static void food_m_reset(struct vec_env *e)
{
    e->hunt = e->eat = 0;
    e->hunt_off = 0;
    food_census_reset(e);
}

static const struct mac_switch FOOD_SW[] = {{"food", NETH_VEC_FOOD}, {NULL, 0}};    /* (the hunt's too) */

/* ------------------------------------------------------------ lane/food2: the food census (vecenv.h neth_vec_food),
 * counting only, kept with the dbench books (neth_vec_opts.dbench): no act reads it */

/* the food that is not a meat, by its FB_ bit: apple; bread; rotten flesh; fish; stew; carrot, potato, baked potato;
 * the rest (golden apple, cookie, melon, pumpkin pie) */
enum { FB_APPLE = 1, FB_BREAD = 2, FB_FLESH = 4, FB_FISH = 8, FB_STEW = 16, FB_VEG = 32, FB_REST = 64 };
static const int16_t FOOD_OTHER[][2] = {{260, FB_APPLE}, {297, FB_BREAD}, {367, FB_FLESH}, {349, FB_FISH},
                                        {350, FB_FISH},  {282, FB_STEW},  {391, FB_VEG},    {392, FB_VEG},
                                        {393, FB_VEG},   {322, FB_REST},  {357, FB_REST},   {360, FB_REST},
                                        {400, FB_REST}};
#define FOOD_NOTHER ((int)(sizeof FOOD_OTHER / sizeof *FOOD_OTHER))

/* the other food held: its count (*bits the kinds) */
static int other_food(const struct neth_rl_state *s, int *bits)
{
    int n = 0, b = 0;
    for (int i = 0; i < 36; ++i)
        for (int j = 0; j < FOOD_NOTHER; ++j)
            if (s->inv[i].item == FOOD_OTHER[j][0] && s->inv[i].count > 0) n += s->inv[i].count, b |= FOOD_OTHER[j][1];
    if (bits) *bits = b;
    return n;
}

static int animal_in_view(const struct neth_rl_state *s)
{
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
    {
        int k = s->vis[i].kind;
        if (k == AK_PIG || k == AK_COW || k == AK_MOOSHROOM || k == AK_CHICKEN) return 1;
    }
    return 0;
}

static void food_books(struct mac_books *b)
{
    const struct neth_vec *v = b->v;
    struct vec_env *e = b->e;
    if (b->at != MB_RESULT || !v->dbench || e->reset_step) return;
    const struct pool_result *r = b->r;
    const struct neth_rl_state *s = b->s;
    struct neth_vec_food *f = &e->fb;
    uint32_t mbit = v->b.label[b->id].mbit;
    int mb = mbit ? 1 + __builtin_ctz(mbit) : 0, bits = 0;
    int meat = (int)meat_count(s), other = other_food(s, &bits), animal = animal_in_view(s), food = r->priv.food;
    int dead = (r->flags & (PF_DEAD | PF_DIED)) != 0;
    if (!dead && e->ep_steps > 0)
    {
        if (meat > e->fb_pmeat)
        {
            f->meat_got += meat - e->fb_pmeat;
            if (e->fb_hunt) f->hunt_meat += meat - e->fb_pmeat;
        }
        if (other > e->fb_pother) f->other_got += other - e->fb_pother;
        if (other < e->fb_pother && food > e->db_pfood) f->other_eaten += 1;
    }
    e->fb_pmeat = meat, e->fb_pother = other;
    e->fb_hunt = e->kind == M_MINE && e->hunt;
    /* a scout's trip: from the scout's start until no scout, hunt or walk home runs or is owed */
    if (e->fs_trip)
    {
        if (!dead && meat > e->fs_tmeat) f->trip_meat += meat - e->fs_tmeat;
        e->fs_tmeat = meat;
        f->trip_steps += 1;
        if (dead || (!e->fs_on && !e->fb_hunt && !(e->fs_home & 3))) e->fs_trip = 0, e->fs_tripped = 1;
    }
    if (dead) return;
    if (food <= 6 && food_held(v, s, food)) f->low_held += 1;
    if (e->fs_tripped && !e->fs_trip && r->priv.y < 56.0) f->deep_after += 1;
    f->animal_steps += animal;
    if (r->priv.dim == 0 && e->db_pdim == 0)
    {
        int band = food > 6 ? 0 : food > 0 ? 1 : 2;
        double dx = r->priv.x - e->fb_px, dz = r->priv.z - e->fb_pz, d = sqrt(dx * dx + dz * dz);
        f->band_steps[band] += 1;
        if (e->ep_steps > 0 && d < 2.0) f->band_cm[band] += (int32_t)(d * 100.0 + 0.5);
        f->band_deep[band] += r->priv.y < 56.0;
    }
    e->fb_px = r->priv.x, e->fb_pz = r->priv.z;
    if (food <= 6 && f->low_step < 0)
    {
        f->low_step = (int32_t)e->ep_steps, f->low_meat = meat, f->low_other = bits, f->low_y = (int32_t)r->priv.y;
        f->low_mb = mb, f->low_animal = animal;
    }
    if (food <= 0 && f->zero_step < 0)
    {
        f->zero_step = (int32_t)e->ep_steps, f->zero_meat = meat, f->zero_other = bits, f->zero_y = (int32_t)r->priv.y;
        f->zero_mb = mb, f->zero_animal = animal;
    }
    if (food > 10) return;
    int w = e->kind == M_CRAFT && e->eat ? FW_EATING : meat > 0 ? (mb ? FW_MEAT_MACRO : FW_MEAT_POLICY)
            : animal ? FW_ANIMAL : other > 0 ? FW_OTHER : FW_NOTHING;
    f->hungry[w] += 1;
    if (food <= 0) f->starving[w] += 1;
    if (meat > 0 && w != FW_EATING) f->meat_mb[mb & 31] += 1;
}

int neth_vec_food(const struct neth_vec *v, int id, struct neth_vec_food *out)
{
    if (v == NULL || id < 0 || id >= v->n || out == NULL || !v->dbench) return -1;
    *out = v->e[id].fb;
    return 0;
}

static void food_census_reset(struct vec_env *e)
{
    memset(&e->fb, 0, sizeof e->fb);
    e->fb.low_step = e->fb.zero_step = -1;
    e->fb_pmeat = e->fb_pother = e->fb_hunt = 0;
    e->fs_trip = e->fs_tripped = 0;
}

static const struct vec_macro MAC_EAT = {
    .name = "food", .bit = NETH_VEC_FOOD, .sw = FOOD_SW, .cand = eat_m_cand, .start = eat_m_start, .drive = eat_m_drive,
    .label = eat_m_label, .end = food_m_end, .reset = food_m_reset, .books = food_books, .interrupt = eat_m_interrupt,
};

/* the hunt of the animal at vis index h begun (the fight's lock on it) */
static void hunt_begin(struct vec_env *e, const struct neth_rl_state *s, int h)
{
    e->kind = M_MINE;
    e->fid = s->vis[h].id;
    e->flost = 0;
    e->flast = -100;
    e->hunt = 1;
    e->fb.hunts += 1;
    e->hmeat = (int)meat_count(s);
    e->hk0 = s->mob_kills;          /* (the census: a lost hunt's kill) */
    e->fs_det = 0;                  /* (food_chase far's detour; read only with it on) */
    e->hpk = 0;                     /* (food_chase far's search for a kill's drop) */
    e->hx = s->vis[h].x, e->hy = s->vis[h].y, e->hz = s->vis[h].z;
}

/* ------------------------------------------------------------ lane/food2 (food_scout): the look for food animals.
 * Seed 42's spawn has its nearest pigs 78 to 98 blocks out (48 within 200 blocks) and the hunt starts only on one in
 * view (80 blocks, 75 degrees either side of the look, a clear line), so 210 of 256 frozen-policy runs never saw one
 * and 244 reached food 0. The scout, by day at the surface with a pickaxe and short of meat once the food level has
 * begun to fall: a turn on the spot (look), then (walk) a square spiral of growing legs at the surface with a turn on
 * the spot at each leg's end, until an animal is in view, when the hunt takes the step over. What it reads: the food
 * level, the inventory, the feet's height and the ground under them, the world time (the sky), the livings in view */
#define FOOD_SCOUT_AT 17        /* the scout starts at this food level or under (the bar has begun to fall) */
#define FOOD_SCOUT_Y 62.0       /* ... with the feet at this height or over (the surface; sea level 63) */
#define FOOD_SCOUT_SWEEP 36     /* steps of a turn on the spot (10 degrees a step) */
#define FOOD_SCOUT_LEG 24.0     /* the spiral's first legs, blocks; the k-th leg is LEG * (1 + k / 2) */
#define FOOD_SCOUT_MAX 2400     /* a scout given up after this many steps (2 minutes) */
#define FOOD_SCOUT_RETRY 6000   /* no scout for this many steps after one that found nothing (5 minutes) */
#define FOOD_SCOUT_N 4          /* scouts begun at most an episode */
#define FOOD_SCOUT_STUCK 20     /* a leg that moved under a block in this many steps turns 45 degrees */
#define FOOD_SCOUT_AIR 150      /* in water, the scout given up (the swim macro's) under this much air */

static int food_day(const struct vec_last *r) { return r->wtime % 24000 < 12500; }

/* lane/bportal (food_scout_sky): the sky over the player as the option reads it: on, the census's pl_sky (the server's
 * height map: leaves cover it); foliage, the sky over the eye through foliage (shoracle_sky_here, the stronghold
 * oracle's; PRIVILEGED as the census is). Seed 2's obsidian start under on: 5,203 of the wait's rows on the surface
 * with an animal within 64 and none in view stood under leaves, the scout refused and the strip's climb holding them */
enum { FSS_OFF, FSS_ON, FSS_FOLIAGE };
static int scout_sky(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_far *f)
{
    if (MOPT(MO_FOOD_SCOUT_SKY) == FSS_FOLIAGE)
        return shoracle_sky_here(v->p, (int)(e - v->e), (int)floor(r->x), (int)floor(r->y + 1.62), (int)floor(r->z));
    return f->census.pl_sky;
}

static int scout_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s)
{
    /* lane/chain (food_oracle): the provisions' scout, by day or night, wherever the census has an animal */
    if (prov_want(v, e, r, s))
    {
        const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
        if (f && !f->census.on) neth_rl_far_census(v->h, 1);   /* (every env's, from the next step) */
        return f && f->census.on && f->census.food_n > 0 && r->screen == PS_NONE && e->steps >= e->fs_off &&
               (r->y >= FOOD_SCOUT_Y - 4.0 || (f->census.pl_sky && r->y >= FO_FLOOR + 5.0)) && r->on_ground &&
               (!MOPT(MO_FOOD_SCOUT_SKY) || scout_sky(v, e, r, f)) &&     /* lane/bportal: food_scout_sky */
               !s->eye_water && !animal_in_view(s) &&
               e->steps >= e->hunt_off;
    }
    if (MOPT(MO_FOOD_SCOUT) == FS_OFF || !(v->macros & NETH_VEC_FOOD) || r->dim != 0 || r->screen != PS_NONE ||
        (r->flags & PF_DEAD) || e->steps < e->fs_off ||
        (e->fs_n >= (MOPT(MO_FOOD_TRIPS) == FT_ONE ? 1 : MOPT(MO_FOOD_TRIPS) == FT_TWO ? 2 : FOOD_SCOUT_N) &&
         !(MOPT(MO_FOOD_TRIPS) == FT_PEARL && food_pearl_stage(v, r, s)) &&
         provision_mode(v, e, r, s) != PV_STOCK) ||
        ((MOPT(MO_FOOD_TRIPS) == FT_ONE || MOPT(MO_FOOD_TRIPS) == FT_TWO) && food_held(v, s, 0)) ||
        (MOPT(MO_FOOD_SCOUT) != FS_EARLY && r->food > FOOD_SCOUT_AT && provision_mode(v, e, r, s) != PV_STOCK &&
         !(MOPT(MO_FOOD_TRIPS) == FT_PEARL && food_pearl_stage(v, r, s) &&
           food_cooked_meat(s) < FOOD_PEARL_COOKED)) ||
        r->y < FOOD_SCOUT_Y || !r->on_ground || s->eye_water || !food_day(r) || animal_in_view(s))
        return 0;
    int64_t cap = hunt_cap(v);
    if (meat_count(s) >= cap || e->steps < e->hunt_off) return 0;
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    /* (early after the stone pickaxe, measured: 25 scouts in 64 runs, the player mostly under the surface by then) */
    return tier >= 1;
}

/* lane/food2 (food_scout early): the walk home after the scout and its hunts, to where the scout began (the
 * position F3 shows): measured, the early scout without it left the stone pickaxe at 0.625 of 64 runs against 0.938
 * (the policy far from the trees and the stone it knows). Begun when no hunt or scout runs and the place is over
 * FOOD_HOME_NEAR blocks away; a hunt takes over when an animal comes into view on the way */
#define FOOD_HOME_NEAR 4.0
#define FOOD_HOME_MAX 2400      /* a walk home given up after this many steps */
#define FOOD_HOME_N 3           /* walks home begun at most for one scout */

static int home_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_state *s)
{
    return (e->fs_home & 3) == 1 && e->fs_hn < FOOD_HOME_N && (v->macros & NETH_VEC_FOOD) && r->dim == 0 &&
           r->screen == PS_NONE && !(r->flags & PF_DEAD) && py_hypot2(r->x - e->fs_hx, r->z - e->fs_hz) > FOOD_HOME_NEAR;
}

/* the walk home's step: 1 when over */
static int home(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                struct neth_rl_act *a)
{
    still(a);
    a->hotbar = -1;
    int h = hunt_want(v, e, s);
    if (h >= 0)
    {
        e->fs_on = 0;
        hunt_begin(e, s, h);
        e->age = 0;
        return 0;
    }
    double dx = e->fs_hx - r->x, dz = e->fs_hz - r->z, d = py_hypot2(dx, dz);
    if (d <= FOOD_HOME_NEAR || e->age > FOOD_HOME_MAX || r->y < FOOD_SCOUT_Y - 8.0 ||
        (s->eye_water && r->air < FOOD_SCOUT_AIR) || r->screen != PS_NONE)
    {
        e->fb.home_end[d <= FOOD_HOME_NEAR ? 0 : 1] += 1;
        if (d <= FOOD_HOME_NEAR || e->age > FOOD_HOME_MAX || r->y < FOOD_SCOUT_Y - 8.0) e->fs_home &= ~3;
        e->fs_on = 0;
        return 1;
    }
    /* toward home, a 45-degree detour each time a step has not moved a block in FOOD_SCOUT_STUCK steps */
    double yaw = atan2(-dx, dz) * 180.0 / M_PI;
    if (py_hypot2(r->x - e->fs_mx, r->z - e->fs_mz) >= 1.0) e->fs_mx = r->x, e->fs_mz = r->z, e->fs_mt = e->steps;
    if (e->steps - e->fs_mt > FOOD_SCOUT_STUCK) e->fs_det += 1, e->fs_mt = e->steps, e->fs_legt = e->steps;
    if (e->fs_det > 0 && e->steps - e->fs_legt < 2 * FOOD_SCOUT_STUCK)
        yaw += 45.0 * (double)(1 + (e->fs_det - 1) % 3);     /* (fs_legt: the detour's first step) */
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = 0.0f;
    a->hold = 1u << PK_FORWARD | (r->food > 6 ? 1u << PK_SPRINT : 0u);
    double mv = py_hypot2(r->x - e->gpx, r->z - e->gpz);
    e->gstuck = mv < 0.05 ? e->gstuck + 1 : 0;
    if (e->gstuck > 2 || s->eye_water) a->hold |= 1u << PK_JUMP;
    e->gpx = r->x, e->gpz = r->z;
    return 0;
}

/* lane/chain (food_oracle, PRIVILEGED): the provisions' scout step: toward the census's nearest food animal, along
 * walk_to's path within FO_PATH_R (the server's pathfinder), else straight at it with home()'s detours; 1 when over
 * (FOOD_SCOUT_MAX steps, under the surface, low air, a screen, no animal left in the census) */
#define FO_PATH_R 40.0
#define FO_WAY 32.0             /* a far animal's waypoint: this far along the line to it */
#define FO_STUCK 40             /* steps with no move across: the pillar out */
static int walk_to(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, struct walkto_st *w, int x, int y, int z);   /* (walkto.c) */
static int food_oracle_walk(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                            const struct neth_rl_state *s, struct neth_rl_act *a, int low_air)
{
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    e->wk = 0;                  /* (lane/pdig, counting only) */
    if (f == NULL || !f->census.on || f->census.food_n == 0 || e->age > FOOD_SCOUT_MAX || r->y < FO_FLOOR ||
        low_air || r->screen != PS_NONE)
    {
        e->fb.scout_end[e->age > FOOD_SCOUT_MAX ? 1 : r->y < FO_FLOOR ? 2 : low_air ? 3 : 4] += 1;
        e->fs_on = 0;
        e->fs_off = e->steps + 200;
        return 1;
    }
    double tx = f->census.food_x, ty = f->census.food_y, tz = f->census.food_z;
    double dx = tx - r->x, dz = tz - r->z, d = py_hypot2(dx, dz);
#ifdef FO_TRACE
    if (e->age % 100 == 0)
        fprintf(stderr, "FOT %d %ld age %ld at %.1f %.1f %.1f to %.1f %.1f %.1f sky %d d %.1f n %d path %d k %d det %d "
                "gst %d water %d\n", (int)(e - v->e), (long)e->steps, (long)e->age, r->x, r->y, r->z, tx, ty, tz,
                f->census.food_sky, d, f->census.food_n, e->fo_wt.have ? e->fo_wt.path.n : -9, e->fo_wt.k, e->fs_det,
                e->gstuck, s->eye_water);
#endif
    int wx = (int)floor(tx), wy = (int)floor(ty + 1e-3), wz = (int)floor(tz), way = d <= FO_PATH_R;
    if (!way && d > 1e-6)
    {
        /* a waypoint FO_WAY blocks along the line: the standing cell nearest the feet's height in its column (the
         * client world: a solid block under two of air) */
        int cx = (int)floor(r->x + dx / d * FO_WAY), cz = (int)floor(r->z + dz / d * FO_WAY), fy = (int)floor(r->y);
        for (int k = 0; k <= 12 && !way; ++k)
            for (int sg = -1; sg <= 1 && !way; sg += 2)
            {
                int y = fy + sg * k, id = (int)(e - v->e);
                int b0 = obsview_block(v->p, id, cx, y - 1, cz, NULL), b1 = obsview_block(v->p, id, cx, y, cz, NULL),
                    b2 = obsview_block(v->p, id, cx, y + 1, cz, NULL);
                if (b0 > 0 && !(b0 >= 8 && b0 <= 11) && b1 == 0 && b2 == 0) wx = cx, wy = y, wz = cz, way = 1;
            }
    }
    if (way && fabs((double)wy - r->y) < FO_PATH_R)
    {
        int k = walk_to(v, e, r, s, a, &e->fo_wt, wx, wy, wz);
#ifdef FO_TRACE
        if (e->age % 100 == 0)
            fprintf(stderr, "FOW %d way %d %d %d k %d seq %d have %d n %d\n", (int)(e - v->e), wx, wy, wz, k,
                    e->fo_wt.seq, e->fo_wt.have, e->fo_wt.path.n);
#endif
        if (k == 0)
        {
            a->look[1] = 0.0f;
            if (r->food > 6) a->hold |= 1u << PK_SPRINT;
            return 0;
        }
        /* there (the animal moved on: the next step's census) or no path: the straight walk this step */
    }
    still(a);
    double yaw = atan2(-dx, dz) * 180.0 / M_PI;
    /* (lane/pdig, counting only: the straight walk's stamps keep in their way-length field the walk_to answer that sent
     * the step here: WK_WT + 3 no path, + 6 there (a waypoint reached); 0 when walk_to was not asked) */
    int wk0 = e->wk;
    /* stuck FO_STUCK steps (a pit, the strip's shaft out): a pillar, a block of cobblestone or dirt from the hotbar
     * placed under the feet at the jump's top, the block over the head dug first; walking again once a block up */
    if (e->gstuck > FO_STUCK || e->fo_tw)
    {
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z), id = (int)(e - v->e);
        if (!e->fo_tw) e->fo_tw = 1, e->fo_ty = r->y, e->fo_base = r->y;
        if (r->on_ground && r->y >= e->fo_ty + 0.99)
        {
            e->fo_tw = 0, e->gstuck = 0;     /* a block up: the walk again */
            e->gpx = r->x, e->gpz = r->z;
        }
        else
        {
            a->look_mode = 1;
            a->look[0] = r->yaw;
            int up = obsview_block(v->p, id, fx, fy + 2, fz, NULL);
            if (up > 0 && !(up >= 8 && up <= 11))
            {
                a->look[1] = -90.0f;
                a->hold = 1u << PK_ATTACK;
                return WK_STAMP(e, WK_FOS + 1, d, ty - r->y, wk0), 0;
            }
            int hb = -1;
            for (int i = 0; i < 9 && hb < 0; ++i)
                if ((s->inv[i].item == COBBLE || s->inv[i].item == 3) && s->inv[i].count > 0) hb = i;
            if (hb < 0) { e->fo_tw = 0, e->gstuck = 0; }   /* nothing to stand on: the walk's detours */
            else
            {
                a->hotbar = hb;
                a->look[1] = 90.0f;
                if (r->on_ground) e->fo_base = r->y;
                a->hold = 1u << PK_JUMP;
                if (s->current == hb && r->y > e->fo_base + 1.05) a->press[PK_USE] = 1;
                return WK_STAMP(e, WK_FOS + 2, d, ty - r->y, wk0), 0;
            }
        }
    }
    if (py_hypot2(r->x - e->fs_mx, r->z - e->fs_mz) >= 1.0) e->fs_mx = r->x, e->fs_mz = r->z, e->fs_mt = e->steps;
    if (e->steps - e->fs_mt > FOOD_SCOUT_STUCK) e->fs_det += 1, e->fs_mt = e->steps, e->fs_legt = e->steps;
    WK_STAMP(e, WK_FOS + 3, d, ty - r->y, wk0);
    if (e->fs_det > 0 && e->steps - e->fs_legt < 2 * FOOD_SCOUT_STUCK)
        yaw += 45.0 * (double)(1 + (e->fs_det - 1) % 3), WK_STAMP(e, WK_FOS + 4, d, ty - r->y, wk0);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = 0.0f;
    a->hold = 1u << PK_FORWARD | (r->food > 6 ? 1u << PK_SPRINT : 0u);
    WK_FRONT(e, yaw);
    double mv = py_hypot2(r->x - e->gpx, r->z - e->gpz);
    e->gstuck = mv < 0.05 ? e->gstuck + 1 : 0;
    if (e->gstuck > 2 || s->eye_water) a->hold |= 1u << PK_JUMP;
    e->gpx = r->x, e->gpz = r->z;
    return 0;
}

/* the scout's step: 1 when over */
static int scout(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                 struct neth_rl_act *a)
{
    still(a);
    a->hotbar = -1;
    int h = hunt_want(v, e, s);
    if (h >= 0)
    {
        e->fb.scout_end[0] += 1;
        e->fb.scout_far += (int32_t)e->fs_far;
        e->fs_on = 0;
        e->fb.scout_found += 1;
        hunt_begin(e, s, h);
        e->age = 0;
        return 0;                   /* the hunt's lock from the next step (fight.c's drive) */
    }
    double from = py_hypot2(r->x - e->fs_sx, r->z - e->fs_sz);
    if (from > e->fs_far) e->fs_far = from;
    int low_air = s->eye_water && r->air < FOOD_SCOUT_AIR;
    /* lane/pdig (scout_census on, with food_oracle): after the Nether (prov_want is the pre-Nether want and false from
     * the first iron pickaxe), hungry with no food held, the scout walks to the census's animal, not the spiral */
    int census = MOPT(MO_SCOUT_CENSUS) && MOPT(MO_FOOD_ORACLE) && (e->dims & 1) && r->dim == 0 && r->food < 14 &&
                 food_points(s) == 0;
    if (census)
    {
        const struct neth_rl_far *cf = neth_rl_far(v->h, (int)(e - v->e));
        if (cf && !cf->census.on) neth_rl_far_census(v->h, 1);
    }
    if (prov_want(v, e, r, s) || census ||
        (MOPT(MO_FOOD_ORACLE) && provision_mode(v, e, r, s) == PV_STOCK))
        return food_oracle_walk(v, e, r, s, a, low_air);
    if (e->age > FOOD_SCOUT_MAX || r->y < FOOD_SCOUT_Y - 4.0 || low_air || r->screen != PS_NONE ||
        !food_day(r) || e->steps < e->hunt_off)
    {
        e->fb.scout_end[e->age > FOOD_SCOUT_MAX ? 1 : r->y < FOOD_SCOUT_Y - 4.0 ? 2 : low_air ? 3 : 4] += 1;
        e->fb.scout_far += (int32_t)e->fs_far;
        e->fs_on = 0;
        e->fs_off = e->steps + FOOD_SCOUT_RETRY;
        return 1;
    }
    a->look_mode = 1;
    a->look[1] = 0.0f;
    int64_t t = e->steps - e->fs_legt;
    if (e->fs_sweep)
    {
        /* the turn on the spot, 10 degrees a step from the leg's heading */
        a->look[0] = (float)(e->fs_yaw + 10.0 * (double)t);
        if (s->eye_water) a->hold = 1u << PK_JUMP;     /* afloat */
        if (t < FOOD_SCOUT_SWEEP) return WK_STAMP(e, WK_OTH + 2, 0, 0, 0), 0;
        if (MOPT(MO_FOOD_SCOUT) == FS_LOOK)
        {
            e->fs_on = 0;
            e->fs_off = e->steps + FOOD_SCOUT_RETRY;
            return 1;
        }
        e->fs_sweep = 0;
        e->fs_legt = e->steps;
        e->fs_x = e->fs_mx = r->x, e->fs_z = e->fs_mz = r->z;
        e->fs_mt = e->steps;
        t = 0;
    }
    /* the leg: forward along its heading, sprinting while the food level allows, a jump where a step does not move */
    double len = FOOD_SCOUT_LEG * (1.0 + (double)(e->fs_leg / 2));
    a->look[0] = (float)e->fs_yaw;
    a->hold = 1u << PK_FORWARD | (r->food > 6 ? 1u << PK_SPRINT : 0u);
    WK_STAMP(e, WK_OTH + 1, py_hypot2(r->x - e->fs_sx, r->z - e->fs_sz), 0, e->fs_leg), WK_FRONT(e, e->fs_yaw);
    e->wkc = (s->eye_water != 0) + 2 * (r->on_ground != 0);
    double mv = py_hypot2(r->x - e->gpx, r->z - e->gpz);
    e->gstuck = mv < 0.05 ? e->gstuck + 1 : 0;
    if (e->gstuck > 2 || s->eye_water) a->hold |= 1u << PK_JUMP;  /* (in water: swum across at the top) */
    e->gpx = r->x, e->gpz = r->z;
    if (py_hypot2(r->x - e->fs_mx, r->z - e->fs_mz) >= 1.0) e->fs_mx = r->x, e->fs_mz = r->z, e->fs_mt = e->steps;
    if (e->steps - e->fs_mt > FOOD_SCOUT_STUCK)
    {
        /* blocked (a wall, a tree): the heading turned 45 degrees, the leg goes on */
        e->fs_yaw += 45.0;
        e->fs_mt = e->steps;
        return WK_STAMP(e, WK_OTH + 3, e->wkd, 0, e->fs_leg), 0;
    }
    if (py_hypot2(r->x - e->fs_x, r->z - e->fs_z) >= len)
    {
        /* the leg's end: a quarter turn and a turn on the spot */
        e->fs_leg += 1;
        e->fs_yaw += 90.0;
        e->fs_sweep = 1;
        e->fs_legt = e->steps;
    }
    return 0;
}

static int hunt_m_cand(struct mac_step *c)
{
    return (c->e->steps % 5 == 0 && hunt_want(c->v, c->e, c->s) >= 0) || scout_want(c->v, c->e, c->r, c->s) ||
           home_want(c->v, c->e, c->r, c->s);
}

static int hunt_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    int h = e->steps % 5 == 0 ? hunt_want(c->v, e, s) : -1;
    if (h >= 0)
    {
        hunt_begin(e, s, h);
        return 1;
    }
    if (scout_want(c->v, e, c->r, s))
    {
        e->kind = M_SURFACE;
        e->fs_on = 1;
        e->fs_n += 1;
        e->fb.scouts += 1;
        e->fs_sx = c->r->x, e->fs_sz = c->r->z, e->fs_far = 0.0;
        e->fs_trip = 1, e->fs_tmeat = (int)meat_count(s), e->fb.trips += 1;
        e->fs_sweep = 1;
        e->fs_leg = 0;
        e->fs_yaw = (double)c->r->yaw;
        e->fs_legt = e->steps;
        e->gstuck = 0;
        e->gpx = c->r->x, e->gpz = c->r->z;
        if (MOPT(MO_FOOD_SCOUT) == FS_EARLY) e->fs_home = (e->fs_home & ~3) | 1, e->fs_hn = 0, e->fs_hx = c->r->x, e->fs_hz = c->r->z;
        return 1;
    }
    if (home_want(c->v, e, c->r, s))
    {
        e->kind = M_SURFACE;
        e->fs_on = 2;
        e->fs_hn += 1;
        e->fb.homes += 1;
        e->fs_det = 0;
        e->fs_mx = c->r->x, e->fs_mz = c->r->z, e->fs_mt = e->steps;
        e->gstuck = 0;
        e->gpx = c->r->x, e->gpz = c->r->z;
        return 1;
    }
    return 0;
}

static int hunt_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_SURFACE || !c->e->fs_on) return -1;
    if (c->e->fs_on == 2) return home(c->v, c->e, c->r, c->s, a);
    return scout(c->v, c->e, c->r, c->s, a);
}

/* the hunt runs as the fight's lock (fight.c drives it) and is labelled the food's; the scout too */
static uint32_t hunt_m_label(const struct vec_env *e)
{
    return (e->kind == M_MINE && e->hunt) || (e->kind == M_SURFACE && e->fs_on) ? NETH_VEC_FOOD : 0;
}

static void hunt_m_end(struct vec_env *e)
{
    if (e->fs_on == 1) e->fb.scout_end[5] += 1, e->fb.scout_far += (int32_t)e->fs_far;    /* cut short (the census) */
    if (e->fs_on == 2) e->fb.home_end[2] += 1;
    e->fs_on = 0;
}

static void hunt_m_reset(struct vec_env *e)
{
    e->fs_on = e->fs_n = 0;
    e->fs_off = 0;
    e->fs_home &= ~3;
    e->fs_hn = 0;
    memset(&e->fo_wt, 0, sizeof e->fo_wt);
    e->fo_tw = 0;
}

static const struct vec_macro MAC_HUNT = {
    .name = "food", .bit = NETH_VEC_FOOD, .cand = hunt_m_cand, .start = hunt_m_start, .drive = hunt_m_drive,
    .label = hunt_m_label, .end = hunt_m_end, .reset = hunt_m_reset,
};
