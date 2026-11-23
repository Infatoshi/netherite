/* lane/rawskill: the task of neth_vec_opts.task_item (vecenv.h), one physical skill on the raw controls: its settings
 * and each env's books. vecenv.c includes vecenv_task.c, which has the logic. */
#ifndef NETHERITE_RUNTIME_VECENV_TASK_H
#define NETHERITE_RUNTIME_VECENV_TASK_H

#include <stdint.h>

struct task_cfg {
    int item, block, seen;      /* the item whose pickup ends the episode, the block shaped on, its neth_rl_state seen
                                 * index (-1: none, no distance term) */
    uint32_t keys;              /* the keys the policy may press (bit j key j) */
    double win, death, look, crack, dist;
    int shape;                  /* 0 each term once a unit, 1 the potential (neth_vec_opts.task_shape) */
    int sticky;                 /* neth_vec_opts.task_sticky: an attack press latches on the block under the crosshair
                                 * (1 any block, 2 the task's block only) */
    int hint;                   /* lane/rawsearch neth_vec_opts.task_hint: the scan box's half width (0 off) */
    int count;                  /* lane/rawsearch neth_vec_opts.task_count: the pickups that end the episode (1) */
};

struct task_env {
    int64_t n0;                 /* the item's count at the episode's first step */
    int64_t nlast;              /* lane/rawsearch (count > 1): the most of it held so far, paid up to there */
    int looked, crack;          /* the crosshair was on the block; the highest crack stage drawn on it so far */
    float dmin;                 /* the least distance to a seen cell of the block so far (0: none seen yet) */
    int done;                   /* the success (1) or a death (2): the episode ends with this step */
    int has_cell;               /* shape 1: a cell of the block seen this episode, the last one in cx, cy, cz */
    int32_t cx, cy, cz;
    double phi;                 /* shape 1: the potential at the last step */
    int mo_ok;                  /* sticky: the crosshair was on a block at the last step, its cell mx, my, mz, its id mb */
    int32_t mx, my, mz, mb;
    int latch;                  /* sticky: the attack held on the cell lx, ly, lz until the crosshair leaves it */
    int32_t lx, ly, lz;
};

#endif
