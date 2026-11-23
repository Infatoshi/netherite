/* walkto (lane/pearlsee, 3rd pass): a path for the player, PRIVILEGED. The engine's own pathfinder (engine/pathfind.h:
 * vanilla's PathFinder, what a zombie walks to the player with) searched from the player's feet to a cell for an entity
 * of the player's size (0.6 wide, 1.8 tall), over the server world's blocks: what no screen shows. For options that are
 * labelled privileged (pearl_oracle; lane/rod's fortress and way home, lane/chain's water and lava), never a default.
 *
 * A macro asks with neth_rl_walkto_ask (rlbind.h; the cell, the search's range) and reads the answer in its env's
 * neth_rl_far (wpath) from the next step on: the search runs at the step's end in the env's own context (rlbind.c, where
 * the census runs), when an ask is newer than the answer. mods/macros/walkto.c follows a path with the walk and jump
 * keys (walk_to). The search reads blocks and writes nothing (pathfind.c's header): a tick's rows are the same with or
 * without it. */
#ifndef NETHERITE_RUNTIME_WALKTO_H
#define NETHERITE_RUNTIME_WALKTO_H

#include <stdint.h>

#define WALKTO_PTS 96           /* the path's first points kept (a search of range 48 rarely has more) */

struct walkto_ask {
    int32_t seq;                /* bumped by each ask; 0 none yet */
    int32_t x, y, z;            /* the cell (the feet's) */
    float range;                /* PathFinder's maxDistance (the target must be within it) */
    int32_t rel;                /* 1: x, y, z an offset from the feet's cell when the search runs (vec_bench --walk-ask) */
};

struct walkto_path {
    int32_t seq;                /* the ask it answers */
    int32_t n;                  /* its points (-1: no path; 0: the start is the cell) */
    int32_t len;                /* the whole path's points (n is at most WALKTO_PTS) */
    int16_t pts[WALKTO_PTS][3]; /* each point's cell (the entity box's min corner: x, the feet's y, z) */
};

/* a follower's state (mods/macros/walkto.c walk_to), one in each macro's env state that walks */
struct walkto_st {
    int32_t seq, have;          /* the ask it made; the answer it holds */
    int32_t x, y, z;            /* the cell asked for */
    int64_t asked, prog_t;      /* the step of the ask; of the last progress */
    double px, pz;              /* where the last progress was */
    int32_t k;                  /* the point walked to */
    struct walkto_path path;
};

struct world;

/* the search: from an entity of the player's size with its feet at (px, py, pz) to cell (x, y, z) over w */
void walkto_search(struct world *w, double px, double py, double pz, const struct walkto_ask *ask,
                   struct walkto_path *out);

#endif
