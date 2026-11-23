/* lane/dragon: the End fight (vecdragon.h, vecenv.h NETH_VEC_DRAGON) */
#include "vecdragon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pool.h"
#include "../engine/chatmsg.h"
#include "../engine/living.h"

/* ---- the constants the macro is tuned by ---- */
#define DG_SHOOT_RANGE 90.0     /* a crystal in view is shot from at most this far (blocks, eye to its middle) */
#define DG_SNIPE_TOWERS 14      /* the snipe's tower: at most this many blocks */
#define DG_RESNIPES 3           /* snipes started by the dragon's healing seen (a crystal left that no look around
                                 * from the ground shows: a pillar's own top edge hides its crystal from below) */
#define DG_SNIPE_DUCK 20.0      /* such a snipe's tower dug down when the dragon's head comes this near */
#define DG_HEAL 0.5f            /* the boss bar's rise between two sightings that counts as healing */
#define DG_AT_DRAGON (-2)       /* the shot's target: the dragon (else a crystal's entity id) */
#ifndef DG_DRAGON_RANGE
#define DG_DRAGON_RANGE 40.0    /* the bow drawn at the dragon's head within this many blocks */
#endif
#ifndef DG_RELEASE
#define DG_RELEASE 30.0         /* lane/dragon2: the full draw let go once the head comes this near (the arrows ran out
                                 * by minute 7 let go at 40, where a body hit takes a quarter; 12 was its choice).
                                 * lane/dragon3, the crystals gone first: kills at 12, 20 and 30, 3, 8 and 11 of 128
                                 * (seeds 1 to 4); at 40, 6 of 96 (seeds 1 to 3) */
#endif
#ifndef DG_RELEASE_FAR
#define DG_RELEASE_FAR 20.0     /* ... or once it turns away within this */
#endif
#ifndef DG_HOLD_MAX
#define DG_HOLD_MAX 400         /* ... or after this many steps drawn */
#endif
#define DG_HOLD_LOST 60         /* ... or this many out of view */
/* lane/macroopts: the three either/ors below are run-file options (vecenv.c MOPTS: dragon_crit, dragon_block,
 * dragon_lob), read off the step's input */
#define DG_CRIT (in->crit)      /* lane/dragon2: the sword's swing on a jump's way down (1.5x) */
#define DG_CRIT_WAIT 30         /* ... or a swing anyway after this many steps */
#define DG_BLOCK (in->block)    /* lane/dragon2: the sword held blocking between swings while the head is near */
#define DG_BLOCK_R 16.0         /* ... and while it is in view this near, out of the bow's turn */
#ifndef DG_GUARD
#define DG_GUARD 5.0            /* ... or at once when the head is this near (the hit first: a hurt dragon bites nothing
                                 * for 10 ticks, EntityDragon's hurtTime gate) */
#endif
#define DG_DRAGON_FLIGHT 25     /* steps an arrow at the dragon is watched */
#define DG_DRAGON_GAP 5         /* steps between arrows at the dragon at least */
#ifndef DG_ARROW_KEEP
#define DG_ARROW_KEEP 6         /* arrows kept for crystals seen later (none of them at the dragon) while the client
                                 * holds a crystal copy; every arrow at the dragon once it holds none */
#endif
#define DG_DRAW 22              /* steps the bow is drawn before the release (20 is full power: ItemBow's charge) */
#define DG_FLIGHT 40            /* steps a shot is watched for its crystal's end */
#define DG_TRIES 3              /* shots at one crystal before it is left alone */
#define DG_SKIP 600             /* steps a crystal left alone stays so */
#ifndef DG_MELEE
#define DG_MELEE 9.0            /* the dragon's head within this many blocks of the eye: the sword in hand, the look on it */
#endif
#define DG_SWING 10             /* steps between swings (the dragon's hurt resistance takes a hit each 10 ticks) */
#define DG_SWEEP 24             /* a look around: this many steps of DG_SWEEP_DEG each */
#define DG_SWEEP_DEG 15.0
#define DG_SWEEP_PITCH (-25.0)  /* the look around's pitch: the pillars' tops */
#define DG_WALK 60              /* steps walked toward the waypoint between looks around */
#define DG_RING 36.0            /* the waypoints past the centre: a ring this far out, eight of them */
#define DG_NEAR 6.0             /* a waypoint within this many blocks is reached */
#define DG_TOWERS 16            /* tower steps the walk takes toward one waypoint at most */
#ifndef DG_WALK_RISE
#define DG_WALK_RISE 2          /* lane/dragon2: rises in a row without a block's progress before the walk turns aside
                                 * (lane/dragon's walk towered up to DG_TOWERS beside a pillar, then walked off: 9 of
                                 * seed 1's 29 End deaths, a 23-block fall at one place) */
#endif
#define DG_DETOUR 40            /* lane/dragon2: steps the walk turns aside */
/* lane/dragon2: the hunt for a crystal seen and not seen to blow up, from a stand DG_HUNT_D from it toward the centre */
#ifndef DG_HUNT_D
#define DG_HUNT_D 56.0          /* the sight line to 0.8 over a crystal's middle clears its pillar's edge (bedrock 1 under
                                 * the middle) w out from about 14 w blocks: 42 saw a 3-out edge's crystal at best */
#endif
#ifndef DG_HUNTS
#define DG_HUNTS 3              /* hunts a crystal at most (each from another stand) */
#endif
#ifndef DG_HUNT_R
#define DG_HUNT_R 110.0         /* crystals within this of the island's centre (the dragon circles within 60 of it and
                                 * heals from a crystal within 32 of its box; lane/dragon3: 80 before, and the server's
                                 * healing crystal stood 81 to 106 out on 56% of its healing ticks, dragon_case's books) */
#endif
#define DG_HUNT_STEPS 1500      /* a hunt given up after this many steps */
#define DG_HUNT_LOOK 40         /* steps the look rests on the crystal's place at the stand */
#define DG_HUNT_TURN 35.0       /* degrees a retry's stand is turned about the crystal */
/* lane/dragon3: the lob. A crystal on a pillar is hidden from below by the pillar's own top edge, but an arrow that
 * comes down onto it clears that edge: the high arc at the place it was seen, from a stand near it. An entity moves
 * only while the chunks within 32 blocks of it are loaded (World.updateEntityWithOptionalForce), and the server loads
 * the render distance's chunks around the player's (4 here), so an arrow keeps flying only within 2 chunks of the
 * player's chunk: the stand is near enough for the whole flight to be */
#define DG_LOB (in->lob)        /* 0: lane/dragon2's hunt (the stand DG_HUNT_D off, the low arc only, in view only) */
#ifndef DG_LOB_STAND
#define DG_LOB_STAND 20.0       /* the stand this far from the crystal toward the island's centre (the walk stops within
                                 * DG_NEAR of it; out of the blast's reach: an explosion of 6 hurts within 12 blocks) */
#endif
#define DG_LOB_MIN 12.0         /* a lob from at least this far (horizontally: nearer, the arc is too steep to land) */
#define DG_LOB_MAX 30.0         /* ... and at most this far */
#define DG_LOB_CHUNKS 2         /* the crystal's chunk at most this many from the player's on each axis */
#ifndef DG_LOB_FLIGHT
#define DG_LOB_FLIGHT 105       /* steps a lob is watched for its crystal's end (its flight is 85 to 95 ticks) */
#endif
#define DG_LOB_LOOK 12        /* steps the look rests on the crystal's place at the stand before a lob (in view: the
                                 * low arc, the straight one, instead) */
#ifndef DG_LOST
#define DG_LOST 600             /* lane/dragon2: steps the dragon is out of view before the hole is left to follow it */
#endif
#define DG_FOLLOW_R 40.0        /* ... toward its last place, kept this near the centre */
#define DG_FOLLOW_FAR 56.0      /* lane/dleg (dragon_follow server): ... toward the server's dragon, kept this near */
#ifndef DG_CRYSTAL_SHOTS
#define DG_CRYSTAL_SHOTS 9     /* arrows at one crystal at most, an episode */
#endif
#define DG_MEM_GONE 56.0       /* a crystal kept is dropped when its copy goes within this many blocks (rd 4's chunks) */
#define DG_DETOUR_DEG 80.0      /* ... this far off the waypoint's heading, the side alternating */
#define DG_FETCHES 6            /* fetches from the main inventory to the hotbar an episode at most */
#define DG_BOX_TRY 16           /* steps a side of the box is tried */
#ifndef DG_PIT_R
#define DG_PIT_R 20.0           /* the box is made within this many blocks of the island's centre (0, 0): at render
                                 * distance 4 only there are the dragon's circle's chunks loaded, so only there it flies */
#endif
#define DG_PIT_WALK_STEPS 4000  /* the walk in given up after this many steps (the box made where it stands) */
#define DG_PIT_STEPS 4200       /* the box given up after this many steps */
#define DG_DIG_STALL 14         /* steps without progress before the walk digs what its look meets */
#define DG_DUCK 40.0            /* the dragon's head in view this near during the walk: the box made where it stands */
#define DG_UNBOX 400            /* steps the dragon is out of view before a box short of the centre is left */
#ifndef DG_HOLE_DEPTH
#define DG_HOLE_DEPTH 1         /* lane/dragon2: the box is a hole this deep dug in the end stone (0: the cobblestone
                                 * box lane/dragon placed) */
#endif
#define DG_VIEW_TOWERS (DG_HOLE_DEPTH > 0 ? 0 : 2)   /* blocks the box is raised for the view at most (none from the
                                                     * hole: a block up is out of it) */
#define DG_ENDER_DEG 8.0        /* the crosshair kept this far from every enderman in view (their aggro cone is under
                                 * 6 degrees past 2 blocks: EntityEnderman.shouldAttackPlayer, 1 - 0.025 / d) */
#define DG_EAT_FOOD 14          /* food eaten at this level or under, or under 20 with health at DG_EAT_HEALTH or under */
#define DG_EAT_HEALTH 12.0f
#ifndef DG_EAT_REGEN
#define DG_EAT_REGEN 18         /* lane/dragon2: hurt at all and the food under this: eaten (FoodStats heals only at 18
                                 * and over: from 17 down to 15 a hurt player healed nothing) */
#endif
#define DG_EAT_STEPS 40         /* the use held this long (eating is 32 ticks) */
#define DG_CLIMB_Y 56.0         /* the arrival's climb ends with the open sky over the feet, or past this height */
#define DG_EXIT_STALL 8         /* the exit walk's steps without moving before a tower step up toward the portal */
#define DG_EXIT_TOWER_R 6.0     /* ... within this many blocks of its cell */
#define DG_EXIT_RIM 64.9        /* ... while the feet are under the rim's top (65) */
#define DG_EXIT_OUT 3.6         /* the tower's cell: its middle past this from the portal's (the rim's are within 3.5) */
#define DG_EXIT_CLEAR 6.0       /* lane/dragon2: the dying dragon kept this far (the portal's rim reaches 3.5 from it) */
#define DG_COL_LOOK 72         /* lane/dragon2: steps the exit turns looking for the portal's column (three turns) */
#define DG_EXIT_WAIT 210     /* steps from the death seen until the portal stands (onDeathUpdate's 200 ticks) */
#define DG_EXIT_STUCK 60        /* lane/dragon3: steps without moving under the rim's top before the walk out */
#define DG_EXIT_OUT_WALK 9.0    /* ... this far from the portal's middle (its rim reaches 3.5) */
/* the books' only (lane/dragon2): the wing push seen as a step's horizontal move past this (walking is under 0.3, the
 * push adds 4 a tick: EntityDragon.collideWithEntities), and a death within this many steps of one counts as pushed */
#define DG_PUSH_V 1.0
#define DG_PUSH_WINDOW 100

/* EntityArrow's flight: ItemBow's full draw (2 * 1.5 blocks a tick), its gravity and drag a tick */
#define ARROW_V 3.0
#define ARROW_G 0.05
#define ARROW_DRAG 0.99
/* lane/shold: EntitySnowball's (EntityThrowable: func_70182_d 1.5 a tick, getGravityVelocity 0.03, the same drag and
 * the same order: moved, then the drag, then the gravity) */
#define SNOW_V 1.5
#define SNOW_G 0.03
#define DG_SNOW_TOWERS 16       /* lane/shold: blocks a hunt's stand rises at most for a snowball's arc */

enum { BOW = 261, ARROW = 262, SNOWBALL = 332, COBBLE = 4, END_STONE = 121, OBSIDIAN = 49, BEDROCK = 7 };
enum { BUCKET = 325, WATER_B = 326 };
/* lane/dleg (dragon_wait centre, in->hunt_opts bit 6): the box (the pit, where the bot waits for the charge) made within
 * DG_WAIT_R of the centre instead of DG_PIT_R, and a box outside that left for the centre: a throw from there lands on
 * the island (the dragon's charge carries the body 20-50 blocks; from the pit's ring at r 20 it went over the rim) */
#define DG_WAIT_R 8.0           /* (walk_to's DG_NEAR 6 from (0.5, 0.5) is within it) */
#define DG_BOX_R ((in->hunt_opts & (64 | 128)) ? DG_WAIT_R : DG_PIT_R)
/* lane/dleg (dragon_wait band, bit 7): the wait point DG_BAND_R out from the centre toward the island's widest side
 * (bits 29-31 its eighth of the circle, dragon.c's from the server's End blocks): the box within DG_WAIT_R of it. On
 * dw's rows the charges at r 8-30 dealt 2.56 for each health lost against the centre's 2.23 */
#define DG_BAND_R 16.0
static void wait_point(const struct dragon_in *in, double *wx, double *wz)
{
    static const double C8[8] = {1.0, 0.70710678118654752, 0.0, -0.70710678118654752, -1.0, -0.70710678118654752, 0.0,
                                 0.70710678118654752};
    *wx = *wz = 0.5;
    if (!(in->hunt_opts & 128)) return;
    int k = (int)(((uint32_t)in->hunt_opts >> 29) & 7u);
    *wx += DG_BAND_R * C8[k], *wz += DG_BAND_R * C8[(k + 6) & 7];
}
/* the squared distance the box is measured by: to the wait point with dragon_wait band, else to the centre (0, 0) */
static double wait_d2(const struct dragon_in *in)
{
    if (!(in->hunt_opts & 128)) return in->x * in->x + in->z * in->z;
    double wx, wz;
    wait_point(in, &wx, &wz);
    return (in->x - wx) * (in->x - wx) + (in->z - wz) * (in->z - wz);
}
/* lane/dleg (dragon_late, in->hunt_opts bits 24 nohunt, 25 nopit, 26 walkin): once the client holds no crystal copy
 * (the tracker sends every crystal of the island; the dragon's copy held, so the island's chunks are loaded) and past
 * the arrival's first DG_LATE_STEPS: no hunt (24), no hole out past DG_WAIT_R (25: the duck and the hunt's hole), and
 * the wait for the pass out there a walk in to the centre (26). After the crystals, 31% of the steps were at r 30+
 * (seek 58% of them, pits 18%, hunts for crystals already gone 11%) and 63 void deaths came there */
#define DG_LATE_STEPS 600
#define DG_LATE(in, en, bit) (((in)->hunt_opts & (bit)) && (en)->held == 0 && (en)->dragon_id && \
                              (in)->steps > DG_LATE_STEPS)
#define DG_NOHUNT (1 << 24)
#define DG_NOPIT (1 << 25)
#define DG_WALKIN (1 << 26)
#define DG_OUT(in) ((in)->x * (in)->x + (in)->z * (in)->z > DG_WAIT_R * DG_WAIT_R)
/* lane/dleg (dragon_pit): once, an entry into the hole it dug last (its cell, the feet at or under its floor) digs
 * nothing; stand, no hole at all (the pit's walk in kept, the duck stands on the floor, blocking in the seek) */
#define DG_PIT_ONCE (1 << 27)
#define DG_PIT_STAND (1 << 28)
/* lane/dleg (dragon_clutch water, in->hunt_opts bit 1) */
#define DG_CLUTCH (in->hunt_opts & 2)
#define DG_CLUTCH_FALL 4.0      /* a fall caught once the feet are this far under their highest since the ground (a
                                 * tower's jump falls about 1.25, a fall of 3 does no damage) */
#define DG_CLUTCH_UP 24         /* steps on the ground after a catch that the water is taken up again at most */
#define DG_CLUTCH_UP_EVERY 5    /* ... the use pressed every this many (the right click's delay is 4 ticks) */
static const int16_t SWORDS[] = {276, 267, 272, 268, 283};
static const int16_t PICKS[] = {278, 257, 274, 270, 285};
/* the best first; rotten flesh last (4 food, its hunger's 30 s nearly as much again: a last resort) */
static const int16_t FOODS[] = {364, 320, 366, 297, 393, 350, 260, 391, 363, 319, 365, 360, 357, 282, 400, 367};

static double deg(double r) { return r * (180.0 / 3.14159265358979323846); }
static double rad(double d) { return d * (3.14159265358979323846 / 180.0); }

/* vecenv.c's look_at: Minecraft's yaw and pitch from the eye to a point */
static void look_at(double ex, double ey, double ez, double x, double y, double z, double *yaw, double *pitch)
{
    double dx = x - ex, dy = y - ey, dz = z - ez;
    *yaw = deg(atan2(dz, dx)) - 90.0;
    double p = -deg(atan2(dy, sqrt(dx * dx + dz * dz)));
    *pitch = p > 90.0 ? 90.0 : p < -90.0 ? -90.0 : p;
}

static void look(struct neth_rl_act *a, double yaw, double pitch)
{
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)(pitch > 90.0 ? 90.0 : pitch < -90.0 ? -90.0 : pitch);
}

/* the first hotbar slot holding one of ids (in their order), -1 none */
static int hot(const struct neth_rl_state *s, const int16_t *ids, int n)
{
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < 9; ++i)
            if (s->inv[i].item == ids[j] && s->inv[i].count > 0) return i;
    return -1;
}

static int hot1(const struct neth_rl_state *s, int id)
{
    int16_t one = (int16_t)id;
    return hot(s, &one, 1);
}

static int count(const struct neth_rl_state *s, int id)
{
    int c = 0;
    for (int i = 0; i < 36; ++i)
        if (s->inv[i].item == id && s->inv[i].count > 0) c += s->inv[i].count;
    return c;
}

/* the slot in hand: 1 when it is (a step to switch otherwise) */
static int hold_slot(const struct neth_rl_state *s, struct neth_rl_act *a, int slot)
{
    if (slot < 0) return 0;
    if (s->current == slot) return 1;
    a->hotbar = slot;
    return 0;
}

/* lane/dragon2: a block to tower with in the hotbar: cobblestone, else the end stone the holes gave (-1 none); and how
 * many the inventory holds */
static int hot_block(const struct neth_rl_state *s)
{
    int k = hot1(s, COBBLE);
    return k >= 0 ? k : hot1(s, END_STONE);
}

static int blocks(const struct neth_rl_state *s) { return count(s, COBBLE) + count(s, END_STONE); }

/* lane/shold (dragon_crystals snowball): the crystals' shots thrown snowballs instead of arrows (any projectile ends a
 * crystal: EntityEnderCrystal.attackEntityFrom whatever the amount; serverreplay.c sr_end_other_hit), never at the
 * dragon; the shooter's item and what it fires */
static int shot_item(const struct dragon_in *in) { return in->snow ? SNOWBALL : BOW; }
static int can_shoot(const struct dragon_in *in, const struct neth_rl_state *s)
{
    return in->snow ? hot1(s, SNOWBALL) >= 0 : hot1(s, BOW) >= 0 && count(s, ARROW) > 0;
}

/* the bow (any item) in the main inventory, not the hotbar: its slot 9..35, -1 none */
static int main_slot(const struct neth_rl_state *s, int id)
{
    for (int i = 9; i < 36; ++i)
        if (s->inv[i].item == id && s->inv[i].count > 0) return i;
    return -1;
}

static int main_sword(const struct neth_rl_state *s)
{
    for (size_t j = 0; j < sizeof SWORDS / sizeof *SWORDS; ++j)
        if (main_slot(s, SWORDS[j]) >= 0) return SWORDS[j];
    return 0;
}

/* the item swapped from the main inventory into the hotbar (the inventory key, a number key's click on its slot,
 * mode 2, as fetch_item in vecenv.c): into an empty hotbar slot, else the first that holds no sword, pickaxe, food
 * or cobblestone. 1 when over */
static int fetch(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a,
                 int id)
{
    int src = main_slot(s, id);
    if (hot1(s, id) >= 0 || src < 0 || d->age > 40) return 1;
    if (in->screen == PS_INVENTORY)
    {
        int to = -1;
        for (int i = 0; i < 9 && to < 0; ++i)
            if (s->inv[i].item < 0 || s->inv[i].count <= 0) to = i;
        for (int i = 8; i >= 0 && to < 0; --i)
        {
            /* lane/dleg: with dragon_clutch the snowballs kept too (the water bucket's fetch took their slot, and their
             * fetch again ran DG_FETCHES out before the food's: endrank's 19 hurt deaths with food in the pack) */
            int it = s->inv[i].item, keep = it == COBBLE || it == BOW ||
                                            (DG_CLUTCH && (it == WATER_B || it == BUCKET || it == SNOWBALL));
            for (size_t j = 0; j < 5; ++j) keep |= it == SWORDS[j] || it == PICKS[j];
            for (size_t j = 0; j < sizeof FOODS / sizeof FOODS[0]; ++j) keep |= it == FOODS[j];
            if (!keep) to = i;
        }
        if (to < 0) to = 8;
        a->nops = 2;
        a->ops[0] = (struct neth_rl_op){PG_CLICK, -1, src, to, 2, 0};
        a->ops[1] = (struct neth_rl_op){PG_CLOSE, 0, 0, 0, 0, 0};
        return 0;
    }
    if (!d->fetch_pressed)
    {
        a->press[PK_INVENTORY] = 1;
        d->fetch_pressed = 1;
    }
    return 0;
}

/* the arrow's height at horizontal distance D after a release pitched up th (radians), less H; -1e9 short of D */
static double arc_miss(double D, double H, double th, int snow)
{
    double v0 = snow ? SNOW_V : ARROW_V, g = snow ? SNOW_G : ARROW_G;
    double h = 0.0, y = 0.0, vh = v0 * cos(th), vy = v0 * sin(th);
    for (int t = 0; t < 200; ++t)
    {
        double nh = h + vh, ny = y + vy;
        if (nh >= D) return y + (D - h) / (nh - h) * (ny - y) - H;
        h = nh, y = ny;
        vh *= ARROW_DRAG;
        vy = vy * ARROW_DRAG - g;
        if (vh < 1e-3) break;
    }
    return -1e9;
}

/* the low arc's pitch up (radians) that meets (D, H) from the release point, by bisection; 0 out of range */
static int arc(double D, double H, double *th, int snow)
{
    /* the low arc: the first pitch up from -69 degrees, in 3-degree steps, that reaches (D, H), then bisection */
    double lo = -1.2, hi = -1.2;
    while (hi < 1.45 && arc_miss(D, H, hi, snow) < 0.0) lo = hi, hi += 0.05;
    if (hi >= 1.45) return 0;
    for (int i = 0; i < 40; ++i)
    {
        double m = 0.5 * (lo + hi);
        if (arc_miss(D, H, m, snow) < 0.0) lo = m;
        else hi = m;
    }
    *th = 0.5 * (lo + hi);
    return 1;
}

/* lane/dragon3: the high arc's pitch up that meets (D, H) on its way down: from nearly straight up (89.7 degrees: the
 * drag stops the arrow's horizontal flight short of D) down, the first pitch whose flight reaches D at or over H, then
 * bisection against the steeper one; 0 none. From 13 to 30 blocks it comes down 9 to 17 blocks a block across, and
 * ItemBow's spread (0.0075 a component) moves where it lands by about 1.1 to 1.5 blocks along the line (the crystal's
 * box, grown 0.3 for the arrow, is 2.6 wide) */
static int arc_high(double D, double H, double *th, int snow)
{
    double hi = 1.565, lo = 1.565;
    while (lo > 0.3 && arc_miss(D, H, lo, snow) < 0.0) hi = lo, lo -= 0.05;
    if (lo <= 0.3) return 0;
    for (int i = 0; i < 40; ++i)
    {
        double m = 0.5 * (lo + hi);
        if (arc_miss(D, H, m, snow) < 0.0) hi = m;
        else lo = m;
    }
    *th = 0.5 * (lo + hi);
    return 1;
}

/* the look at a point with the bow's arc (EntityArrow's release point: the eye less 0.1); high: the arc down onto it */
static int aim_arc(const struct dragon_in *in, double x, double y, double z, int high, double *yaw, double *pitch)
{
    double ex = in->x, ey = in->y + 1.62 - 0.1, ez = in->z;
    double dx = x - ex, dz = z - ez, D = sqrt(dx * dx + dz * dz), th;
    if (!(high ? arc_high(D, y - ey, &th, in->snow) : arc(D, y - ey, &th, in->snow))) return 0;
    *yaw = deg(atan2(dz, dx)) - 90.0;
    *pitch = -deg(th);
    return 1;
}

/* lane/dragon3: (x, z)'s chunk within DG_LOB_CHUNKS of the player's on both axes (an arrow there keeps flying) */
static int chunk_near(const struct dragon_in *in, double x, double z)
{
    int cx = (int)floor(x) >> 4, cz = (int)floor(z) >> 4, px = (int)floor(in->x) >> 4, pz = (int)floor(in->z) >> 4;
    return abs(cx - px) <= DG_LOB_CHUNKS && abs(cz - pz) <= DG_LOB_CHUNKS;
}

/* lane/dragon3: a lob from here at (x, z) keeps flying: within DG_LOB_MIN..DG_LOB_MAX and DG_LOB_CHUNKS chunks */
static int lob_ok(const struct dragon_in *in, double x, double z)
{
    double dx = x - in->x, dz = z - in->z, D = sqrt(dx * dx + dz * dz);
    return D >= DG_LOB_MIN && D <= DG_LOB_MAX && chunk_near(in, x, z);
}

static int aim_arrow(const struct dragon_in *in, double x, double y, double z, double *yaw, double *pitch)
{
    double ex = in->x, ey = in->y + 1.62 - 0.1, ez = in->z;
    double dx = x - ex, dz = z - ez, D = sqrt(dx * dx + dz * dz), th;
    if (!arc(D, y - ey, &th, in->snow)) return 0;
    *yaw = deg(atan2(dz, dx)) - 90.0;
    *pitch = -deg(th);
    return 1;
}

/* the angle (degrees) between a look and the line from the eye to a point */
static double off_look(const struct dragon_in *in, double yaw, double pitch, double x, double y, double z)
{
    double yr = rad(yaw), pr = rad(pitch);
    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
    double dx = x - in->x, dy = y - (in->y + 1.62), dz = z - in->z, d = sqrt(dx * dx + dy * dy + dz * dz);
    if (d < 1e-6) return 0.0;
    double c = (lx * dx + ly * dy + lz * dz) / d;
    return deg(acos(c > 1.0 ? 1.0 : c < -1.0 ? -1.0 : c));
}

/* an enderman in view within DG_ENDER_DEG of the look */
static int ender_in_look(const struct dragon_in *in, const struct neth_rl_end *en, double yaw, double pitch)
{
    for (int k = 0; k < en->nender; ++k)
        if (off_look(in, yaw, pitch, en->ender[k].x, en->ender[k].y, en->ender[k].z) < DG_ENDER_DEG) return 1;
    return 0;
}

static int skipped(const struct vec_dragon *d, int id, int64_t now)
{
    /* lane/dragon2: a crystal shot DG_CRYSTAL_SHOTS times is left for good (its arrows meet its pillar's edge) */
    for (int j = 0; j < d->ncm; ++j)
        if (d->cm[j].id == id && d->cm[j].shots >= DG_CRYSTAL_SHOTS) return 1;
    for (int k = 0; k < d->nskip; ++k)
        if (d->skip[k].id == id && d->skip[k].until > now) return 1;
    return 0;
}

static void skip(struct vec_dragon *d, int id, int64_t until)
{
    int k = 0;
    while (k < d->nskip && d->skip[k].id != id && d->skip[k].until > 0) ++k;
    if (k == 8) k = (int)(until % 8);
    d->skip[k].id = id;
    d->skip[k].until = until;
    if (k >= d->nskip) d->nskip = k + 1;
}

/* the crystal to shoot: the nearest in view in range, not left alone, no enderman in its line; -1 none */
static int crystal_pick(const struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_end *en)
{
    for (int k = 0; k < en->ncrystal; ++k)
    {
        if (en->crystal[k].d > DG_SHOOT_RANGE || skipped(d, en->crystal[k].id, in->steps)) continue;
        /* lane/dragon3: an arrow past DG_LOB_CHUNKS chunks from the player's stops in the air (its chunks' updates);
         * one there is the hunt's, from a stand near it */
        if (DG_LOB && !chunk_near(in, en->crystal[k].x, en->crystal[k].z)) continue;
        double yaw, pitch;
        if (!aim_arrow(in, en->crystal[k].x, en->crystal[k].y, en->crystal[k].z, &yaw, &pitch)) continue;
        if (ender_in_look(in, en, yaw, pitch)) continue;
        return k;
    }
    return -1;
}

/* lane/dragon2: the crystals seen kept (where seen); one is dropped once its copy is gone while within DG_MEM_GONE
 * (the loaded chunks: its blast seen and heard), never for being out of view */
static void crystal_mem(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_end *en)
{
    for (int k = 0; k < en->ncrystal; ++k)
    {
        int j = 0;
        while (j < d->ncm && d->cm[j].id != en->crystal[k].id) ++j;
        if (j == d->ncm)
        {
            if (d->ncm == NETH_RL_END_CRYSTALS) continue;
            d->cm[j].id = en->crystal[k].id;
            d->cm[j].hunts = d->cm[j].shots = 0;
            ++d->ncm;
        }
        d->cm[j].x = en->crystal[k].x, d->cm[j].y = en->crystal[k].y, d->cm[j].z = en->crystal[k].z;
    }
    if (en->beam_id)
    {
        /* the healing beam's end: a crystal seen through its beam (endview.h beam) */
        int j = 0;
        while (j < d->ncm && d->cm[j].id != en->beam_id) ++j;
        if (j == d->ncm && d->ncm < NETH_RL_END_CRYSTALS)
        {
            d->cm[j].id = en->beam_id;
            d->cm[j].x = en->beam[0], d->cm[j].y = en->beam[1], d->cm[j].z = en->beam[2];
            d->cm[j].hunts = d->cm[j].shots = 0;
            ++d->ncm;
        }
    }
    for (int j = 0; j < d->ncm;)
    {
        double dx = d->cm[j].x - in->x, dz = d->cm[j].z - in->z;
        int held = 0;
        for (int k = 0; k < en->held && k < NETH_RL_END_CRYSTALS; ++k) held |= en->held_id[k] == d->cm[j].id;
        if (!held && dx * dx + dz * dz < DG_MEM_GONE * DG_MEM_GONE)
        {
            d->cm[j] = d->cm[--d->ncm];
            continue;
        }
        ++j;
    }
}

/* lane/dragon2: crystals kept that arrows are still kept for (seen, not seen to blow up, shots left at them) */
static int crystals_left(const struct vec_dragon *d)
{
    int n = 0;
    for (int j = 0; j < d->ncm; ++j)
        n += d->cm[j].shots < DG_CRYSTAL_SHOTS && d->cm[j].hunts < DG_HUNTS &&
             (double)d->cm[j].x * d->cm[j].x + (double)d->cm[j].z * d->cm[j].z <= DG_HUNT_R * DG_HUNT_R;
    return n;
}

/* the crystal kept to hunt: the nearest within DG_HUNT_R of the island's centre with hunts left; -1 none */
static int hunt_pick(const struct vec_dragon *d, const struct dragon_in *in)
{
    int best = -1;
    double bd = 1e30;
    for (int j = 0; j < d->ncm; ++j)
    {
        double r2 = (double)d->cm[j].x * d->cm[j].x + (double)d->cm[j].z * d->cm[j].z;
        double dx = d->cm[j].x - in->x, dz = d->cm[j].z - in->z, d2 = dx * dx + dz * dz;
        if (d->cm[j].hunts >= DG_HUNTS || d->cm[j].shots >= DG_CRYSTAL_SHOTS || r2 > DG_HUNT_R * DG_HUNT_R ||
            d2 >= bd)
            continue;
        best = j, bd = d2;
    }
    return best;
}

/* the hunt's stand: DG_HUNT_D from the crystal toward the island's centre (a pillar's top edge hides its crystal from
 * nearer: past about 37 blocks for a 5-wide pillar 25 over the eye), turned DG_HUNT_TURN a retry, the side alternating */
static void hunt_stand(const struct vec_dragon *d, const struct dragon_in *in, int j, double *wx, double *wz)
{
    double cx = d->cm[j].x, cz = d->cm[j].z, r = sqrt(cx * cx + cz * cz);
    double ux = r > 1.0 ? -cx / r : 1.0, uz = r > 1.0 ? -cz / r : 0.0;
    int h = d->cm[j].hunts;
    double t = rad(DG_HUNT_TURN * ((h + 1) / 2) * (h % 2 ? 1.0 : -1.0));
    double r_st = DG_LOB ? DG_LOB_STAND : DG_HUNT_D;   /* lane/dragon3: the lob's stand is near */
    *wx = cx + r_st * (ux * cos(t) - uz * sin(t));
    *wz = cz + r_st * (ux * sin(t) + uz * cos(t));
}

static double head_dist(const struct dragon_in *in, const struct neth_rl_end *en)
{
    double dx = en->head[0] - in->x, dy = en->head[1] - (in->y + 1.62), dz = en->head[2] - in->z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

int dragon_want(const struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                const struct neth_rl_end *en)
{
    (void)d;
    if (in->dim != 1 || in->dead || in->screen != 0 || !en->on) return 0;
    return hot(s, SWORDS, 5) >= 0 || (in->sword_stock && main_sword(s)) ||
           ((hot1(s, BOW) >= 0 || main_slot(s, BOW) >= 0) && count(s, ARROW) > 0);
}

static void phase(struct vec_dragon *d, int p)
{
    d->phase = p;
    d->age = d->sub = d->sub_age = 0;
}

void dragon_begin(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                  const struct neth_rl_end *en)
{
    (void)s;
    d->on = 1;
    if (d->crystals_start == 0) d->crystals_start = en->held;
    phase(d, d->saw_death ? DP_EXIT : in->y < DG_CLIMB_Y ? DP_CLIMB : DP_SEEK);
    if (d->ax == 0.0 && d->az == 0.0) d->ax = in->x, d->az = in->z;
    d->walk_until = 0;
    d->px = in->x, d->pz = in->z;
}

/* the arrival's climb: the cell over the head dug (the best pickaxe), then a cobblestone placed under a jump, until
 * the look up meets nothing within reach (the open sky) or the feet pass DG_CLIMB_Y. 1 when over */
static int climb(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a)
{
    int fy = (int)floor(in->y + 1e-3), fx = (int)floor(in->x), fz = (int)floor(in->z);
    if (d->age > 1200 || in->y >= DG_CLIMB_Y) return 1;
    look(a, in->yaw, d->sub == 2 ? 90.0 : -90.0);
    if (d->sub == 0)
    {
        /* the look up: a block in the head's way dug, else the tower's jump */
        if (d->sub_age++ < 1) return 0;
        if (s->mo_hit && s->mo_x == fx && s->mo_z == fz && s->mo_y == fy + 2)
        {
            if (s->mo_block == BEDROCK) return 1;
            hold_slot(s, a, hot(s, PICKS, 5));
            a->hold |= 1u << PK_ATTACK;
            return 0;
        }
        if (!s->mo_hit || s->mo_y > fy + 4) return 1;  /* the sky (or room to stand): the climb's over */
        if (blocks(s) == 0) return 1;
        d->sub = 2;
        d->sub_age = 0;
        d->gx = in->y;
        return 0;
    }
    /* the tower: cobblestone in hand, the look straight down, a jump, the block placed once the feet clear it */
    if (!hold_slot(s, a, hot_block(s))) return 0;
    if (d->sub_age == 0) d->gx = in->y;
    ++d->sub_age;
    if (in->y > d->gx + 1.05 && d->sub_age < 30)
    {
        a->press[PK_USE] = 1;
        d->sub_age = 30;
        return 0;
    }
    if (d->sub_age < 30)
    {
        a->hold |= 1u << PK_JUMP;
        return 0;
    }
    if (in->on_ground && d->sub_age > 32)
    {
        d->sub = 0;         /* landed: on the new block or where it jumped from, the look up again */
        d->sub_age = 0;
    }
    if (d->sub_age > 60) d->sub = 0, d->sub_age = 0;
    return 0;
}

/* a look around (DG_SWEEP steps), turned by the policy's own look change on top (the search the policy steers) */
static void sweep(struct vec_dragon *d, const struct dragon_in *in, struct neth_rl_act *a)
{
    double yaw = in->yaw + DG_SWEEP_DEG + in->nudge[0];
    look(a, yaw, DG_SWEEP_PITCH + 0.5 * in->nudge[1]);
    ++d->sweep;
}

/* one tower step (the portal macro's): cobblestone in hand, the look straight down, a jump, the block placed under
 * the feet once they clear its cell; 1 when landed (on it, or where it jumped from) */
static int tower_step(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a)
{
    look(a, in->yaw, 90.0);
    if (d->tw < 0 && (!hold_slot(s, a, hot_block(s)) || !in->on_ground)) return ++d->tw_age > 10;
    if (d->tw < 0)
    {
        d->tw = 1;
        d->tw_age = 0;
        d->tw_base = in->y;
    }
    ++d->tw_age;
    if (d->tw == 1)
    {
        a->hold |= 1u << PK_JUMP;
        if (in->y > d->tw_base + 1.05)
        {
            a->press[PK_USE] = 1;
            d->tw = 2;
        }
        return d->tw_age > 30;
    }
    return (in->on_ground && d->tw_age > 4) || d->tw_age > 40;
}

/* a block higher where the player stands: the look up, the block over the head dug while there is one (an overhang
 * held the plain tower down), then a tower step; 1 when over */
static int rise(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a)
{
    int fx = (int)floor(in->x), fy = (int)floor(in->y + 1e-3), fz = (int)floor(in->z);
    if (d->rs == 3) return tower_step(d, in, s, a);
    look(a, in->yaw, -90.0);
    if (d->rs == 1)
    {
        d->rs = 2;
        d->rs_age = 0;
        return 0;
    }
    if (++d->rs_age > 80) return 1;
    if (s->mo_hit && s->mo_x == fx && s->mo_z == fz && s->mo_y == fy + 2)
    {
        if (s->mo_block == OBSIDIAN || s->mo_block == BEDROCK) return 1;
        hold_slot(s, a, hot(s, PICKS, 5));
        a->hold |= 1u << PK_ATTACK;
        return 0;
    }
    d->rs = 3;
    d->tw = -1;
    d->tw_age = 0;
    return tower_step(d, in, s, a);
}

/* the walk toward the waypoint: the look level on it, forward, a jump when a step does not move, a rise when the
 * jump does not clear what is in the way */
static int walk_to(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a,
                   double wx, double wz);

static void walk(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a)
{
    double wx = d->wp == 0 ? 0.5 : DG_RING * cos(rad(45.0 * (d->wp - 1))),
           wz = d->wp == 0 ? 0.5 : DG_RING * sin(rad(45.0 * (d->wp - 1)));
    int r = walk_to(d, in, s, a, wx, wz);
    if (r != 0) d->wp = (d->wp % 8) + 1;    /* reached (a look around here) or stuck: the next waypoint */
    if (r > 0) d->walk_until = in->steps;
}

/* the walk toward (wx, wz); 1 (and nothing done) once within DG_NEAR */
static int walk_to(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a,
                   double wx, double wz)
{
    if (d->rs)
    {
        if (rise(d, in, s, a)) d->rs = 0, d->tw = 0, d->pmove = 0, d->gbest = 0.0;
        return 0;
    }
    double dist = sqrt((wx - in->x) * (wx - in->x) + (wz - in->z) * (wz - in->z));
    if (dist < DG_NEAR)
    {
        d->towers = 0;
        d->gbest = 0.0;
        return 1;
    }
    double yaw, pitch;
    look_at(in->x, in->y + 1.62, in->z, wx, in->y + 1.62, wz, &yaw, &pitch);
    if (in->steps < d->detour_until)
    {
        /* lane/dragon2: around what the rises did not clear (a pillar), turned aside, jumping where a step stalls */
        look(a, yaw + d->detour_side * DG_DETOUR_DEG, 10.0);
        a->hold |= 1u << PK_FORWARD;
        double mv = (in->x - d->px) * (in->x - d->px) + (in->z - d->pz) * (in->z - d->pz);
        if (mv < 0.0025) a->hold |= 1u << PK_JUMP;
        if (in->steps + 1 >= d->detour_until) d->towers = 0, d->gbest = 0.0, d->pmove = 0, d->rise_best = 0.0;
        return 0;
    }
    look(a, yaw + 0.25 * in->nudge[0], 10.0);
    a->hold |= 1u << PK_FORWARD;
    /* no progress: no step closer to the waypoint than the best yet (sliding along a wall is none) */
    if (d->gbest <= 0.0 || dist < d->gbest - 0.05) d->gbest = dist, d->pmove = 0;
    else ++d->pmove;
    /* lane/dragon2: the rises' count holds until the walk is a block nearer than at the run's first */
    if (d->towers > 0 && d->rise_best > 0.0 && dist < d->rise_best - 1.0) d->towers = 0, d->rise_best = 0.0;
    if (d->pmove > 2) a->hold |= 1u << PK_JUMP;
    if (d->pmove > 6 && d->towers >= DG_WALK_RISE)
    {
        /* risen DG_WALK_RISE blocks without getting past: aside, not up (a tower beside a pillar is a fall) */
        d->detour_side = d->detour_side > 0 ? -1 : 1;
        d->detour_until = in->steps + DG_DETOUR;
        d->pmove = 0;
        return 0;
    }
    if (d->pmove > DG_DIG_STALL)
    {
        /* the tower did not rise (an overhang) or no tower is left: what the look meets at the feet and the head dug
         * (never obsidian or bedrock, which the chain's pickaxe does not take) */
        look(a, yaw, ((int)d->pmove / 10) % 2 ? 35.0 : 0.0);
        if (s->mo_hit && s->mo_block != OBSIDIAN && s->mo_block != BEDROCK)
        {
            hold_slot(s, a, hot(s, PICKS, 5));
            a->hold = 1u << PK_ATTACK;
        }
    }
    else if (d->pmove > 6 && d->towers < DG_TOWERS && blocks(s) > 0)
    {
        /* a wall the jump does not clear: a block higher (rise: what is over the head dug first) */
        if (d->towers == 0) d->rise_best = dist;
        ++d->towers;
        d->rs = 1;
        rise(d, in, s, a);
        return 0;
    }
    if (d->pmove > 90)
    {
        d->pmove = 0;               /* stuck: the caller's next goal */
        d->towers = 0;
        d->gbest = 0.0;
        return -1;
    }
    return 0;
}

/* the pit: DG_PIT_WALK blocks walked in from where the macro began toward the island's centre (the arrival platform
 * stands at the island's edge, the void beside it), then DG_PIT_DEPTH blocks dug straight down under the feet (the
 * look straight down, the best pickaxe): the dragon's wing push (4 blocks a tick, EntityDragon.collideWithEntities)
 * meets the pit's walls instead of carrying the player off the island. 1 when over */
static int pit(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, const struct neth_rl_end *en,
               struct neth_rl_act *a)
{
    /* the box where the player stands: each of the four cells beside the feet given a cobblestone, placed on the top
     * face of the floor block under it (the look on that face's middle, the use pressed once the crosshair is on it);
     * a side whose floor the crosshair does not meet in DG_BOX_TRY steps is left open */
    if (d->age > DG_PIT_STEPS || d->sub >= 4) return 1;
    int duck = d->dragon_seen > 0 && in->steps - d->dragon_seen < 5 && head_dist(in, en) < DG_DUCK &&
               !(DG_LATE(in, en, DG_NOPIT) && DG_OUT(in));   /* lane/dleg (dragon_late nopit): no duck out there */
    if (d->sub == 0 && d->sub_age == 0 && d->age < DG_PIT_WALK_STEPS && !duck &&
        (wait_d2(in) > DG_BOX_R * DG_BOX_R || d->rs))
    {
        /* first in from the arrival toward the centre: the server moves the dragon only while the chunks around it
         * are loaded (World.updateEntityWithOptionalForce), and from the arrival platform its circle is not */
        if (in->hunt_opts & (64 | 128))
        {
            /* lane/dleg (dragon_wait centre, band): to the centre, or to the band's wait point */
            double wx, wz;
            wait_point(in, &wx, &wz);
            walk_to(d, in, s, a, wx, wz);
        }
        else walk(d, in, s, a);
        return 0;
    }
    if (!in->on_ground) return 0;
    if (in->hunt_opts & DG_PIT_STAND) return 1;     /* lane/dleg (dragon_pit stand): no hole */
    int fx = (int)floor(in->x), fy = (int)floor(in->y + 1e-3), fz = (int)floor(in->z);
    double yaw, pitch;
    if ((in->hunt_opts & DG_PIT_ONCE) && d->hole_ok && fx == d->hole_x && fz == d->hole_z && fy <= d->hole_y)
        return 1;                                   /* lane/dleg (dragon_pit once): in its own hole, no dig */
    if (DG_HOLE_DEPTH > 0)
    {
        /* lane/dragon2: a hole dug DG_HOLE_DEPTH blocks down under the feet (the look straight down, the best
         * pickaxe), centred first: its walls are the island's end stone, which the dragon's body never breaks
         * (EntityDragon.destroyBlocksInAABB keeps obsidian, end stone and bedrock), where the cobblestone box's go at
         * its first pass; the push's 4 blocks a tick meet them */
        if (d->sub == 0 && (fabs(in->x - (fx + 0.5)) > 0.2 || fabs(in->z - (fz + 0.5)) > 0.2) && d->sub_age < 60)
        {
            ++d->sub_age;
            look_at(in->x, in->y + 1.62, in->z, fx + 0.5, in->y + 1.62, fz + 0.5, &yaw, &pitch);
            look(a, yaw, 30.0);
            a->hold |= (1u << PK_FORWARD) | (1u << PK_SNEAK);
            return 0;
        }
        /* lane/dleg (dragon_follow server): no hole dug while the client holds no dragon copy (frozen out of range:
         * nothing comes to duck from; the follow goes to it) */
        if ((in->hunt_opts & 32) && !en->dragon_id) return 1;
        if (d->sub == 0)
        {
            d->sub = 1, d->sub_age = 0, d->pit_y = fy;
            d->hole_ok = 1, d->hole_x = fx, d->hole_z = fz, d->hole_y = fy - DG_HOLE_DEPTH;   /* dragon_pit once's */
        }
        look(a, in->yaw, 90.0);
        if (fy <= d->pit_y - DG_HOLE_DEPTH || ++d->sub_age > 120 * DG_HOLE_DEPTH) return 1;
        if ((in->hunt_opts & 12) == 12) return 1;   /* lane/dleg: dragon_hole_floor, no block under the dug one */
        if (s->mo_hit && s->mo_x == fx && s->mo_y == fy - 1 && s->mo_z == fz)
        {
            if (s->mo_block == OBSIDIAN || s->mo_block == BEDROCK) return 1;
            hold_slot(s, a, hot(s, PICKS, 5));
            a->hold |= 1u << PK_ATTACK;
        }
        return 0;
    }
    if (!hold_slot(s, a, hot1(s, COBBLE))) return d->age > 20;
    static const int DX[4] = {1, 0, -1, 0}, DZ[4] = {0, 1, 0, -1};
    int cx = fx + DX[d->sub], cz = fz + DZ[d->sub];
    if (d->sub == 0 && d->sub_age == 0 && (fabs(in->x - (fx + 0.5)) > 0.2 || fabs(in->z - (fz + 0.5)) > 0.2))
    {
        /* first the feet to the cell's middle (a box block beside them must not meet the player's own box),
         * sneaking (no step off an edge) */
        look_at(in->x, in->y + 1.62, in->z, fx + 0.5, in->y + 1.62, fz + 0.5, &yaw, &pitch);
        look(a, yaw, 30.0);
        a->hold |= (1u << PK_FORWARD) | (1u << PK_SNEAK);
        return 0;
    }
    look_at(in->x, in->y + 1.62, in->z, cx + 0.5, fy + 0.02, cz + 0.5, &yaw, &pitch);
    look(a, yaw, pitch);
    if (d->sub_age == 0) d->pit_dug = count(s, COBBLE);
    ++d->sub_age;
    if (count(s, COBBLE) < d->pit_dug || d->sub_age > DG_BOX_TRY)
    {
        ++d->sub;
        d->sub_age = 0;
        return 0;
    }
    if (d->sub_age > 2 && d->sub_age % 4 == 3 && s->mo_hit && s->mo_x == cx && s->mo_y == fy - 1 && s->mo_z == cz &&
        s->mo_side == 1)
        a->press[PK_USE] = 1;
    return 0;
}

/* the shot at the target crystal: the bow drawn with the look on the arc, released, the flight watched. 1 over */
static int shoot(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                 const struct neth_rl_end *en, struct neth_rl_act *a)
{
    int k = -1;
    for (int j = 0; j < en->ncrystal; ++j)
        if (en->crystal[j].id == d->target) k = j;
    if (k >= 0) d->tx = en->crystal[k].x, d->ty = en->crystal[k].y, d->tz = en->crystal[k].z;
    double yaw, pitch;
    if (d->target == DG_AT_DRAGON)
    {
        /* the dragon: its head where it will be when the arrow arrives (its motion a step times the flight's
         * steps, the flight about the distance over the arrow's mean speed); its health watched after the release */
        if (d->sub == 2)
        {
            look(a, in->yaw, in->pitch);
            if (en->dragon_id && en->dragon_health < d->hp_before)
            {
                ++d->dragon_hits;
                d->arrow_dmg += d->hp_before - en->dragon_health;
                return 1;
            }
            return ++d->sub_age > DG_DRAGON_FLIGHT;
        }
        if (!en->dragon_id || en->dragon_dying) return 1;
        if (!en->dragon_view)
        {
            /* lane/dragon2: out of view mid-draw: the bow held where it was (a return here let the use go: an arrow
             * blind), up to DG_HOLD_LOST steps, then let go at the last lead */
            if (d->sub != 1) return 1;
            if (++d->lost_age < DG_HOLD_LOST && hot1(s, BOW) == s->current)
            {
                look(a, in->yaw, in->pitch);
                a->hold |= 1u << PK_USE;
                ++d->sub_age;
                return 0;
            }
            d->lost_age = 0;
            goto release;
        }
        d->lost_age = 0;
        double t = head_dist(in, en) / (ARROW_V * 0.85);
        d->tx = (float)(en->head[0] + d->vx * t), d->ty = (float)(en->head[1] + d->vy * t);
        d->tz = (float)(en->head[2] + d->vz * t);
    }
    if (d->sub == 2)
    {
        /* the flight: the crystal gone (the client's copies one fewer; lane/dragon3: or the target's own copy gone, its
         * blast), or the arrow's time up */
        look(a, in->yaw, in->pitch);
        int gone = en->held < d->crystals_before;
        if (DG_LOB && en->held <= NETH_RL_END_CRYSTALS)
        {
            int has = 0;
            for (int j = 0; j < en->held; ++j) has |= en->held_id[j] == d->target;
            gone |= !has;
        }
        if (gone)
        {
            ++d->crystals_hit;
            d->lob_hits += d->lob;
            return 1;
        }
        if (++d->sub_age > (d->lob ? DG_LOB_FLIGHT : DG_FLIGHT))
        {
            if (++d->tries >= DG_TRIES) skip(d, d->target, in->steps + DG_SKIP);
            return 1;
        }
        return 0;
    }
    if (!aim_arc(in, d->tx, d->ty, d->tz, d->lob, &yaw, &pitch) || ender_in_look(in, en, yaw, pitch) || !can_shoot(in, s))
    {
        if (d->target != DG_AT_DRAGON) skip(d, d->target, in->steps + 100);
        for (int j = 0; j < d->ncm && d->target != DG_AT_DRAGON; ++j)
            if (d->cm[j].id == d->target && d->hunt_ret) ++d->cm[j].hunts;   /* lane/dragon3: no arc from here */
        return 1;
    }
    look(a, yaw, pitch);
    if (!hold_slot(s, a, hot1(s, shot_item(in)))) return 0;
    if (in->snow)
    {
        /* lane/shold: a snowball is thrown at the use's press (ItemSnowball.onItemRightClick), on the look the step
         * sets: the press a step after the look is on the arc */
        if (d->sub == 0)
        {
            d->sub = 1;
            d->sub_age = 0;
            return 0;
        }
        a->press[PK_USE] = 1;
        goto release;
    }
    if (d->sub == 0)
    {
        a->press[PK_USE] = 1;
        a->hold |= 1u << PK_USE;
        d->sub = 1;
        d->sub_age = 0;
        return 0;
    }
    if (++d->sub_age < DG_DRAW)
    {
        a->hold |= 1u << PK_USE;
        return 0;
    }
    if (d->target == DG_AT_DRAGON)
    {
        /* lane/dragon2: the full draw held on the lead while the dragon comes, let go within DG_RELEASE (its head a
         * wide mark there, the hit before its bite: a hurt dragon turns away and bites nothing for 10 ticks), or once
         * it turns away within DG_RELEASE_FAR, or after DG_HOLD_MAX */
        double hd = head_dist(in, en);
        double rx = in->x - en->head[0], ry = in->y + 1.62 - en->head[1], rz = in->z - en->head[2];
        int coming = d->vx * rx + d->vy * ry + d->vz * rz > 0.0;
        if (hd > DG_RELEASE && (coming || hd > DG_RELEASE_FAR) && d->sub_age < DG_HOLD_MAX)
        {
            a->hold |= 1u << PK_USE;
            return 0;
        }
    }
release:
    /* the release: the use let go with the look on the arc */
    d->crystals_before = en->held;
    d->hp_before = en->dragon_health;
    d->shot_at = in->steps;
    ++d->shots;
    d->lobs += d->lob;
    if (d->target == DG_AT_DRAGON) ++d->dragon_shots;
    for (int j = 0; j < d->ncm; ++j)
        if (d->cm[j].id == d->target) ++d->cm[j].shots;
    d->sub = 2;
    d->sub_age = 0;
    return 0;
}

/* lane/dragon3: the crystal kept that a lob from here reaches (DG_LOB_MIN..DG_LOB_MAX, its chunk near), with arrows
 * left for it, not left alone: the nearest; -1 none */
static int lob_pick(const struct vec_dragon *d, const struct dragon_in *in)
{
    int best = -1;
    double bd = 1e30;
    for (int j = 0; j < d->ncm; ++j)
    {
        double dx = d->cm[j].x - in->x, dz = d->cm[j].z - in->z, d2 = dx * dx + dz * dz;
        if (d->cm[j].shots >= DG_CRYSTAL_SHOTS || skipped(d, d->cm[j].id, in->steps) ||
            !lob_ok(in, d->cm[j].x, d->cm[j].z) || d2 >= bd)
            continue;
        best = j, bd = d2;
    }
    return best;
}

/* lane/dragon3: the lob at kept crystal j begun (the shot's phase; back to the hunt after it when ret) */
static void lob_start(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                      const struct neth_rl_end *en, struct neth_rl_act *a, int j, int ret)
{
    phase(d, DP_SHOOT);
    d->target = d->cm[j].id;
    d->tx = d->cm[j].x, d->ty = d->cm[j].y, d->tz = d->cm[j].z;
    if (d->first_shot < 0) d->first_shot = in->steps;
    d->tries = 0;
    d->lob = 1;
    d->hunt_ret = ret;
    shoot(d, in, s, en, a);
}

/* the sword on the dragon's head while it is near: the look on it, a swing when the crosshair is on any of its parts */
static void melee(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                  const struct neth_rl_end *en, struct neth_rl_act *a)
{
    double yaw, pitch;
    look_at(in->x, in->y + 1.62, in->z, en->head[0], en->head[1], en->head[2], &yaw, &pitch);
    look(a, yaw, pitch);
    if (!hold_slot(s, a, hot(s, SWORDS, 5))) return;
    int on = s->mo_entity != 0 && s->mo_entity >= en->dragon_id && s->mo_entity <= en->dragon_id + 8;
    /* lane/dragon2: the crit (EntityPlayer.attackTargetEntityWithCurrentItem: falling, off the ground, 1.5x): a jump
     * while the head is in reach, the swing on the way down (or after DG_CRIT_WAIT steps without one) */
    int falling = !in->on_ground && in->y < d->py - 1e-3;
    if (DG_CRIT && in->on_ground) a->hold |= 1u << PK_JUMP;
    int ready = on && in->steps - d->lastswing >= DG_SWING &&
                (!DG_CRIT || falling || in->steps - d->lastswing >= DG_CRIT_WAIT || head_dist(in, en) < DG_GUARD);
    if (ready && !d->use_prev)
    {
        a->press[PK_ATTACK] = 1;
        d->lastswing = in->steps;
        ++d->swings;
        d->swing_hp = en->dragon_health;
    }
    else if (!ready && DG_BLOCK)
        a->hold |= 1u << PK_USE;    /* lane/dragon2: the sword's block between swings (a bite halved before the armour:
                                     * EntityPlayer.damageEntity); let go a step before the swing (Minecraft.runTick
                                     * drops an attack press while an item is in use) */
}

/* lane/shold (dragon_oracle crystals, PRIVILEGED): the raid on the crystal dragon.c's dg_raid_pick chose: walked to the
 * tower's cell, a tower up to one under the pillar's bedrock cap, the sword at the crystal's box (sneaking, pressed
 * toward it: the pillar's side holds the feet on the tower), the tower dug down once it is gone. 1 when over */
#define DG_RAID_STEPS 2400      /* a raid given up after this many steps short of the swing */
#define DG_RAID_SWINGS 200      /* ... or this many at the top without the crystal gone */
#define DG_RAID_REASK 200       /* steps after a pick that found none before the next */
static int rc_held(const struct neth_rl_end *en, int id)
{
    int nh = en->held < NETH_RL_END_CRYSTALS ? en->held : NETH_RL_END_CRYSTALS;
    for (int k = 0; k < nh; ++k)
        if (en->held_id[k] == id) return 1;
    return 0;
}

static void raid_down(struct vec_dragon *d, const struct dragon_in *in, int gave)
{
    if (gave)
    {
        ++d->raid_gave;
        if (d->nraid_skip < NETH_RL_END_CRYSTALS) d->raid_skip[d->nraid_skip++] = d->rc_id;
    }
    if (d->sub == 0) d->sbase = in->y;
    d->sub = 3;
    d->sub_age = 0;
    d->tw = 0;
}

static int raid(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, const struct neth_rl_end *en,
                struct neth_rl_act *a)
{
    int fx = (int)floor(in->x), fy = (int)floor(in->y + 1e-3), fz = (int)floor(in->z);
    if (d->sub < 3 && !rc_held(en, d->rc_id))
    {
        if (d->sub == 2) ++d->raid_hits;
        raid_down(d, in, 0);
    }
    if (d->sub < 3 && (d->age > DG_RAID_STEPS + DG_RAID_SWINGS || (d->sub == 2 && d->sub_age > DG_RAID_SWINGS)))
        raid_down(d, in, 1);
    ++d->sub_age;
    double tx = d->rc_sx + 0.5, tz = d->rc_sz + 0.5;
    switch (d->sub)
    {
    case 0:
    {
        double dist = sqrt((tx - in->x) * (tx - in->x) + (tz - in->z) * (tz - in->z));
        if (fx == d->rc_sx && fz == d->rc_sz && dist < 0.3 && in->on_ground)
        {
            d->sub = 1;
            d->sub_age = 0;
            d->sbase = in->y;
            d->tw = 0;
            return 0;
        }
        if (dist >= DG_NEAR)
        {
            walk_to(d, in, s, a, tx, tz);
            return 0;
        }
        double yaw, pitch;
        look_at(in->x, in->y + 1.62, in->z, tx, in->y + 1.62, tz, &yaw, &pitch);
        look(a, yaw, 30.0);
        if (dist > 0.15) a->hold |= 1u << PK_FORWARD;
        if (dist < 1.5) a->hold |= 1u << PK_SNEAK;
        double mv = (in->x - d->px) * (in->x - d->px) + (in->z - d->pz) * (in->z - d->pz);
        if (dist > 1.0 && mv < 0.0025 && d->sub_age > 3) a->hold |= 1u << PK_JUMP;
        return 0;
    }
    case 1:
        if (in->on_ground && (fx != d->rc_sx || fz != d->rc_sz))
        {
            raid_down(d, in, 1);
            return 0;
        }
        if (fy >= d->rc_top && in->on_ground && !d->tw)
        {
            d->sub = 2;
            d->sub_age = 0;
            return 0;
        }
        if (blocks(s) == 0 && !d->tw)
        {
            raid_down(d, in, 1);
            return 0;
        }
        if (!d->tw) d->tw = -1, d->tw_age = 0;
        if (tower_step(d, in, s, a)) d->tw = 0;
        return 0;
    case 2:
    {
        /* the box's point nearest the eye (2.0 wide, 2 high from the cap's cell), a little inside */
        double ex = in->x, ey = in->y + 1.62, ez = in->z;
        double px = ex < d->rc_x - 0.9 ? d->rc_x - 0.9 : ex > d->rc_x + 0.9 ? d->rc_x + 0.9 : ex;
        double pz = ez < d->rc_z - 0.9 ? d->rc_z - 0.9 : ez > d->rc_z + 0.9 ? d->rc_z + 0.9 : ez;
        double py = ey < d->rc_y - 0.8 ? d->rc_y - 0.8 : ey > d->rc_y + 0.8 ? d->rc_y + 0.8 : ey;
        double yaw, pitch;
        look_at(ex, ey, ez, px, py, pz, &yaw, &pitch);
        look(a, yaw, pitch);
        a->hold |= (1u << PK_SNEAK) | (1u << PK_FORWARD);
        int sw = hot(s, SWORDS, 5);
        if (sw >= 0) hold_slot(s, a, sw);
        if (in->steps - d->lastswing >= DG_SWING && (s->mo_entity == d->rc_id || d->sub_age % 20 == 19))
        {
            a->press[PK_ATTACK] = 1;
            d->lastswing = in->steps;
        }
        return 0;
    }
    default:
        /* the tower dug down from the top (the block under the feet) to where it began */
        if ((in->y <= d->sbase + 0.01 && in->on_ground) || d->sub_age > 60 * (d->rc_top - d->rc_ground + 3)) return 1;
        look(a, in->yaw, 90.0);
        if (s->mo_hit && s->mo_x == fx && s->mo_y == fy - 1 && s->mo_z == fz && s->mo_block != OBSIDIAN &&
            s->mo_block != BEDROCK)
        {
            hold_slot(s, a, hot(s, PICKS, 5));
            a->hold |= 1u << PK_ATTACK;
        }
        else if (s->mo_hit && (s->mo_block == OBSIDIAN || s->mo_block == BEDROCK))
            return 1;
        return 0;
    }
}

static int eat_want(const struct dragon_in *in, const struct neth_rl_state *s)
{
    if (hot(s, FOODS, (int)(sizeof FOODS / sizeof FOODS[0])) < 0) return 0;
    return in->food <= DG_EAT_FOOD || (in->food < 20 && in->health <= DG_EAT_HEALTH) ||
           (in->food < DG_EAT_REGEN && in->health < 20.0f);
}

/* the exit: once the portal stands (DG_EXIT_WAIT after the death seen), walked to its ring beside the bedrock
 * column under where the dragon died, a jump when a step does not move. The portal is at y 64 whatever the ground
 * (EntityDragon.createEnderPortal: bedrock under it at 63, its rim at 64): stuck near it and lower than the rim's top,
 * a tower step (cobblestone) up to it. Over when the dimension changes */
static void exit_walk(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a)
{
    if (in->steps - d->dead_step < DG_EXIT_WAIT)
    {
        /* lane/dragon2: away from under the dying dragon first (the portal forms where it dies, 200 ticks on: a
         * player in its hole under it ends inside the portal's base, bedrock overhead, every kill's exit lost) */
        double ox = in->x - d->dx, oz = in->z - d->dz, od = sqrt(ox * ox + oz * oz);
        if (od < DG_EXIT_CLEAR)
        {
            if (od < 0.5) ox = 1.0, oz = 0.0, od = 1.0;
            walk_to(d, in, s, a, d->dx + ox / od * (DG_EXIT_CLEAR + 2.0), d->dz + oz / od * (DG_EXIT_CLEAR + 2.0));
            return;
        }
        look(a, in->yaw, 0.0);
        return;
    }
    if (!d->col_found && d->col_age < DG_COL_LOOK && d->sub == 0)
    {
        /* lane/dragon2: the portal's column seen: bedrock 65 to 68 high (EntityDragon.createEnderPortal: the column 64
         * to 67 over the rim's 64), looked for by turning before the walk; its cell is the portal's middle (the
         * dragon's last place seen stood 5 blocks off it in two of three kills) */
        if (s->mo_hit && s->mo_block == BEDROCK && s->mo_y >= 65 && s->mo_y <= 68)
        {
            d->col_found = 1;
            d->dx = (float)s->mo_x + 0.25f, d->dz = (float)s->mo_z + 0.25f;
        }
        else
        {
            ++d->col_age;
            static const double CP[3] = {-6.0, -16.0, 2.0};
            look(a, in->yaw + DG_SWEEP_DEG, CP[(d->col_age / DG_SWEEP) % 3]);
            return;
        }
    }
    double cx = floor(d->dx) + 0.5, cz = floor(d->dz) + 0.5;
    /* a portal cell beside the column; lane/dragon3: another of eight round it after each stand at the rim's height
     * that never fell in */
    static const double EX[8][2] = {{1.5, 0.0}, {0.0, 1.5}, {-1.5, 0.0}, {0.0, -1.5},
                                    {1.0, 1.0}, {-1.0, 1.0}, {-1.0, -1.0}, {1.0, -1.0}};
    double tx = cx + EX[d->exit_try % 8][0], tz = cz + EX[d->exit_try % 8][1];
    double dist = sqrt((tx - in->x) * (tx - in->x) + (tz - in->z) * (tz - in->z));
    /* the feet's cell's middle from the portal's middle: the rim's cells are those within 3.5 */
    double fx = floor(in->x) + 0.5 - cx, fz = floor(in->z) + 0.5 - cz, fc = sqrt(fx * fx + fz * fz);
    double yaw, pitch;
    look_at(in->x, in->y + 1.62, in->z, cx, in->y + 1.62, cz, &yaw, &pitch);
    if (d->sub == 1)
    {
        /* under the rim (no room to step up onto the bedrock): backed out to the first cell past it */
        if (fc > DG_EXIT_OUT || ++d->sub_age > 80) d->sub = 2, d->sub_age = 0;
        look(a, yaw, 30.0);
        a->hold |= 1u << PK_BACK;
        d->px = in->x, d->pz = in->z;
        return;
    }
    if (d->sub == 2)
    {
        /* a tower there up to the rim's top, then the walk in over it (the portal a block under) */
        if (in->y >= DG_EXIT_RIM || ++d->sub_age > 400)
        {
            d->sub = 3, d->tw = 0, d->pmove = 0;
            d->px = in->x, d->pz = in->z;
            return;
        }
        /* lane/dragon2: rise's (what is over the head dug first: a plain tower step under a block stood 4,000 steps) */
        if (d->rs == 2 && s->mo_hit && s->mo_block == BEDROCK && s->mo_y > (int)floor(in->y + 1e-3) &&
            s->mo_y <= (int)floor(in->y + 1e-3) + 3)
        {
            /* bedrock over the head: under the portal's own rim or floor (it formed over the hole): out first, the walk
             * digging what is not bedrock, away from the guessed middle, then the column looked for again */
            d->sub = 4, d->sub_age = 0, d->rs = 0, d->tw = 0;
            d->gx = in->x + (in->x - cx) * 8.0 / (fc > 0.5 ? fc : 1.0), d->gz = in->z + (in->z - cz) * 8.0 /
                                                                                         (fc > 0.5 ? fc : 1.0);
            if (fc <= 0.5) d->gx = in->x + 8.0;
            return;
        }
        if (!d->rs && in->on_ground) d->rs = 1;
        if (d->rs && rise(d, in, s, a)) d->rs = 0, d->tw = 0;
        d->px = in->x, d->pz = in->z;
        return;
    }
    if (d->sub == 4)
    {
        if (walk_to(d, in, s, a, d->gx, d->gz) != 0 || ++d->sub_age > 200)
            d->sub = 0, d->col_found = 0, d->col_age = 0, d->pmove = 0;
        d->px = in->x, d->pz = in->z;
        return;
    }
    if (d->sub == 0 && d->pmove > DG_EXIT_STALL && dist < DG_EXIT_TOWER_R && in->y < DG_EXIT_RIM && in->on_ground &&
        hot_block(s) >= 0)
    {
        d->sub = fc <= DG_EXIT_OUT ? 1 : 2;
        d->sub_age = 0;
        d->px = in->x, d->pz = in->z;
        return;
    }
    if (d->sub == 0 && dist > DG_EXIT_TOWER_R)
    {
        /* lane/dragon2: the way there is the walk's (its jump, rises, turns aside and digging: the plain walk stood
         * jumping at a wall it could not clear for 10,000 steps after a kill) */
        if (walk_to(d, in, s, a, tx, tz) < 0) d->detour_side = -d->detour_side;
        d->px = in->x, d->pz = in->z;
        return;
    }
    look_at(in->x, in->y + 1.62, in->z, tx, in->y + 1.62, tz, &yaw, &pitch);
    look(a, yaw, 30.0);
    if (dist > 0.3) a->hold |= 1u << PK_FORWARD;
    double mv = sqrt((in->x - d->px) * (in->x - d->px) + (in->z - d->pz) * (in->z - d->pz));
    d->pmove = mv < 0.05 ? d->pmove + 1 : 0;
    d->px = in->x, d->pz = in->z;
    if (d->pmove > 2 || dist < 3.0) a->hold |= 1u << PK_JUMP;
    if (d->pmove > DG_EXIT_STUCK && in->y < DG_EXIT_RIM - 1.5)
    {
        /* lane/dragon3: stuck under the portal's floor (its own hole there: the portal formed over it, bedrock at 63
         * overhead, the cell beside the column reached from below): out DG_EXIT_OUT_WALK blocks away from the middle,
         * then the column found again and the rim climbed from outside */
        double ox = in->x - cx, oz = in->z - cz, od = sqrt(ox * ox + oz * oz);
        if (od < 0.5) ox = 1.0, oz = 0.0, od = 1.0;
        d->sub = 4, d->sub_age = 0, d->rs = 0, d->tw = 0, d->pmove = 0;
        d->gx = cx + ox / od * DG_EXIT_OUT_WALK, d->gz = cz + oz / od * DG_EXIT_OUT_WALK;
        ++d->exit_unstuck;
    }
    else if (d->pmove > DG_EXIT_STUCK)
    {
        /* lane/dragon3: at the rim's height on solid ground, not in the portal: the column looked for again (the one
         * read may be off), the next cell round it */
        ++d->exit_try;
        d->sub = 0, d->col_found = 0, d->col_age = 0, d->pmove = 0;
        ++d->exit_unstuck;
    }
}

/* lane/dleg (dragon_clutch water): a fall caught in water. Airborne and going down DG_CLUTCH_FALL under the highest
 * feet since the ground, a water bucket in the hotbar: the look straight down, the bucket in hand, the use pressed once
 * the crosshair meets a block's top face (in reach: the water goes into the cell over it, where the feet land; the
 * server's handleFalling reads the water before the fall's damage). Back on the ground, the water taken up again with
 * the empty bucket (its ray meets the source in the feet's cell). 1 when the step is the catch's */
static int clutch(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s, struct neth_rl_act *a)
{
    if (!d->fpk_ok || in->on_ground || in->y > d->fpeak) d->fpeak = in->y, d->fpk_ok = 1;
    int wb = hot1(s, WATER_B);
    if (!in->on_ground && in->y < d->py - 1e-3 && d->fpeak - in->y >= DG_CLUTCH_FALL && (wb >= 0 || d->clutch == 1))
    {
        if (d->clutch != 1) d->clutch = 1, d->clutch_age = 0, d->clutch_pressed = 0;
        ++d->clutch_age;
        look(a, in->yaw, 90.0);
        /* the state's mouseover is a tick old (the client's, before this tick's move); the act's runs from where the
         * body is now. So when the ground (in->hunt_opts' bits 16-23, read off the End's blocks) is in the 4.5 reach
         * from the eye now and the next tick lands (its fall, (dy - 0.08) * 0.98, reaches the ground), the press goes
         * on this tick whatever the old mouseover held: the fast falls' only tick in reach */
        double dy = in->y - d->py, top = (double)(in->hunt_opts >> 16 & 255);
        int last = (in->hunt_opts & 16) && in->y + 1.62 - top <= 4.45 && in->y + (dy - 0.08) * 0.98 <= top + 1e-3;
        if (wb >= 0 && hold_slot(s, a, wb) && !d->clutch_pressed && ((s->mo_hit && s->mo_side == 1) || last))
        {
            a->press[PK_USE] = 1;
            d->clutch_pressed = 1;
            if (s->mo_hit && s->mo_side == 1)
                d->cl_x = s->mo_x, d->cl_y = s->mo_y + 1, d->cl_z = s->mo_z;   /* the source's cell */
            else
                d->cl_x = (int)floor(in->x), d->cl_y = (int)top, d->cl_z = (int)floor(in->z);
            ++d->clutches;
        }
        return 1;
    }
    if (d->clutch == 1) d->clutch = d->clutch_pressed && in->on_ground ? 2 : 0, d->clutch_age = 0;
#ifdef CLUTCH_TRACE
    if (d->clutch == 2 || d->clutch_pressed)
        fprintf(stderr, "CLU step %lld state %d age %d og %d y %.3f wb %d eb %d cur %d mo %d %d,%d,%d side %d blk %d\n",
                (long long)in->steps, d->clutch, d->clutch_age, in->on_ground, in->y, wb, hot1(s, BUCKET), s->current,
                s->mo_hit, s->mo_x, s->mo_y, s->mo_z, s->mo_side, s->mo_block);
#endif
    if (d->clutch == 2)
    {
        int eb = hot1(s, BUCKET);
        if (wb >= 0 && d->clutch_age > 0) ++d->clutch_ups;
        if (eb < 0 || wb >= 0 || ++d->clutch_age > DG_CLUTCH_UP)
        {
            d->clutch = 0;
            return 0;
        }
        /* the look on the source's top (the feet are often in the cell beside it, whose water is flowing: the bucket's
         * ray passes through flowing water) */
        double yaw, pitch;
        look_at(in->x, in->y + 1.62, in->z, d->cl_x + 0.5, d->cl_y + 0.8, d->cl_z + 0.5, &yaw, &pitch);
        look(a, yaw, pitch);
        if (hold_slot(s, a, eb) && d->clutch_age % DG_CLUTCH_UP_EVERY == 1) a->press[PK_USE] = 1;
        return 1;
    }
    return 0;
}

int dragon_drive(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_state *s,
                 const struct neth_rl_end *en, struct neth_rl_act *a)
{
    if (in->dim != 1 || in->dead || (in->screen != 0 && !(in->screen == PS_INVENTORY && d->phase == DP_FETCH)))
    {
        d->on = 0;
        return 1;
    }
    ++d->age;
    d->arrows = count(s, ARROW);
    if (d->age > 1 && d->arrows > d->arrows_prev) d->picked += d->arrows - d->arrows_prev;
    d->arrows_prev = d->arrows;
    d->blk = blocks(s), d->blk_hot = hot_block(s);
    d->mo_block = s->mo_hit ? s->mo_block : -1, d->mo_dy = s->mo_hit ? s->mo_y - (int)floor(in->y + 1e-3) : -99;
    a->hold = 0;
    memset(a->press, 0, sizeof a->press);
    a->look_mode = 0;
    if (en->dragon_view || en->dragon_dying)
    {
        d->dx = en->pos[0], d->dz = en->pos[2];
        if (d->pseen == in->steps - 1)
            d->vx = en->head[0] - d->pdx, d->vy = en->head[1] - d->pdy, d->vz = en->head[2] - d->pdz;
        else
            d->vx = d->vy = d->vz = 0.0f;
        d->pdx = en->head[0], d->pdy = en->head[1], d->pdz = en->head[2];
        d->pseen = in->steps;
        if (en->dragon_view) d->dragon_seen = in->steps;
    }
    if (en->dragon_view && !en->dragon_dying)
    {
        if (d->hp_seen > 0.0f && en->dragon_health > d->hp_seen + DG_HEAL) d->healed = 1;
        if (d->hp_seen > 0.0f && en->dragon_health > d->hp_seen) d->heal_sum += en->dragon_health - d->hp_seen;
        d->hp_seen = en->dragon_health;
    }
    if (d->swing_hp > 0.0f && in->steps - d->lastswing <= 3 && en->dragon_id && en->dragon_health < d->swing_hp)
    {
        /* the books: the health a swing took (seen within 3 steps of it) */
        d->sword_dmg += d->swing_hp - en->dragon_health;
        ++d->sword_hits;
        d->swing_hp = 0.0f;
    }
    crystal_mem(d, in, en);
    if (d->raid_ask)
    {
        /* lane/shold: the pick asked for last step (dragon.c dg_raid_pick filled dragon_in's rc_* this step) */
        d->raid_ask = 0;
        if (in->rc_ok)
        {
            d->raid_have = 1;
            d->rc_id = in->rc_id, d->rc_sx = in->rc_sx, d->rc_sz = in->rc_sz;
            d->rc_top = in->rc_top, d->rc_ground = in->rc_ground;
            d->rc_x = in->rc_x, d->rc_y = in->rc_y, d->rc_z = in->rc_z;
        }
        else
            d->raid_none_at = in->steps > 0 ? in->steps : 1;
    }
    if (can_shoot(in, s) && crystal_pick(d, in, en) >= 0) d->saw_c = 1;
    if (DG_CLUTCH && clutch(d, in, s, a))
    {
        d->px = in->x, d->pz = in->z, d->py = in->y;
        d->use_prev = (a->hold >> PK_USE) & 1;
        return 0;
    }
    if (d->saw_death && d->phase != DP_EXIT) phase(d, DP_EXIT);
    int near = en->dragon_id && !en->dragon_dying && en->dragon_view && head_dist(in, en) < DG_MELEE;
    if ((d->phase == DP_PIT || d->phase == DP_SNIPE || d->phase == DP_DESCEND || d->phase == DP_HUNT ||
         (d->phase == DP_RAID && d->sub == 0)) && !near &&
        !d->tw && !d->rs &&
        in->on_ground && eat_want(in, s))
    {
        /* eating from any phase but a shot, a fetch and the exit, which is taken up again after it */
        d->ret_phase = d->phase;
        phase(d, DP_EAT);
        d->eating = 0;
        d->efood = in->food;
    }
    switch (d->phase)
    {
    case DP_CLIMB:
        if (climb(d, in, s, a)) phase(d, DP_SEEK);
        break;
    case DP_EAT:
        if (near || ++d->eating > DG_EAT_STEPS || in->food > d->efood)
        {
            phase(d, d->ret_phase ? d->ret_phase : DP_SEEK);
            d->ret_phase = 0;
            break;
        }
        look(a, in->yaw, 30.0);
        if (hold_slot(s, a, hot(s, FOODS, (int)(sizeof FOODS / sizeof FOODS[0]))))
        {
            a->hold |= 1u << PK_USE;
            if (d->eating == 1) a->press[PK_USE] = 1;
        }
        else
            d->eating = 0;
        break;
    case DP_SHOOT:
        if (near && (d->sub == 0 || (d->sub == 1 && d->target != DG_AT_DRAGON)))
        {
            phase(d, DP_SEEK);
            d->lob = d->hunt_ret = 0;
            melee(d, in, s, en, a);
            break;
        }
        if (shoot(d, in, s, en, a))
        {
            phase(d, d->snipe > 0 ? DP_SNIPE : d->hunt_ret ? DP_HUNT : DP_SEEK);
            d->lob = d->hunt_ret = 0;
        }
        break;
    case DP_EXIT:
        exit_walk(d, in, s, a);
        break;
    case DP_RAID:
        if (near && d->sub != 1)
        {
            melee(d, in, s, en, a);
            break;
        }
        if (raid(d, in, s, en, a)) phase(d, DP_SEEK);
        break;
    case DP_FETCH:
        if (fetch(d, in, s, a, d->fetch_id)) phase(d, DP_SEEK);
        break;
    case DP_SNIPE:
    {
        /* the dragon asleep (no copy: its chunks are not loaded, so the server does not move it): a tower up in
         * place, a look around a block, every crystal in view shot from it; down once a look around from the top
         * shows none to shoot, or when the dragon comes */
        if (d->snipe == 1 ? en->dragon_id && !en->dragon_dying && (en->dragon_view || near)
                          : near || d->crystals_hit > d->snipe_hits ||
                                (en->dragon_view && !en->dragon_dying &&
                                 (head_dist(in, en) < DG_SNIPE_DUCK || d->stowers > 0)))
        {
            phase(d, DP_DESCEND);
            break;
        }
        int k = can_shoot(in, s) ? crystal_pick(d, in, en) : -1;
        if (k >= 0 && !d->tw)
        {
            phase(d, DP_SHOOT);
            d->target = en->crystal[k].id;
            d->tx = en->crystal[k].x, d->ty = en->crystal[k].y, d->tz = en->crystal[k].z;
            if (d->first_shot < 0) d->first_shot = in->steps;
            d->tries = 0;
            shoot(d, in, s, en, a);
            break;
        }
        if (d->tw)
        {
            if (tower_step(d, in, s, a)) d->tw = 0;
            break;
        }
        if (DG_LOB && d->sweep >= DG_SWEEP && in->on_ground && can_shoot(in, s))
        {
            /* lane/dragon3: a look around over: a crystal kept within the lob's reach lobbed from the tower */
            int j = lob_pick(d, in);
            if (j >= 0)
            {
                lob_start(d, in, s, en, a, j, 0);
                break;
            }
        }
        if (d->sweep >= DG_SWEEP)
        {
            int more = !d->saw_c;
            d->sweep = 0;
            d->saw_c = 0;
            if (more && d->stowers < DG_SNIPE_TOWERS && blocks(s) > 0 && hot_block(s) >= 0)
            {
                ++d->stowers;
                d->tw = -1;
                d->tw_age = 0;
                tower_step(d, in, s, a);
                break;
            }
            if (more)
            {
                phase(d, DP_DESCEND);
                break;
            }
        }
        sweep(d, in, a);
        break;
    }
    case DP_DESCEND:
    {
        /* the snipe's tower dug down from the top (the block under the feet, the best pickaxe) to where it began */
        int fx = (int)floor(in->x), fy = (int)floor(in->y + 1e-3), fz = (int)floor(in->z);
        if ((in->y <= d->sbase + 0.01 && in->on_ground) || d->age > 60 * (DG_SNIPE_TOWERS + 2))
        {
            d->snipe = -1;
            phase(d, DP_SEEK);
            break;
        }
        look(a, in->yaw, 90.0);
        if (s->mo_hit && s->mo_x == fx && s->mo_y == fy - 1 && s->mo_z == fz && s->mo_block != OBSIDIAN &&
            s->mo_block != BEDROCK)
        {
            hold_slot(s, a, hot(s, PICKS, 5));
            a->hold |= 1u << PK_ATTACK;
        }
        else if (s->mo_hit && (s->mo_block == OBSIDIAN || s->mo_block == BEDROCK))
        {
            d->snipe = -1;
            phase(d, DP_SEEK);
        }
        break;
    }
    case DP_HUNT:
    {
        /* lane/dragon2: to the stand for the hunted crystal, the look on where it was seen, shot once in view */
        int j = 0;
        while (j < d->ncm && d->cm[j].id != d->hunt) ++j;
        if (j == d->ncm || !can_shoot(in, s) || d->cm[j].shots >= DG_CRYSTAL_SHOTS ||
            ((in->hunt_opts & 1) && d->cm[j].hunts >= DG_HUNTS) ||   /* lane/legs: dragon_hunt_cap */
            DG_LATE(in, en, DG_NOHUNT))                              /* lane/dleg: dragon_late nohunt */
        {
            phase(d, DP_SEEK);
            break;
        }
        if (near)
        {
            melee(d, in, s, en, a);
            break;
        }
        if (en->dragon_id && en->dragon_view && head_dist(in, en) < DG_DUCK && !DG_LATE(in, en, DG_NOPIT) &&
            in->x * in->x + in->z * in->z > DG_PIT_R * DG_PIT_R && !d->rs && !d->tw && in->on_ground)
        {
            /* lane/dragon2: the dragon coming while the hunt is away from the centre (from the arrival its first stand
             * is through the island's east cliff): a hole where it stands (the pit's duck), the hunt after */
            d->pitted = 0;
            phase(d, DP_PIT);
            break;
        }
        int k = crystal_pick(d, in, en);
        if (k >= 0 && !d->rs)
        {
            phase(d, DP_SHOOT);
            d->target = en->crystal[k].id;
            d->tx = en->crystal[k].x, d->ty = en->crystal[k].y, d->tz = en->crystal[k].z;
            if (d->first_shot < 0) d->first_shot = in->steps;
            d->tries = 0;
            d->lob = 0;
            d->hunt_ret = DG_LOB;
            shoot(d, in, s, en, a);
            break;
        }
        if (d->age > DG_HUNT_STEPS)
        {
            ++d->cm[j].hunts;
            phase(d, DP_SEEK);
            break;
        }
        if (d->sub == 0)
        {
            double wx, wz;
            hunt_stand(d, in, j, &wx, &wz);
            /* lane/dragon3: a lob from wherever the walk stands within the lob's reach (on the ground, not mid-rise) */
            double cdx = d->cm[j].x - in->x, cdz = d->cm[j].z - in->z;
            if (DG_LOB && lob_ok(in, d->cm[j].x, d->cm[j].z) && in->on_ground && !d->rs && !d->tw &&
                cdx * cdx + cdz * cdz <= (DG_LOB_STAND + 4.0) * (DG_LOB_STAND + 4.0))
            {
                d->sub = 1, d->sub_age = 0;
                break;
            }
            int r = walk_to(d, in, s, a, wx, wz);
            if (r < 0) ++d->cm[j].hunts, phase(d, DP_SEEK);
            else if (r > 0) d->sub = 1, d->sub_age = 0;
            break;
        }
        if (in->snow && d->tw)
        {
            /* lane/shold: the stand's tower step under way */
            if (tower_step(d, in, s, a)) d->tw = 0;
            d->sub_age = 0;
            break;
        }
        if (in->snow && in->on_ground && !d->rs && lob_ok(in, d->cm[j].x, d->cm[j].z))
        {
            /* lane/shold (dragon_crystals snowball): a snowball's highest arc rises about 29 blocks, so a tall
             * pillar's crystal is out of its reach from the ground: a block higher at the stand until an arc reaches
             * (at most DG_SNOW_TOWERS; dug down when the hunt is over) */
            double ty, tp;
            int tcap = in->hunt_opts >> 8 & 255 ? in->hunt_opts >> 8 & 255 : DG_SNOW_TOWERS;   /* lane/legs */
            if (!aim_arc(in, d->cm[j].x, d->cm[j].y, d->cm[j].z, 1, &ty, &tp) && d->htower < tcap &&
                blocks(s) > 0 && hot_block(s) >= 0)
            {
                if (d->htower == 0) d->hbase = in->y;
                ++d->htower;
                d->tw = -1;
                d->tw_age = 0;
                tower_step(d, in, s, a);
                d->sub_age = 0;
                break;
            }
        }
        double yaw, pitch;
        look_at(in->x, in->y + 1.62, in->z, d->cm[j].x, d->cm[j].y, d->cm[j].z, &yaw, &pitch);
        look(a, yaw + 0.5 * in->nudge[0], pitch + 0.5 * in->nudge[1]);
        if (DG_LOB && d->sub_age >= DG_LOB_LOOK && lob_ok(in, d->cm[j].x, d->cm[j].z) && in->on_ground)
        {
            /* lane/dragon3: not in view from the stand (the pillar's edge): the lob at its place */
            lob_start(d, in, s, en, a, j, 1);
            break;
        }
        if (++d->sub_age > (DG_LOB ? 3 * DG_LOB_LOOK : DG_HUNT_LOOK)) ++d->cm[j].hunts, phase(d, DP_SEEK);
        break;
    }
    case DP_FOLLOW:
    {
        /* lane/dragon2: toward the dragon's last place in view (kept within DG_FOLLOW_R of the centre), until it is in
         * view again; then a new hole (the pit) */
        double gx = d->dx, gz = d->dz, r;
        /* lane/dleg (dragon_follow server, PRIVILEGED): toward the server's dragon (frozen where its chunks are not
         * all loaded), kept within DG_FOLLOW_FAR, until the client holds its copy; there and still none: waited on */
        int srv = (in->hunt_opts & 32) && (en->sv_pos[0] != 0.0f || en->sv_pos[2] != 0.0f);
        if (srv) gx = en->sv_pos[0], gz = en->sv_pos[2];
        r = sqrt(gx * gx + gz * gz);
        double fr = srv ? DG_FOLLOW_FAR : DG_FOLLOW_R;
        if (r > fr) gx *= fr / r, gz *= fr / r;
        if ((en->dragon_view && d->age > 1) || (srv && en->dragon_id && d->age > 1) || d->age > DG_HUNT_STEPS)
        {
            d->dragon_seen = in->steps;
            phase(d, DP_SEEK);
            break;
        }
        if (srv && (in->x - gx) * (in->x - gx) + (in->z - gz) * (in->z - gz) < 4.0)
        {
            sweep(d, in, a);    /* at the island's edge toward it: its chunks load, it comes */
            break;
        }
        int wr = walk_to(d, in, s, a, gx, gz);
        /* (vecenv.c sneaks this walk where a cell beside the feet has no floor within a survived fall: the rim's
         * last cell is where it stops) */
        if (wr != 0 && srv)
        {
            sweep(d, in, a);    /* stopped short (the rim, a wall): waited on there */
            break;
        }
        if (wr != 0)
        {
            /* there and still nothing: a look around, then back to the centre's hole */
            d->dragon_seen = in->steps;
            phase(d, DP_SEEK);
        }
        break;
    }
    case DP_PIT:
        if (near)
        {
            melee(d, in, s, en, a);
            break;
        }
        if (pit(d, in, s, en, a))
        {
            d->pitted = d->age <= DG_PIT_STEPS ? 1 : -1;
            phase(d, DP_SEEK);
        }
        break;
    default:
    {
        /* the bow and cobblestone in the hotbar (the start's are in the main inventory): each fetched once it is
         * needed, at most DG_FETCHES fetches an episode */
        /* lane/chain2 (dragon_food fetch): the food first, when eat_want's level wants it and the hotbar holds none */
        int pack_food = 0;
        if (in->food_fetch && hot(s, FOODS, (int)(sizeof FOODS / sizeof FOODS[0])) < 0 &&
            (in->food <= DG_EAT_FOOD || (in->food < 20 && in->health <= DG_EAT_HEALTH) ||
             (in->food < DG_EAT_REGEN && in->health < 20.0f)))
            for (size_t j = 0; j + 1 < sizeof FOODS / sizeof FOODS[0] && !pack_food; ++j)   /* (no rotten flesh) */
                if (main_slot(s, FOODS[j]) >= 0) pack_food = FOODS[j];
        int pack_sword = in->sword_stock && hot(s, SWORDS, 5) < 0 ? main_sword(s) : 0;
        int want = !near && d->fetches < DG_FETCHES
                       ? pack_sword ? pack_sword
                         : pack_food ? pack_food
                         : (in->snow && hot1(s, SNOWBALL) < 0 && main_slot(s, SNOWBALL) >= 0) ? SNOWBALL
                         : (DG_CLUTCH && !d->clutch && hot1(s, WATER_B) < 0 && main_slot(s, WATER_B) >= 0)
                             ? WATER_B   /* lane/dleg: dragon_clutch water */
                         : (hot1(s, BOW) < 0 && main_slot(s, BOW) >= 0 && count(s, ARROW) > 0) ? BOW
                         : (hot_block(s) < 0 && main_slot(s, COBBLE) >= 0) ? COBBLE : 0
                       : 0;
        if (want)
        {
            ++d->fetches;
            phase(d, DP_FETCH);
            d->fetch_id = want;
            d->fetch_pressed = 0;
            fetch(d, in, s, a, want);
            break;
        }
        if (in->snow && d->htower && in->on_ground && !d->tw)
        {
            /* lane/shold: a hunt's stand tower dug down (DP_DESCEND, to where it began) before anything else */
            d->htower = 0;
            if (in->y > d->hbase + 0.5)
            {
                d->sbase = d->hbase;
                phase(d, DP_DESCEND);
                break;
            }
        }
        if (in->oracle && !near)
        {
            /* lane/shold (dragon_oracle crystals): a crystal a tower reaches taken first; else a pick asked for */
            if (d->raid_have)
            {
                d->raid_have = 0;
                ++d->raids;
                phase(d, DP_RAID);
                raid(d, in, s, en, a);
                break;
            }
            if (en->held > 0 && (d->raid_none_at == 0 || in->steps - d->raid_none_at > DG_RAID_REASK)) d->raid_ask = 1;
        }
        if (d->snipe == 0 && !near)
        {
            /* once, first: the snipe while the dragon sleeps (no copy of it in the client world) */
            d->snipe = en->dragon_id ? -1 : 1;
            if (d->snipe > 0)
            {
                d->sbase = in->y;
                d->stowers = 0;
                d->sweep = 0;
                d->saw_c = 0;
                phase(d, DP_SNIPE);
                sweep(d, in, a);
                break;
            }
        }
        if (!near && d->snipe != 0 && can_shoot(in, s) && d->pitted != 0 &&
            (in->x * in->x + in->z * in->z <= DG_PIT_R * DG_PIT_R || d->dragon_seen == 0 ||
             in->steps - d->dragon_seen > DG_UNBOX ||
             (DG_LOB && !(en->dragon_id && en->dragon_view && head_dist(in, en) < DG_DUCK))))
        {
            /* lane/dragon2: a crystal seen and not seen to blow up: hunted from its stand (out of the hole: a new one is
             * dug after) */
            int j = DG_LATE(in, en, DG_NOHUNT) ? -1 : hunt_pick(d, in);   /* lane/dleg (dragon_late nohunt) */
            if (j >= 0)
            {
                d->hunt = d->cm[j].id;
                /* lane/dragon3: out of the hole for the hunts (-2: one after another, the hole after the last; lane/dragon2's
                 * walked back to the centre and dug again between two hunts, a third of the first ten minutes) */
                if (d->pitted > 0 || (DG_LOB && d->pitted == 0)) d->pitted = DG_LOB ? -2 : 0;
                phase(d, DP_HUNT);
                break;
            }
            if (d->pitted == -2) d->pitted = 0;     /* lane/dragon3: the hunts over: back to the centre's hole */
        }
        if (d->pitted == -2 && !(d->snipe != 0 && can_shoot(in, s))) d->pitted = 0;
        if (!d->pitted && !near)
        {
            /* once: in from the edge and into a pit (pit()); a pit given up is not tried again */
            d->pitted = -1;
            phase(d, DP_PIT);
            pit(d, in, s, en, a);
            break;
        }
        /* DP_SEEK: the dragon near first, then a crystal in view, eating at need, the dragon followed with the look
         * while it is in view, else a look around and a walk */
        if (near)
        {
            melee(d, in, s, en, a);
            break;
        }
        int k = can_shoot(in, s) ? crystal_pick(d, in, en) : -1;
        if (k >= 0)
        {
            phase(d, DP_SHOOT);
            d->target = en->crystal[k].id;
            d->tx = en->crystal[k].x, d->ty = en->crystal[k].y, d->tz = en->crystal[k].z;
            if (d->first_shot < 0) d->first_shot = in->steps;
            d->tries = 0;
            shoot(d, in, s, en, a);
            break;
        }
        if (eat_want(in, s))
        {
            phase(d, DP_EAT);
            d->eating = 0;
            d->efood = in->food;
            break;
        }
        if (DG_LOB && can_shoot(in, s) && in->on_ground && !d->tw && !d->rs &&
            !(en->dragon_id && en->dragon_view && head_dist(in, en) < DG_DUCK))
        {
            /* lane/dragon3: a crystal kept within the lob's reach from here: lobbed where it stands */
            int j = lob_pick(d, in);
            if (j >= 0)
            {
                lob_start(d, in, s, en, a, j, 0);
                break;
            }
        }
        if (en->dragon_id && en->dragon_view && !en->dragon_dying && !in->snow && can_shoot(in, s) &&
            count(s, ARROW) > (crystals_left(d) ? DG_ARROW_KEEP : 0) &&
            head_dist(in, en) < DG_DRAGON_RANGE && in->steps - d->shot_at > DG_DRAGON_GAP)
        {
            /* the dragon in view, not in reach, no crystal to shoot: an arrow at it (led) */
            phase(d, DP_SHOOT);
            d->target = DG_AT_DRAGON;
            shoot(d, in, s, en, a);
            break;
        }
        if (DG_LATE(in, en, DG_WALKIN) && DG_OUT(in) && !en->dragon_dying)
        {
            /* lane/dleg (dragon_late walkin): out past DG_WAIT_R with the crystals gone: a box out here left (the pit's
             * walk in takes it to the centre's next step), else the walk in, the sword in hand */
            if (d->pitted > 0) d->pitted = 0;
            hold_slot(s, a, hot(s, SWORDS, 5));
            walk_to(d, in, s, a, 0.5, 0.5);
            break;
        }
        if (en->dragon_id && en->dragon_view && !en->dragon_dying)
        {
            /* the dragon in view: the look on it, the sword in hand, waiting for its pass */
            double yaw, pitch;
            look_at(in->x, in->y + 1.62, in->z, en->head[0], en->head[1], en->head[2], &yaw, &pitch);
            look(a, yaw, pitch);
            if (hold_slot(s, a, hot(s, SWORDS, 5)) && DG_BLOCK && head_dist(in, en) < DG_BLOCK_R)
                a->hold |= 1u << PK_USE;    /* lane/dragon2: blocking while it comes */
            break;
        }
        if (d->pitted > 0 && wait_d2(in) > DG_BOX_R * DG_BOX_R && in->steps - d->dragon_seen > DG_UNBOX)
        {
            /* boxed in on the way (the dragon came), the dragon gone a while: on toward the centre, the walk digging
             * out through the box's side */
            d->pitted = 0;
            sweep(d, in, a);
            break;
        }
        if (d->healed && d->snipe < 0 && d->resnipes < DG_RESNIPES && d->sweep > 0 && d->sweep % DG_SWEEP == 0 && !d->saw_c && !d->tw &&
            in->on_ground && blocks(s) > 0 && hot_block(s) >= 0 && can_shoot(in, s))
        {
            /* the dragon healing and a whole look around shows no crystal: one stands where only height sees it, so a
             * snipe again (a tower up, the look around from each block, down after a hit or when the dragon comes) */
            ++d->resnipes;
            d->healed = 0;
            d->snipe = 2;
            d->snipe_hits = d->crystals_hit;
            d->sbase = in->y;
            d->stowers = 0;
            d->sweep = 0;
            d->saw_c = 0;
            phase(d, DP_SNIPE);
            sweep(d, in, a);
            break;
        }
        if (d->pitted > 0 && d->dragon_seen > 0 && in->steps - d->dragon_seen > DG_LOST && !d->tw)
        {
            /* lane/dragon2: the dragon out of view this long (at render distance 4 it stops where its chunks are not
             * all loaded, and its copy goes past the watched ones): out of the hole toward where it was last seen */
            d->pitted = 0;
            phase(d, DP_FOLLOW);
            break;
        }
        if (d->pitted > 0)
        {
            /* in the box: looks around (the crystals come into view as the look turns; the dragon comes); a whole
             * look around with crystals left and none in view: a block higher for the view (walls 2 high hide the
             * low pillars), at most DG_VIEW_TOWERS times */
            if (d->tw || (d->sweep > 0 && d->sweep % DG_SWEEP == 0 && !d->saw_c && en->held > 0 &&
                          d->vtowers < DG_VIEW_TOWERS && blocks(s) > 0))
            {
                if (!d->tw) ++d->vtowers, d->tw = -1, d->tw_age = 0;
                if (tower_step(d, in, s, a)) d->tw = 0;
                ++d->sweep;
                break;
            }
            if (d->sweep % DG_SWEEP == 0) d->saw_c = 0;
            sweep(d, in, a);
            break;
        }
        if (in->steps < d->walk_until || d->sweep > 0)
        {
            if (d->sweep >= DG_SWEEP)
            {
                d->sweep = 0;
                d->walk_until = in->steps + DG_WALK;
            }
            else if (in->steps >= d->walk_until)
            {
                sweep(d, in, a);
                break;
            }
            walk(d, in, s, a);
            break;
        }
        sweep(d, in, a);
        break;
    }
    }
    /* never the crosshair on an enderman: a look that would be is lowered to the ground at the feet */
    if (a->look_mode == 1 && ender_in_look(in, en, a->look[0], a->look[1])) a->look[1] = 75.0f;
    d->px = in->x, d->pz = in->z, d->py = in->y;
    d->use_prev = (a->hold >> PK_USE) & 1;
    return 0;
}

/* the books' cause (vecdragon.h DK_*) of a CombatTracker entry: its family and name, and the hit before the fall */
static int dk_of(int dt, int name, int fall_dt, int fall_name)
{
    if (dt == DT_OUT_OF_WORLD) return DK_VOID;
    if (dt == DT_MOB && name == CM_NAME_DRAGON) return DK_DRAGON;
    if (dt == DT_MOB && name == HK_ENDERMAN) return DK_ENDER;
    if (dt == DT_FALL) return fall_dt == DT_MOB && fall_name == CM_NAME_DRAGON ? DK_FALL_HIT : DK_FALL;
    if (dt == DT_EXPLOSION || dt == DT_EXPLOSION_PLAYER) return DK_BLAST;
    if (dt == DT_ARROW) return DK_ARROW;
    if (dt == DT_STARVE) return DK_STARVE;
    return DK_OTHER;
}

void dragon_books(struct vec_dragon *d, const struct dragon_in *in, const struct neth_rl_end *en, int64_t te)
{
    if (en->on && en->dragon_id)
    {
        if (d->dragon_min < 0.0f || en->dragon_health < d->dragon_min) d->dragon_min = en->dragon_health;
        if (en->dragon_dying && !d->saw_death)
        {
            d->saw_death = 1;
            d->dead_step = in->steps;
            d->dx = en->pos[0], d->dz = en->pos[2];
            if (d->kill_step < 0) d->kill_step = te;
        }
    }
    if (in->dim != 1 && d->saw_death && d->exit_step < 0 && !in->dead) d->exit_step = te;
    if (en->on && in->dim == 1)
    {
        /* lane/dragon2: the push (a step's horizontal move past DG_PUSH_V), every hit's health by its cause */
        double hx = in->x - d->bx, hz = in->z - d->bz;
        if (d->prev_hp > 0.0f && hx * hx + hz * hz > DG_PUSH_V * DG_PUSH_V && hx * hx + hz * hz < 100.0)
        {
            if (d->push_step <= 0 || te - d->push_step > 5) ++d->pushes;
            d->push_step = te;
        }
        d->bx = in->x, d->bz = in->z;
        if (d->prev_hp > 0.0f && en->sv_health < d->prev_hp - 1e-3f)
        {
            int k = dk_of(en->cm_dt, en->cm_name, en->fall_dt, en->fall_name);
            d->dmg[k] += d->prev_hp - en->sv_health;
            ++d->hits[k];
        }
        d->prev_hp = en->sv_health > 0.0f ? en->sv_health : d->prev_hp;
    }
    if (in->dead && !d->was_dead && in->dim == 1)
    {
        d->d_cause = 1 + dk_of(en->cm_dt, en->cm_name, en->fall_dt, en->fall_name);
        d->d_pushed = d->push_step > 0 && te - d->push_step <= DG_PUSH_WINDOW;
        d->d_phase = d->on ? d->phase : 0;
        d->d_dt = en->cm_dt, d->d_name = en->cm_name;
        d->d_fall_dt = en->fall_dt, d->d_fall_name = en->fall_name, d->d_fall_len = en->fall_len;
        d->d_x = (float)in->x, d->d_y = (float)in->y, d->d_z = (float)in->z;
        d->d_head = en->dragon_id ? (float)head_dist(in, en) : -1.0f;
        d->d_food = in->food;
        /* what was near, first that holds: below the island, the dragon's head within 10 blocks, an enderman within 4
         * (seen or not: the books' ender_near), else other */
        int c = in->y < 0.0                                                 ? 0
                : en->dragon_id && head_dist(in, en) < 10.0                 ? 1
                : en->ender_near > 0.0f && en->ender_near < 4.0f            ? 2
                                                                            : 3;
        ++d->died[c];
        if (d->death_step < 0) d->death_step = te;
    }
    d->was_dead = in->dead;
}
