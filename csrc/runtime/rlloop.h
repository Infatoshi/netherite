/* The trainer loop's per-env work in C (lane/looppy), beside rlbind.h: what
 * mcsr-speedrun's overlapped GRPO loop (mcsr/rl/arollout.py) did in Python
 * for every env of a group, every step, on the trainer's one thread: the
 * reward and the episode metrics from a step's results (mcsr/rl/reward.py
 * Reward.step, rollout.py Runner._metrics), the policy's sampled keys and
 * camera bins to acts (actmap.py to_acts), and the macros' common case
 * (macros.py Macros.apply: which envs a macro holds or may start, the
 * respawn, the mine lock: its start, aim, best tool and end). The rare
 * macros (surface, screen, loot, craft) stay Python's: an env that starts or
 * runs one, and a mine lock whose reach is within a hair of its 5 blocks
 * (Python's math.dist decides it), comes back marked for a Python visit.
 *
 * Outside the tick: no engine state, only the arrays the trainer owns
 * (numpy, by pointer) and the results. Every value is computed as numpy and
 * Python compute it (float32 sums in numpy's pairwise order, the products
 * and casts in the same order and width, the angles by atan2 and the same
 * radians-to-degrees constant), so the acts, rewards and records equal the
 * Python loop's byte for byte (mcsr-speedrun scripts/nw_loop_replay.py on a
 * captured stream, scripts/nw_async_check.py on the live loop). The calls
 * hold no lock and touch only envs lo .. lo + k - 1: a Python caller gets
 * the GIL back while they run (ctypes). */
#ifndef NETHERITE_RUNTIME_RLLOOP_H
#define NETHERITE_RUNTIME_RLLOOP_H

#include <stdint.h>

#include "rlbind.h"

#define NWRL_LOOP_TERMS 8       /* items, looted, logs, chest, progress, ach, death, milestone (Reward.step's order) */
#define NWRL_LOOP_M 18          /* Runner.M: x0 z0 lx lz path still steps net60 deaths ret, then t_ of
                                 * items logs chest progress ach death milestone looted */

/* The tables (mcsr's own, item and block ids 0 .. 4095), the reward's
 * weights and the macros' switches. */
struct nwrl_loop_tabs {
    const float *value;         /* reward.values() */
    const uint64_t *msbit;      /* the milestone bits an item sets (reward.MILESTONES) */
    const uint8_t *lockable;    /* macros.LOCKABLE */
    const uint8_t *hand;        /* macros.HAND */
    const uint8_t *craft;       /* macros.CRAFT_ITEMS */
    const int64_t *picky;       /* the pickaxe tier a block needs to drop (0: none) */
    const int64_t *pick;        /* a pickaxe item's tier (0: not one) */
    const int64_t *tool_of;     /* _best_tool's table for a block: 0 axes, 1 shovels, 2 pickaxes, -1 none */
    const int64_t *tool;        /* [3][4096] a tool's tier in each table */
    const int64_t *cnt;         /* [5][4096] the craft plan's counts: logs, planks, sticks, tables, cobblestone */
    const uint32_t *keybit;     /* [nkeys] the hold bits a policy key sets */
    const double *camdeg;       /* [41] a camera bin's degrees (pairs.mulaw_center) */
    double w[NWRL_LOOP_TERMS];  /* the objective's weights */
    double near5;               /* a lock's reach within this of 5 blocks goes to Python (math.dist) */
    int32_t nkeys, key_drop;
    int32_t on_surface, on_loot, on_mine, on_craft;
};

/* The trainer's per-env arrays, n envs each (the macros' are one group's
 * Macros object's) */
struct nwrl_loop_env {
    float *prev_val;
    int64_t *prev_logs, *prev_opened;
    uint64_t *prev_ach;
    int64_t *prev_near;
    float *prev_dist;
    uint64_t *got;
    int32_t *prev_screen;
    uint8_t *fresh;
    double *m[NWRL_LOOP_M];
    int64_t *te;                /* steps into the episode (harvest counts it) */
    float *rew;                 /* [n, ep_len]: REW[e, te - 1] = the step's reward */
    uint8_t *mask;              /* [n, ep_len]: MASK[e, te] = the policy drove the step (no macro, or its attack
                                 * chose the lock) */
    int64_t *macro_n;           /* [n, 6]: steps by the macro kind driving them */
    int64_t ep_len;
    uint32_t *prev_hold;
    int8_t *kind;               /* macros: the kind each env runs (0 none) */
    int64_t *mage, *mpos, *mblock, *steps;   /* the mine lock's age, block [n, 3] and id; Macros.steps */
};

/* the visits Python owes an env after nwrl_loop_harvest */
enum {
    NWRL_V_DEAD = 1,            /* respawned: a Python macro it ran is dropped */
    NWRL_V_START = 2,           /* may start a macro: Macros._start, then its first _drive */
    NWRL_V_DRIVE = 4,           /* runs a Python macro: Macros._drive */
    NWRL_V_MINED = 8,           /* its lock ended on a broken block: the "mined" event */
    NWRL_V_MINE = 16,           /* its lock's reach is next to 5 blocks: Python drives this step */
};

/* Results R[0 .. k) of envs lo .. lo + k - 1 (in id order): the reward
 * (written to rew[e, te - 1] when te > 0) and the metrics. */
void nwrl_loop_arrive(const struct nwrl_loop_tabs *T, const struct nwrl_loop_env *E, const struct nwrl_result *R,
                      int k, int lo);

/* The same envs' next acts from the policy's packed step (packed[q *
 * stride ..]: the camera's pitch and yaw bins, then the nkeys keys, as
 * floats: mcsr arollout.AStepper's pinned buffer), the macros' edits on top:
 * acts[k], each env's macro kind (kind[k]), whether the policy's attack
 * started a lock this step (chosen[k]) and the visits Python owes
 * (visit[k]); how many envs need one. Updates prev_hold, Macros.steps, the
 * macro state, te (+1) and, for the envs not visited, macro_n and mask (a
 * visited env's are its visit's). -1, nothing changed: a camera bin outside
 * 0 .. 40. */
int nwrl_loop_harvest(const struct nwrl_loop_tabs *T, const struct nwrl_loop_env *E, const struct nwrl_result *R,
                      int k, int lo, const float *packed, int stride, struct nwrl_act *acts, int8_t *kind,
                      uint8_t *chosen, uint8_t *visit);

#endif
