/* An opt-in exclusive food stage. Its acts are the existing climb, food and craft controllers; no new controller
 * bytes are stored, so a version-2 chain image made before this option still restores exactly. */
#define PROVISION_FOOD 160
#define PROVISION_ARMOR 15
#define PROVISION_SCOUT_FAILS 2
#define PROVISION_ROAM_STEPS 300
#define PROVISION_LATCH 8   /* fs_home's older states are 0, 1 and 2; this bit persists across a disk seam */
#define PROVISION_ROAM 16
#define PROVISION_DONE 32
#define PROVISION_EXIT 64
#define PROVISION_EXIT_CAP 128
#define PROVISION_EXIT_DONE 256
#define PROVISION_WOOD_ROUTE 512
#define PROVISION_WOOD_DONE 1024
#define PROVISION_WOOD_FAIL 2048
#define PROVISION_WOOD_CRAFT_LATCH 4096
#define PROVISION_SURFACE_Y 64.0
#define PROVISION_EXIT_Y 65.0
#define PROVISION_EXIT_R 12
#define PROVISION_EXIT_AWAY 4.0
#define PROVISION_WOOD_RADIUS 24
#define PROVISION_WOOD_RETRY_INIT 8192
#define PROVISION_WOOD_RETRY_BELOW 16384
#define PROVISION_AIR_RESCUE 32768 /* fs_home's opt-in rescue latch; version-2 images already store this word */
#define PROVISION_WOOD_RETRY_DIST 32.0
#define PROVISION_WOOD_Y_DOWN 2
#define PROVISION_WOOD_Y_UP 12
#define PROVISION_WOOD_MAX 10000 /* the 23-cell seed-3 path digs stone by hand; 3000 ended mid-stair */
#define PROVISION_WOOD_STUCK 600 /* lane/proom (food_wood_stuck on): steps with the feet not moved a block (the walk's
                                  * longest dig, SHO_DIG_BARE_FURNACE, is 480) */
#define PROVISION_WOOD_STUCK_FAIL 1048576  /* fs_home (lane/proom): the route's fail was a stand: food_wood_retry's
                                            * rearms (the surface again, the first step) leave it; only the move of
                                            * PROVISION_WOOD_RETRY_DIST from where it gave up clears it */
#define PROVISION_WOOD_ATTACK_R 2
#define PROVISION_WOOD_ATTACK_Y 5
#define PROVISION_CRAFT_LOGS 2

/* A pickaxe-less return can end on its own cobblestone column with a deep air shaft beside it. This is a privileged
 * food-oracle route: the server's blocks tell us whether that column, not natural ground, supports the player. */
static int provision_pillar_top(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                                const struct neth_rl_state *s)
{
    if (!MOPT(MO_FOOD_ORACLE) || !(e->dims & 1) || r->y < PROVISION_EXIT_Y ||
        st3_tier(v, s) >= 1 || inv_count(s, COBBLE) < ST3_PIL_COBBLE) return 0;
    int id = (int)(e - v->e), x = (int)floor(r->x), y = (int)floor(r->y + 0.01), z = (int)floor(r->z);
    return shoracle_block(v->p, id, x, y - 1, z, NULL) == COBBLE &&
           shoracle_block(v->p, id, x, y - 2, z, NULL) == COBBLE;
}

static int provision_sword(const struct neth_rl_state *s)
{
    return inv_count(s, 268) + inv_count(s, 272) + inv_count(s, 267) + inv_count(s, 276) + inv_count(s, 283) > 0;
}

static int provision_under(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    if (f && !f->census.on) neth_rl_far_census(v->h, 1);
    if (MOPT(MO_PROV_UNDER_SURFACE) == 1) return r->y < FOOD_SCOUT_Y;
    return r->y < FOOD_SCOUT_Y || (f && f->census.on && !f->census.pl_sky);
}

/* lane/pdig (prov_wood_food=on): by day with the food level under 14 and no food item held, the food want comes before
 * the craft's wood route (its PVW_CRAFT_WOOD line and the wood step's craft are not taken) */
static int provision_wood_hungry(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    return MOPT(MO_PROV_WOOD_FOOD) && food_day(r) && r->food < 14 && food_points(s) == 0;
}

/* A craft that has its ore or diamonds and handles but lacks the wood for a table, sticks or fuel. Logs already held
 * are left to the craft controller; the route begins only when there is no log to turn into planks. */
static int provision_craft_wood_need(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                                     const struct neth_rl_state *s)
{
    if (r->dim != 0 || r->screen != PS_NONE || r->y < PROVISION_SURFACE_Y ||
        inv_count(s, LOG) + inv_count(s, LOG2) > 0) return 0;
    int tier = st3_tier(v, s);
    int pick = (inv_count(s, DIAMOND) >= 3 && inv_count(s, DIAMOND_PICK) == 0) ||
               (inv_count(s, INGOT) >= 3 && tier < 3) || (inv_count(s, COBBLE) >= 3 && tier < 2);
    int furnace = inv_count(s, IRON_ORE) > 0 && inv_count(s, FURNACE) == 0 && inv_count(s, COBBLE) >= 8;
    int armor = MOPT(MO_ARMOR_KIT) && arm_need(v, e, s) > 0 && inv_count(s, INGOT) >= 4;
    int table = inv_count(s, TABLE) > 0 || near_table(e, r);
    int64_t planks = inv_count(s, PLANKS), sticks = inv_count(s, STICK);
    return (!table && planks < 4 && (pick || furnace || armor)) ||
           (sticks < 2 && planks < 2 && pick) || st3_food_fuel_want(v, e, s);
}

static void provision_retry_observe(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    if (!MOPT(MO_FOOD_WOOD_RETRY)) return;
    if (!(e->fs_home & PROVISION_WOOD_RETRY_INIT))
    {
        e->fs_home |= PROVISION_WOOD_RETRY_INIT;
        if (r->dim == 0 && r->y >= PROVISION_SURFACE_Y && !(e->fs_home & PROVISION_WOOD_STUCK_FAIL))
            e->fs_home &= ~PROVISION_WOOD_FAIL;
    }
    if (r->dim != 0 || r->y < PROVISION_SURFACE_Y)
    {
        e->fs_home |= PROVISION_WOOD_RETRY_BELOW;
        return;
    }
    if (e->fs_home & PROVISION_WOOD_RETRY_BELOW)
        e->fs_home &= (e->fs_home & PROVISION_WOOD_STUCK_FAIL) ? ~PROVISION_WOOD_RETRY_BELOW
                                                               : ~(PROVISION_WOOD_RETRY_BELOW | PROVISION_WOOD_FAIL);
    if ((e->fs_home & PROVISION_WOOD_FAIL) &&
        hypot(r->x - e->fs_hx, r->z - e->fs_hz) >= PROVISION_WOOD_RETRY_DIST)
        e->fs_home &= ~(PROVISION_WOOD_FAIL | PROVISION_WOOD_STUCK_FAIL);
}

/* lane/pdig (counting only): provision_mode_w's *why, the line that decided (neth_vec_lsrc's bits 16 up) */
enum { PVW_NONE, PVW_CRAFT_WOOD, PVW_EXIT, PVW_PILLAR, PVW_BRIDGE_WOOD, PVW_FULL, PVW_LATCH, PVW_HELD, PVW_UNDER,
       PVW_OFF };
static int provision_mode_w(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                            const struct neth_rl_state *s, int *why)
{
    *why = PVW_OFF;
    if (r->dim != 0 || (r->flags & PF_DEAD))
        return PV_OFF;
    /* lane/proom (stronghold_room_start on): latched in the portal room it saw, the stronghold's turn */
    if (MOPT(MO_SH_ROOM_START) && e->sh.rstart && e->sh.go && e->sh.phase != SH_DONE) return PV_OFF;
    /* Once full armor is actually worn, stock food and fetch wood by day; the pearl night belongs to the hunt. */
    if (r->food >= 7 && shore_night_yield(v, e, r, s)) return PV_OFF;
    if (MOPT(MO_FOOD_WOOD_ROUTE) == 2 && !(e->fs_home & PROVISION_WOOD_FAIL) && !provision_wood_hungry(v, r, s) &&
        ((e->fs_home & PROVISION_WOOD_CRAFT_LATCH) || provision_craft_wood_need(v, e, r, s)))
        return *why = PVW_CRAFT_WOOD, PV_SURFACE;
    if (!MOPT(MO_FOOD_PROVISION) || !(v->macros & NETH_VEC_FOOD)) return PV_OFF;
    if (e->fs_home & PROVISION_EXIT) return *why = PVW_EXIT, PV_SURFACE;
    int pillar_top = !(e->fs_home & PROVISION_EXIT_DONE) && provision_pillar_top(v, e, r, s);
    if (pillar_top) return *why = PVW_PILLAR, PV_SURFACE;
    /* lane/pground (pearl_march): at night, fed, with pearls or eyes short, the night is the hunt's (the exits above kept) */
    if (pm_night(v, r, s) && pearl_want(v, r, s)) return PV_OFF;
    if (e->fs_home & PROVISION_DONE) return PV_OFF;
    if (MOPT(MO_FOOD_WOOD_ROUTE) && (e->fs_home & PROVISION_EXIT_DONE) &&
        !(e->fs_home & PROVISION_WOOD_DONE) && st3_tier(v, s) < 1)
        return *why = PVW_BRIDGE_WOOD, PV_SURFACE;
    if (e->fs_home & PROVISION_WOOD_DONE && st3_tier(v, s) < 1) return PV_OFF;
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    /* On a roofed return, the exclusive surface climb can take every turn while the armor has iron and a pickaxe.
     * Keep the emergency pillar exit, then let the armor errand finish before this option stocks End food. */
    if (MOPT(MO_FOOD_PROVISION) == 2 && (e->dims & 1) && (!f || f->armor_worn < PROVISION_ARMOR))
        return PV_OFF;
    /* The stock latch owns every night tick. Once the pearl goal is met, allow
     * the ready powder/eye recipe to finish, then resume the food errand. */
    if (MOPT(MO_FOOD_PROVISION_EYES) && inv_count(s, EYE) < 12 && inv_count(s, PEARL) > 0 &&
        inv_count(s, EYE) + inv_count(s, PEARL) >= 12 &&
        (inv_count(s, BLAZE_POWDER) > 0 || inv_count(s, BLAZE_ROD) > 0))
        return PV_OFF;
    /* A stocked player still needs the furnace in the pack before the controller releases the turn. */
    if (food_points(s) >= PROVISION_FOOD && inv_count(s, FURNACE) > 0)
        return (e->fs_home & PROVISION_LATCH) ? (*why = PVW_FULL, PV_STOCK) : PV_OFF;
    if (e->fs_home & PROVISION_LATCH) return *why = PVW_LATCH, PV_STOCK;
    int held = (int)(inv_count(s, 368) + inv_count(s, EYE));
    if (held >= 12 && provision_sword(s) && f && f->armor_worn >= PROVISION_ARMOR)
        return *why = PVW_HELD, PV_STOCK;
    if ((e->dims & 1) && inv_count(s, BLAZE_ROD) + inv_count(s, BLAZE_POWDER) + inv_count(s, EYE) > 0 &&
        provision_under(v, e, r) && !(MOPT(MO_PROV_UNDER_YIELD) && shore_night_yield(v, e, r, s)))
        return *why = PVW_UNDER, PV_SURFACE;
    return PV_OFF;
}

static int provision_mode(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s)
{
    int why;
    return provision_mode_w(v, e, r, s, &why);
}

static int provision_complete(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                              const struct neth_rl_state *s)
{
    if (food_points(s) < PROVISION_FOOD || inv_count(s, FURNACE) == 0 ||
        r->y < PROVISION_SURFACE_Y || r->screen != PS_NONE) return 0;
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    if (f && !f->census.on) neth_rl_far_census(v->h, 1);
    return f && f->census.on && f->census.pl_sky;
}

/* Bridge from the emergency cobblestone column to a naturally supported standing cell. The target and the column are
 * kept in the food controller's saved walk fields, before any new scout uses them. Only food_oracle=on reads the
 * server's block cells; the ordinary surface climb does not enter this stage. */
static int provision_exit_prime(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                                const struct neth_rl_state *s)
{
    if (e->fs_home & PROVISION_EXIT) return 1;
    /* The wood bridge also places a cobblestone column. Its floor must not start the finished pillar exit again. */
    if (MOPT(MO_FOOD_WOOD_ROUTE) && (e->fs_home & PROVISION_EXIT_DONE)) return 0;
    if (!provision_pillar_top(v, e, r, s)) return 0;
    int id = (int)(e - v->e), ox = (int)floor(r->x), oy = (int)floor(r->y + 0.01), oz = (int)floor(r->z);
    e->fs_home |= PROVISION_EXIT;
    e->fs_hx = ox, e->fs_hz = oz, e->fs_far = oy - 1;
    e->fs_det = 0;
    double best = 1e9;
    for (int dx = -PROVISION_EXIT_R; dx <= PROVISION_EXIT_R; ++dx)
        for (int dz = -PROVISION_EXIT_R; dz <= PROVISION_EXIT_R; ++dz)
            for (int dy = -4; dy <= 3; ++dy)
            {
                double d = hypot((double)dx, (double)dz);
                if (d < PROVISION_EXIT_AWAY) continue;
                int x = ox + dx, y = oy + dy, z = oz + dz;
                int floor_block = shoracle_block(v->p, id, x, y - 1, z, NULL);
                if (floor_block == COBBLE || !shoracle_floor(floor_block) ||
                    !shoracle_open(shoracle_block(v->p, id, x, y, z, NULL)) ||
                    !shoracle_open(shoracle_block(v->p, id, x, y + 1, z, NULL))) continue;
                double score = d + 0.4 * abs(dy);
                if (score < best) best = score, e->fs_sx = x, e->fs_det = y, e->fs_sz = z;
            }
    return 1;
}

static void provision_exit_step(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                                const struct neth_rl_state *s, struct neth_rl_act *a)
{
    int id = (int)(e - v->e), ox = (int)e->fs_hx, oz = (int)e->fs_hz, top = (int)e->fs_far;
    double away = hypot(r->x - (ox + 0.5), r->z - (oz + 0.5));
    int cap = shoracle_block(v->p, id, ox, top + 1, oz, NULL);
    if (cap > 0) e->fs_home |= PROVISION_EXIT_CAP;
    if (!(e->fs_home & PROVISION_EXIT_CAP) && away >= 1.2 && away <= 3.5 && cap == 0)
    {
        still(a);
        a->hold = 1u << PK_SNEAK;
        int hb = sh_hotbar(s, COBBLE);
        if (hb < 0 && inv_count(s, COBBLE) > 0) { sh_fetch(v, e, r, s, a, COBBLE); return; }
        if (hb < 0) return;
        a->hotbar = hb;
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, ox + 0.5, top + 0.99, oz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        if (s->mo_hit && s->mo_x == ox && s->mo_y == top && s->mo_z == oz && s->mo_side == 1 &&
            s->current == hb) a->press[PK_USE] = 1;
        return;
    }
    if (!e->fs_det) { still(a); a->hold = 1u << PK_SNEAK; return; }
    /* The pathfinder sees only placed blocks now. Plan one bridge cell at a time: a path straight to the natural
     * landing cannot use the as-yet-unplaced second and later bridge blocks as their own placement faces. */
    int tx = (int)e->fs_sx, ty = e->fs_det, tz = (int)e->fs_sz;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
    int sx = fx, sz = fz;
    if (shoracle_block(v->p, id, fx, fy - 1, fz, NULL) == 0)
    {
        static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        double best_support = 1e9;
        for (int k = 0; k < 4; ++k)
        {
            int x = fx + D[k][0], z = fz + D[k][1];
            if (shoracle_block(v->p, id, x, fy - 1, z, NULL) != COBBLE) continue;
            double d = hypot(r->x - (x + 0.5), r->z - (z + 0.5));
            if (d < best_support) best_support = d, sx = x, sz = z;
        }
    }
    int wx = tx, wy = ty, wz = tz;
    if (abs(tx - sx) + abs(tz - sz) > 1)
    {
        wy = fy;
        if (abs(tz - sz) >= abs(tx - sx)) wx = sx, wz = sz + (tz > sz ? 1 : -1);
        else wx = sx + (tx > sx ? 1 : -1), wz = sz;
    }
    /* A one-block-wide bridge has only the player's own floor as a placement face. sho_follow intentionally excludes
     * that face, so place the next floor directly while sneaking at this edge, then let it walk onto the new block. */
    if (wy == fy && abs(wx - sx) + abs(wz - sz) == 1 &&
        shoracle_block(v->p, id, wx, fy - 1, wz, NULL) == 0 &&
        shoracle_open(shoracle_block(v->p, id, wx, fy, wz, NULL)) &&
        shoracle_open(shoracle_block(v->p, id, wx, fy + 1, wz, NULL)))
    {
        still(a);
        int hb = sh_hotbar(s, COBBLE);
        if (hb < 0 && inv_count(s, COBBLE) > 0) { sh_fetch(v, e, r, s, a, COBBLE); return; }
        if (hb < 0) return;
        int dx = wx - sx, dz = wz - sz;
        int side = dx > 0 ? 5 : dx < 0 ? 4 : dz > 0 ? 3 : 2;
        double yaw, pitch;
        double along = (r->x - (sx + 0.5)) * dx + (r->z - (sz + 0.5)) * dz;
        if (along < 0.55)
            look_at(r->x, r->y + 1.62, r->z, wx + 0.5, r->y + 1.62, wz + 0.5, &yaw, &pitch);
        else
            look_at(r->x, r->y + 1.62, r->z, sx + 0.5 + 0.5 * dx, fy - 0.5,
                    sz + 0.5 + 0.5 * dz, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        a->hotbar = hb;
        a->hold = 1u << PK_SNEAK;
        if (along < 0.55) a->hold |= 1u << PK_FORWARD;
        if (along >= 0.55 && s->mo_hit && s->mo_x == sx && s->mo_y == fy - 1 && s->mo_z == sz &&
            s->mo_side == side && s->current == hb)
        {
            a->hold = 1u << PK_SNEAK;
            a->press[PK_USE] = 1;
        }
        return;
    }
    int rc = sho_follow(v, e, r, s, a, wx, wy, wz, 0.45, SHO_EXACT | SHO_BRIDGE | SHO_SNEAK_F);
    if (rc == 1 && wx == tx && wy == ty && wz == tz &&
        away >= PROVISION_EXIT_AWAY && (e->fs_home & PROVISION_EXIT_CAP))
    {
        e->fs_home &= ~PROVISION_EXIT;
        e->fs_home |= PROVISION_EXIT_DONE;
    }
}

/* The return's saved pillar fields are free after EXIT_DONE. A privileged scan chooses a tree's standing cell; the
 * acts are a bridged walk, a hand attack on the visible log, and a walk to its drop. */
static int provision_wood_step(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                               const struct neth_rl_state *s, struct neth_rl_act *a)
{
    int craft = MOPT(MO_FOOD_WOOD_ROUTE) == 2 && !provision_wood_hungry(v, r, s) &&
                ((e->fs_home & PROVISION_WOOD_CRAFT_LATCH) || provision_craft_wood_need(v, e, r, s));
    int bridge = (e->fs_home & PROVISION_EXIT_DONE) && st3_tier(v, s) < 1;
    if (!MOPT(MO_FOOD_WOOD_ROUTE) || (!craft && !bridge) || (e->fs_home & PROVISION_WOOD_FAIL))
        return 0;
    const int wsub = craft ? 8 : 0;     /* lane/pdig (counting only): e->lsub's route */
    if (r->screen != PS_NONE)
    {
        still(a);
        a->nops = 1;
        a->ops[0] = CLOSE;
        e->lsub = wsub + VLW_SCREEN;
        return 1;
    }
    if (craft) e->fs_home |= PROVISION_WOOD_CRAFT_LATCH, e->fs_home &= ~PROVISION_WOOD_DONE;
    int routed = !(e->fs_home & PROVISION_WOOD_ROUTE);
    int logs = inv_count(s, LOG) + inv_count(s, LOG2);
    int need = MOPT(MO_FOOD_WOOD_ROUTE) == 2 ? PROVISION_CRAFT_LOGS : 1;
    if (logs >= need)
    {
        e->fs_home |= PROVISION_WOOD_DONE;
        e->fs_home &= ~(PROVISION_WOOD_ROUTE | PROVISION_WOOD_CRAFT_LATCH);
        return 0;
    }
    int id = (int)(e - v->e);
    if (logs > 0 && (e->fs_home & PROVISION_WOOD_ROUTE))
    {
        int old = shoracle_block(v->p, id, (int)e->fs_hx, (int)e->fs_far, (int)e->fs_hz, NULL);
        if (old != LOG && old != LOG2) e->fs_home &= ~PROVISION_WOOD_ROUTE;
    }
    if (!(e->fs_home & PROVISION_WOOD_ROUTE))
    {
        int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.01), fz = (int)floor(r->z);
        double best = 1e30;
        if (MOPT(MO_FOOD_WOOD_RETRY))
        {
            int nearest[6];
            if (shoracle_loaded_log_stand(v->p, id, fx, fy, fz, PROVISION_WOOD_Y_DOWN,
                                          PROVISION_WOOD_Y_UP, nearest) == 0)
            {
                best = 0;
                e->fs_hx = nearest[0], e->fs_far = nearest[1], e->fs_hz = nearest[2];
                e->fs_sx = nearest[3], e->fs_det = nearest[4], e->fs_sz = nearest[5];
            }
        }
        else
        {
        static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int y = fy - PROVISION_WOOD_Y_DOWN; y <= fy + PROVISION_WOOD_Y_UP; ++y)
            for (int dz = -PROVISION_WOOD_RADIUS; dz <= PROVISION_WOOD_RADIUS; ++dz)
                for (int dx = -PROVISION_WOOD_RADIUS; dx <= PROVISION_WOOD_RADIUS; ++dx)
                {
                    int x = fx + dx, z = fz + dz;
                    int b = shoracle_block(v->p, id, x, y, z, NULL);
                    if (b != LOG && b != LOG2) continue;
                    for (int k = 0; k < 4; ++k)
                    {
                        int sx = x + D[k][0], sz = z + D[k][1];
                        if (!shoracle_floor(shoracle_block(v->p, id, sx, y - 1, sz, NULL)) ||
                            !shoracle_open(shoracle_block(v->p, id, sx, y, sz, NULL)) ||
                            !shoracle_open(shoracle_block(v->p, id, sx, y + 1, sz, NULL))) continue;
                        double score = (double)dx * dx + (double)dz * dz + 4.0 * (double)(y - fy) * (y - fy);
                        if (score < best)
                        {
                            best = score;
                            e->fs_hx = x, e->fs_far = y, e->fs_hz = z;
                            e->fs_sx = sx, e->fs_det = y, e->fs_sz = sz;
                        }
                    }
                }
        }
        if (best == 1e30)
        {
            e->fs_home |= PROVISION_WOOD_FAIL;
            if (MOPT(MO_FOOD_WOOD_RETRY)) e->fs_hx = r->x, e->fs_hz = r->z;
            return 0;
        }
        e->fs_home |= PROVISION_WOOD_ROUTE;
        e->fs_legt = e->steps;
        /* lane/proom (food_wood_stuck on): the food scout's move tracker (fs_mx, fs_mz, fs_mt; the scout's begin sets
         * it afresh) times the route's stand */
        if (MOPT(MO_FOOD_WOOD_STUCK)) e->fs_mx = r->x, e->fs_mz = r->z, e->fs_mt = e->steps;
    }
    if (MOPT(MO_FOOD_WOOD_STUCK))
    {
        if (py_hypot2(r->x - e->fs_mx, r->z - e->fs_mz) >= 1.0) e->fs_mx = r->x, e->fs_mz = r->z, e->fs_mt = e->steps;
        /* stood still that long: the cap's give-up below, now */
        if (e->steps - e->fs_mt > PROVISION_WOOD_STUCK)
            e->fs_legt = e->steps - PROVISION_WOOD_MAX - 1, e->fs_home |= PROVISION_WOOD_STUCK_FAIL;
    }
    if (e->steps - e->fs_legt > PROVISION_WOOD_MAX)
    {
        e->fs_home &= ~PROVISION_WOOD_ROUTE;
        e->fs_home |= PROVISION_WOOD_FAIL;
        if (MOPT(MO_FOOD_WOOD_RETRY)) e->fs_hx = r->x, e->fs_hz = r->z;
        return 0;
    }
    int lx = (int)e->fs_hx, ly = (int)e->fs_far, lz = (int)e->fs_hz;
    int b = shoracle_block(v->p, id, lx, ly, lz, NULL);
    int tx = (b == LOG || b == LOG2) ? (int)e->fs_sx : lx;
    int ty = (b == LOG || b == LOG2) ? e->fs_det : ly;
    int tz = (b == LOG || b == LOG2) ? (int)e->fs_sz : lz;
    int rc = sho_follow(v, e, r, s, a, tx, ty, tz, 0.55, SHO_EXACT | SHO_BRIDGE | SHO_SNEAK_F);
    if (rc < 0)
    {
        e->fs_home &= ~PROVISION_WOOD_ROUTE;
        e->fs_home |= PROVISION_WOOD_FAIL;
        if (MOPT(MO_FOOD_WOOD_RETRY)) e->fs_hx = r->x, e->fs_hz = r->z;
        return 0;
    }
    if (rc == 1 && (b == LOG || b == LOG2))
    {
        double yaw, pitch;
        still(a);
        look_at(r->x, r->y + 1.62, r->z, lx + 0.5, ly + 0.5, lz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        /* A branch's log can stand between the eye and the chosen trunk log. Break that visible log first. */
        if (s->mo_hit && (s->mo_block == LOG || s->mo_block == LOG2) &&
            abs(s->mo_x - lx) <= PROVISION_WOOD_ATTACK_R && abs(s->mo_z - lz) <= PROVISION_WOOD_ATTACK_R &&
            abs(s->mo_y - ly) <= PROVISION_WOOD_ATTACK_Y)
            a->hold = 1u << PK_ATTACK;
    }
    e->lsub = wsub + (routed ? VLW_ROUTE
                      : rc == 1 && (b == LOG || b == LOG2) ? ((a->hold & (1u << PK_ATTACK)) ? VLW_CHOP : VLW_AIM)
                      : (a->hold & (1u << PK_ATTACK)) ? VLW_DIG : VLW_GO);
    return 1;
}

static int provision_food_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                               const struct neth_rl_state *s)
{
    if (food_points(s) >= PROVISION_FOOD && inv_count(s, FURNACE) == 0 && near_furnace(e, r))
        return C_TAKE;
    int raw = (int)raw_meat(s), furnace = inv_count(s, FURNACE) > 0 || near_furnace(e, r);
    int fuel = inv_count(s, COAL) > 0 || inv_count(s, PLANKS) >= 2;
    int logs = inv_count(s, LOG) + inv_count(s, LOG2);
    if ((raw >= FOOD_COOK_N || food_points(s) >= PROVISION_FOOD) && !furnace && inv_count(s, COBBLE) >= 8)
    {
        if (inv_count(s, TABLE) > 0 || near_table(e, r)) return C_FURNACE;
        if (inv_count(s, PLANKS) >= 4) return C_TABLE;
        if (logs > 0) return C_PLANKS;
    }
    if (raw >= FOOD_COOK_N && furnace)
    {
        if (fuel) return C_SMELT;
        if (logs > 0) return C_PLANKS;
    }
    if (furnace_take_want(v, e, r, s)) return C_TAKE;
    return -1;
}

/* Two scouts that timed out with no animal are followed by a dry 300-step leg, then their cooldown is cleared. The
 * walk and its count reuse the food controller's home and leg fields, unused by food_scout=walk. */
static int provision_roam(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a)
{
    if (!(e->fs_home & PROVISION_ROAM) && e->fb.scout_end[1] >= PROVISION_SCOUT_FAILS * (e->fs_hn + 1))
    {
        e->fs_hn += 1;
        e->fs_home |= PROVISION_ROAM;
        e->fs_legt = e->steps;
        e->fs_yaw = r->yaw + 90.0 * (e->fs_hn & 3);
    }
    if (!(e->fs_home & PROVISION_ROAM)) return 0;
    if (!food_day(r)) { e->fs_legt += 1; return 0; }
    if (e->steps - e->fs_legt >= PROVISION_ROAM_STEPS)
    {
        e->fs_home &= ~PROVISION_ROAM;
        e->fs_off = e->hunt_off = 0;
        return 0;
    }
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    int x, y, z;
    if (MOPT(MO_FOOD_ORACLE) && f && f->census.on && f->census.food_n > 0)
        x = (int)floor(f->census.food_x), y = (int)floor(f->census.food_y), z = (int)floor(f->census.food_z);
    else if (f && f->census.on && f->census.hi_d > 8)
        x = (int)floor(f->census.hi_x), y = (int)floor(f->census.hi_y), z = (int)floor(f->census.hi_z);
    else
    {
        x = (int)floor(r->x - sin(e->fs_yaw * M_PI / 180.0) * 24.0);
        y = (int)floor(r->y);
        z = (int)floor(r->z + cos(e->fs_yaw * M_PI / 180.0) * 24.0);
    }
    int walked = walk_to(v, e, r, s, a, &e->fo_wt, x, y, z);
    if (walked != 0)
    {
        e->fs_home &= ~PROVISION_ROAM;
        e->fs_off = e->hunt_off = 0;
    }
    return 1;
}
