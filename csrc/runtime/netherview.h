/* lane/nether: what the player sees of the Nether's blocks, for NETH_VEC_NETHER (mods/macros/nether.c).
 *
 * The cast's seen record (rlbind.h neth_rl_state seen) holds nether brick in the Nether, but not the fortress's fences,
 * its blaze spawner, the portal, lava or fire, so the Nether macro has its own look at the client world (what the
 * player has been sent, never the server's), the way vis_scan finds the livings in view: a cell within `radius` blocks
 * of the eye and 75 degrees of the look whose centre the eye sees, the line of sight's first block the cell itself
 * (raytrace_blocks over the client world, liquids passed; a lava or fire cell: no block before its centre). The
 * nearest seen cell of each kind. Linux and CUDA builds alike (no device code). */
#ifndef NETHERITE_RUNTIME_NETHERVIEW_H
#define NETHERITE_RUNTIME_NETHERVIEW_H

#include <stdint.h>

enum { NV_SPAWNER, NV_FENCE, NV_BRICK, NV_PORTAL, NV_LAVA, NV_KINDS };

struct netherview_hit {
    int32_t n;                  /* the cells of the kind in the cone (before the line of sight; 0: none) */
    int32_t x, y, z;            /* the nearest one seen */
    float d;                    /* its distance from the eye, -1 none seen */
};

struct pipe;

/* env id's client world from the eye (ex, ey, ez) looking along (yaw, pitch) degrees: the nearest seen cell of each
 * kind in mask (bit NV_*) within radius. 0, -1 no client world */
int netherview_scan(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int radius,
                    int mask, struct netherview_hit out[NV_KINDS]);

/* the block id (meta into *meta) at a cell of env id's client world; -1 where its chunk is not loaded. Only for a
 * cell the player sees: the one the feet stand in, or a cell the crosshair or a scan named */
int netherview_block(struct pipe *p, int id, int x, int y, int z, int *meta);
#ifdef NT_TRACE_FIRE
struct netherview_fire_ent { int kind; double x, y, z; };
int netherview_fire_near(struct pipe *p, int id, double x, double y, double z,
                         struct netherview_fire_ent *out, int max);
#endif

/* lane/nether2: 1 when the eye sees cell (x, y, z): the line from the eye to its centre meets no block of env id's
 * client world before the cell (liquids and blocks without a box passed; a solid cell's own face ends it) */
int netherview_seen(struct pipe *p, int id, double ex, double ey, double ez, int x, int y, int z);

/* lane/nether3: the camera's far view, a fan of lines of sight (what the screen shows out to `range` blocks): nyaw x
 * npitch rays over hfov x vfov degrees round the look, each to its first block (liquids and blocks without a box
 * passed, as the eye sees through lava's surface to nothing: the lava sea's top is reported as a floor of lava). Per
 * ray hit: a fortress block (brick, fence, its stairs, the spawner) kept as the nearest of each kind; a floor (the hit
 * is a block's top face, or lava's surface met on the way, with two open cells over it) kept as a candidate */
#define NV_FLOORS 64
struct netherview_floor { int32_t x, y, z; int16_t lava, ray; float d; int32_t block; };
struct netherview_fan {
    struct netherview_hit fort[NV_KINDS];       /* NV_SPAWNER, NV_FENCE, NV_BRICK (n: rays that met it) */
    struct netherview_hit top;                  /* the highest fortress block met (brick, fence or stairs) */
    int nfloor;
    struct netherview_floor floor[NV_FLOORS];   /* the floors met, in ray order */
    int nlava;                                  /* rays whose line crossed lava's surface first */
};
int netherview_fan(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int range,
                   int nyaw, int npitch, float hfov, float vfov, struct netherview_fan *out);

/* lane/nether4: the ghasts and the fireballs (a ghast's large, a blaze's small) on the screen: env id's client
 * entities within NV_GHAST_R (ghasts) or NV_BALL_R (fireballs) of the eye and NV_THREAT_CONE degrees of the look, the
 * line of sight to each clear of the client world's blocks (vis_scan's rule, at a ghast's 64-block reach). The nearest
 * ghast; the NV_BALLS nearest fireballs with their last tick's motion. 0, -1 no client world */
#define NV_GHAST_R 64.0
#define NV_BALL_R 24.0
#define NV_BLAZE_R 32.0         /* a blaze shoots from under 30 */
#define NV_THREAT_CONE 75.0
#define NV_BALLS 4
struct netherview_threats {
    int32_t nghast;
    float ghast_d, gx, gy, gz;  /* the nearest ghast's distance (-1 none) and its middle */
    int32_t nblaze, blaze_id;
    float blaze_d, bx, by, bz;  /* the nearest blaze within NV_BLAZE_R (-1 none) and its middle */
    int32_t nball;
    struct { int32_t id, large; float x, y, z, mx, my, mz, d; } ball[NV_BALLS];
};
int netherview_threats(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch,
                       struct netherview_threats *out);
/* lane/rods2 (nether_eye_strike): the nearest blaze on the screen (client world, alive, within r of the eye and in the
 * NV_THREAT_CONE about the look, as the vis list tests them) whose middle (0.5 of its height: the vis list's ray) is
 * hidden by a block and whose eye (0.85 of its height, where its own sight of the player starts) is not: its id, its
 * eye's place and distance; the aim (mx, my, mz): the eye's place, or with margin the clear point of a grid over its
 * box with the most clear neighbours. 1 found, 0 none, -1 no client world */
struct netherview_eye { int32_t id; float x, y, z, d, mx, my, mz; };
int netherview_eye_blaze(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, double r,
                         int margin, struct netherview_eye *out);
/* lane/rods2 (nether_mouth_cover): the nearest blaze on the screen (the vis cone about the look) whose eye (0.85 of its
 * height) is farther than rmin and within rmax of the player's eye with a clear ray, the ray leaving the player's feet
 * and head cells through the cell over the head (the camp's hole: one block there shuts it). 1 found, 0 none, -1 no
 * client world */
int netherview_mouth_blaze(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, double rmin,
                           double rmax, struct netherview_eye *out);
/* Client-known blazes with a clear block ray from the eye, in every look direction; -1 without a client world. */
int netherview_exit_blaze_los(struct pipe *p, int id, double ex, double ey, double ez);
/* lane/rod (nether_oracle): PRIVILEGED, never a player's view and never in the benchmark's numbers: from env id's own
 * fortress map (the Nether provider's structure starts, populate_hell's hmap) the fortress piece nearest (x, z)
 * horizontally (box minX minY minZ maxX maxY maxZ, its distance), its topmost nether brick nearest the player where the
 * server has the chunk (has_brick: the throne piece's when there is one), and the nearest throne piece (the blaze
 * spawner's room, tbox: sp its spawner block from the server world, has_sp, else the box's middle 5 up). 0, -1 no
 * server */
struct netherview_oracle {
    int32_t nstarts, has_piece, has_brick, has_throne, has_sp;
    int32_t box[6], tbox[6], brick[3], sp[3];
    float piece_d, throne_d;
};
int netherview_oracle(struct pipe *p, int id, double x, double y, double z, struct netherview_oracle *out);
/* The nearest candidate fortress start chunk from the world seed, without generating or loading chunks: 1 found,
 * 0 none in the bounded region search, -1 no env. Privileged like netherview_oracle. */
int netherview_oracle_start(struct pipe *p, int id, double x, double z, int32_t chunk[2]);
/* Closest generated blaze-throne centre among starts within 400 blocks; only piece boxes are generated. */
int netherview_oracle_throne(struct pipe *p, int id, double x, double z, int32_t target[2]);
/* Closest enclosed fortress piece's standing cell and kind (x, y, z, F_*), from generated piece boxes. */
int netherview_oracle_entry(struct pipe *p, int id, double x, double y, double z, int32_t target[4]);
/* PRIVILEGED too: the nether portal block nearest (x, y, z) within r (a cube) in env id's server Nether, the lowest of
 * its column, into cell; 1 found, 0 none */
int netherview_oracle_portal(struct pipe *p, int id, int x, int y, int z, int r, int32_t cell[3]);
/* PRIVILEGED too: env id's client blazes (alive, not dying) within r of the eye, through walls and out of view, the
 * nearest first, at most max; their count */
struct netherview_blaze { int32_t id; float x, y, z, d; };
/* lane/rods (books only, PRIVILEGED: through walls): env id's client blaze eid (the fight lock), its centre's distance
 * from the eye, its height over the eye, and a clear block ray from the eye to it; 1 found */
int netherview_blaze_lock(struct pipe *p, int id, int eid, double ex, double ey, double ez, float *d, float *dy, int *los);
/* lane/rods2 (books only, PRIVILEGED: through walls): env id's client blazes (alive, not dying) within r of the eye
 * (ex, ey, ez) into *n, and of them the ones with a clear block ray from their eye (0.85 of their height) to the
 * player's eye (EntityLivingBase.canEntityBeSeen's ray) into *los, the nearest of those's distance (eye to eye; -1
 * none) and height over the player's eye into *ld, *ldy, and of the line ones those inside netherview_threats's view
 * cone (NV_THREAT_CONE about the look yaw, pitch: the ones the cover and the lock can see) into *cone; of the line
 * ones those whose ray (eye to eye) leaves the player's feet and head cells through the cell over the head (the camp's
 * hole: one block there shuts them) into *mouth, and the nearest one's first such cell, relative to the feet's cell,
 * into cell (-99 none) */
void netherview_blaze_lines(struct pipe *p, int id, double ex, double ey, double ez, double r, float yaw, float pitch,
                            int *n, int *los, float *ld, float *ldy, int *cone, int *mouth, int cell[3]);
/* lane/rods2 (books only, PRIVILEGED: the server's entities): env id's small fireballs (IE_SMALL_FIREBALL, alive) within
 * r of the eye: their entity ids and places, at most max; their count */
struct netherview_fireball { int32_t id; float x, y, z; };
int netherview_fireballs(struct pipe *p, int id, double ex, double ey, double ez, double r,
                         struct netherview_fireball *out, int max);
/* lane/rods2 (books only): a clear block ray in env id's client world between two points; 1 clear, 0 a block, -1 none */
int netherview_clear(struct pipe *p, int id, double x0, double y0, double z0, double x1, double y1, double z1);
/* PRIVILEGED too: the nearest item entity of id `item` (the server's item lists) within r of the eye, into xyz; 1
 * found */
/* PRIVILEGED: the server's block at (x, y, z) in env id's Nether (-1 its chunk not held) */
int netherview_oracle_block(struct pipe *p, int id, int x, int y, int z);
int netherview_oracle_item(struct pipe *p, int id, double ex, double ey, double ez, double r, int item, float xyz[3]);
/* Passive diagnostic: every live server item entity of one item id, capped at max. Does not drive an act. */
struct netherview_item { int32_t id, age, count; float x, y, z; };
int netherview_oracle_items(struct pipe *p, int id, int item, struct netherview_item *out, int max);
/* PRIVILEGED too: a walk over env id's server Nether from the feet's cell (fx, fy, fz) to a cell within `near`
 * (horizontally) of (tx, ty, tz) and NV_PATH_GOAL_DY of ty: the cheapest way (Dijkstra) over cells the feet may stand
 * in (a floor under, the feet's and head's cells open, no lava or fire in or beside them), four ways, a step up of 1
 * (head room over the jump) or a drop of up to 3 down an open column, each 1; with flags NV_PATH_BUILD also a level
 * cell made walkable, its feet and head blocks dug (NV_PATH_DIG each) and its floor placed over air (NV_PATH_BRIDGE),
 * no lava, water or fire beside or over it; in a box NV_PATH_PAD past the two ends (NV_PATH_HALF at most) and
 * NV_PATH_DY over and under them. No such cell (the target past the chunks the server holds, its render distance):
 * the way to the reached cell nearest the target, when NV_PATH_GAIN nearer than the start. The cells after the start
 * in order into out (max at most); their count, 0 none */
#define NV_PATH_GAIN 4.0
#define NV_PATH_BUILD 1
#define NV_PATH_DIG 2
#define NV_PATH_BRIDGE 3
#define NV_PATH_PAD 24
#define NV_PATH_HALF 96
#define NV_PATH_DY 10
#define NV_PATH_GOAL_DY 3
#define NV_PATH_EXACT 4         /* a flag: the goal's cell at ty itself (not within NV_PATH_GOAL_DY) */
/* a flag: the tunnel (lane/rod, the way home): through solid blocks, a dug block NV_TUN_DIG, a cell open to the air
 * NV_TUN_OPEN (a walk in the open, a bridge dearer still), a cell closed on its four sides and over the head
 * NV_TUN_DIG, a cell under NV_TUN_LOW NV_TUN_LOWC more (the lava sea's level), steps up and down dug as well */
#define NV_PATH_TUNNEL 8
#define NV_PATH_NOBRIDGE 16     /* a flag (lane/supply, dig_bridge): the tunnel lays no floor over air */
#define NV_TUN_DIG 1
#define NV_TUN_OPEN 30          /* tunp-9bt: 10 took the cells east of the pocket, open to the room */
#define NV_TUN_LOW 34
#define NV_TUN_LOWC 4
int netherview_oracle_path(struct pipe *p, int id, int fx, int fy, int fz, int tx, int ty, int tz, double near,
                           int flags, int16_t (*out)[3], int max);
/* lane/rod (armor_kit): the same walk over env id's server world of dimension dim (0 the overworld, -1 the Nether) */
int netherview_oracle_path_dim(struct pipe *p, int id, int dim, int fx, int fy, int fz, int tx, int ty, int tz,
                               double near, int flags, int16_t (*out)[3], int max);
int netherview_oracle_blazes(struct pipe *p, int id, double ex, double ey, double ez, double r,
                             struct netherview_blaze *out, int max);

void netherview_debug_ents(struct pipe *p, int id, double x, double y, double z, double r);   /* NT_TRACE builds */

#endif
