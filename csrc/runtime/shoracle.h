/* lane/shold (stronghold_oracle): PRIVILEGED, never a player's view and never in the benchmark's numbers. The
 * stronghold's portal room read from the world's own structure data (the seed's MapGenStronghold starts, rebuilt by the
 * engine's structure walk, or the server's structure map once it holds them), its frames, eyes, silverfish spawner and
 * End portal from the server's blocks, and a way over the server's overworld cells with walk, swim and dig moves (the
 * staircase down to the room: never a straight shaft, nothing liquid beside a dug cell, nothing that falls over it).
 * For lane/chain's checkpointed search (Elliot, 2026-11-10 1:20 am); the macro's acts stay a player's acts. */
#ifndef NETHERITE_RUNTIME_SHORACLE_H
#define NETHERITE_RUNTIME_SHORACLE_H

#include <stdint.h>

struct pipe;

struct shoracle_room {
    int32_t ok;                 /* a portal room found */
    int32_t box[6];             /* its piece's box: minX minY minZ maxX maxY maxZ */
    int32_t scx, scz;           /* the stronghold start's chunk */
    int32_t loaded;             /* the server holds every chunk of the box: the reads below are its blocks */
    int32_t nfr;                /* the end portal frames in the box (12 once loaded) */
    int32_t fr[12][4];          /* x y z, 1 an eye in it */
    int32_t has_sp, sp[3];      /* the mob spawner in the box */
    int32_t portal, pt[3];      /* end portal blocks in the box, one of them */
    int32_t floor_y;            /* the room's floor (the box's minY + 1: where the feet stand) */
};
/* The portal room of the stronghold nearest (x, z) in env id's overworld (the start's box from the seed when the server's
 * map does not hold it yet), its blocks read where loaded. 0, -1 none (no server, no stronghold) */
int shoracle_locate(struct pipe *p, int id, double x, double z, struct shoracle_room *out);
/* the server's blocks of an already located room again (frames, eyes, spawner, portal); 0, -1 none */
int shoracle_read(struct pipe *p, int id, struct shoracle_room *io);
/* env id's server overworld block at (x, y, z) (its meta into *meta), -1 not loaded */
int shoracle_block(struct pipe *p, int id, int x, int y, int z, int *meta);
/* the same in the server's dimension dim (0 the overworld, -1 the Nether, 1 the End) */
int shoracle_block_dim(struct pipe *p, int id, int dim, int x, int y, int z, int *meta);

/* The way from the feet's cell (fx, fy, fz) to a cell within `near` (horizontally) of (tx, ty, tz) and SHO_GOAL_DY of
 * ty (SHO_EXACT: ty itself), the cheapest (Dijkstra) over env id's server overworld in a box SHO_PAD past the two ends
 * (SHO_HALF at most) and SHO_DY over and under them. Cells the feet may be in: standing (a floor under, the feet's and
 * head's cells open) or swimming (the feet in water, the head in water or open), no lava or fire in or beside them.
 * Moves four ways: a step up of 1 (head room over the jump), level or a drop of up to 3 down an open column, and (unless
 * SHO_NODIG) a dug move to the cell one up, level or one down, every block in its way dug (no bedrock, obsidian,
 * spawner, silverfish block, frame or portal; nothing liquid or hot beside a dug cell; no sand, gravel or liquid over
 * the head), each dug block SHO_DIG more. Target out of reach (past the chunks the server holds): the way to the reached
 * cell nearest it when SHO_GAIN nearer than the start. The cells after the start into out (max at most); their count */
#define SHO_EXACT 1
#define SHO_NODIG 2
#define SHO_ANYY 4              /* the goal at any height in the box (a leg of the walk: over a hill, not into it) */
#define SHO_BRIDGE 8            /* also a level move onto a cell whose floor is air, the floor a block placed against a
                                 * solid side of it (not the one the player stands on): the portal room's gap between
                                 * the stairs and the frames */
#define SHO_BRIDGE_C 6          /* ... its cost */
#define SHO_GOAL_DY 3
#define SHO_PAD 16
#define SHO_HALF 72
#define SHO_DY 16
#define SHO_DIG 6               /* a dug block's cost in walked blocks (dirt by hand 15 ticks, a walked block about 4) */
#define SHO_SWIM 3
#define SHO_CAVE 8              /* a walked move into an open cell under SHO_CAVE_Y costs this more (a cave: its mobs,
                                 * its lava; the dug rock is the safer way) */
#define SHO_CAVE_Y 56
#define SHO_GAIN 4.0
int shoracle_path(struct pipe *p, int id, int fx, int fy, int fz, int tx, int ty, int tz, double near, int flags,
                  int16_t (*out)[3], int max);
/* the client world's ray from the eye (ex, ey, ez) to (tx, ty, tz) hits block (x, y, z) first: 1 */
int shoracle_sees(struct pipe *p, int id, double ex, double ey, double ez, double tx, double ty, double tz, int x, int y,
                  int z);
/* the nearest hostile (zombie, skeleton, creeper, spider, enderman, witch, silverfish, blaze, cave spider, slime) in env
 * id's client entity list within r of the eye (through walls, out of view), its middle into xyz; its entity id, 0 none */
int shoracle_hostile(struct pipe *p, int id, double ex, double ey, double ez, double r, float xyz[3]);
/* the cells a player walks through (no box), water, lava and fire, a floor to stand on */
int shoracle_open(int b);
/* the block a placed floor at (bx, by, bz) goes against for a player standing on (px, py, pz)'s floor: a solid side
 * (the far ones first, then under it), not the player's own floor; 1 found, into n */
int shoracle_bridge_face(struct pipe *p, int id, int bx, int by, int bz, int px, int pz, int n[3]);
int shoracle_hot(int b);
int shoracle_floor(int b);

/* lane/snowleg (snow_kit far, PRIVILEGED: the world generator's biomes, never a player's view). The nearest snowy
 * column to (x, z) in env id's overworld within r blocks: a biome generation cell (4x4 blocks) of ice plains (12), ice
 * mountains (13), cold beach (26), cold taiga (30), its hills (31), ice plains spikes (140) or cold taiga M (158) whose
 * eight neighbours are snowy too (inside the biome, not at its edge), its middle into *tx, *tz. 0, -1 none */
int shoracle_snow_column(struct pipe *p, int id, double x, double z, int r, int *tx, int *tz);
int shoracle_snow_height_column(struct pipe *p, int id, double x, double z, int r,
                                 int *tx, int *ty, int *tz);
/* lane/snowleg: the nearest log (17, 162) within r across and dy up or down of (x, y, z) in env id's server overworld
 * standing on dirt or grass (a trunk's foot), with no lava, fire or water beside it, not one of the nbad cells of bad
 * (given up); 0 (its cell into xyz), -1 none */
int shoracle_log(struct pipe *p, int id, int x, int y, int z, int r, int dy, const int (*bad)[3], int nbad, int xyz[3]);
/* PRIVILEGED search: nearest log with a safe adjacent standing cell among the server's currently loaded chunks.
 * out: log x,y,z then standing x,y,z; -1 if none. No chunk is generated by this query. */
int shoracle_loaded_log_stand(struct pipe *p, int id, int x, int y, int z, int down, int up, int out[6]);
/* PRIVILEGED post-Nether exit: an already loaded, sky-lit standing column with a dry four-block shore.
 * Here checks the player's feet; near picks the nearest such column in radius r, without generating chunks. */
int shoracle_dry_here(struct pipe *p, int id, int x, int y, int z);
int shoracle_dry_near(struct pipe *p, int id, int x, int z, int r, int out[3]);
int shoracle_sky_here(struct pipe *p, int id, int x, int eye_y, int z);
#ifdef PEARL_CENSUS_TRACE
int shoracle_monster_count_cap(struct pipe *p, int id, int *count, int *cap, int *eligible);
#endif

#endif
