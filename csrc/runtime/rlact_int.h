/* rlact.c's part of rlbind.c: each env's compiler state (outside the tick:
 * the binding's own memory, one per env) and the pool's act hook. */
#ifndef NETHERITE_RUNTIME_RLACT_INT_H
#define NETHERITE_RUNTIME_RLACT_INT_H

#include <stdint.h>
#include <stdio.h>

#include "pool.h"
#include "rlact.h"

enum { RLACT_QUEUE = 32 };

/* one queued action and its progress */
struct rlact_item {
    struct pool_gui_op op;
    int window;                 /* the window it was given under (-100: none open yet, bound at its first op) */
    int phase;                  /* the action's own step */
    int from, to;               /* resolved slots (-1: not yet) */
    int n;                      /* a craft's count (0: not yet resolved) */
    int clicks, stuck;          /* ops given; ops in a row that made no progress */
    int fuel_item, fuel_need;   /* a furnace craft's fuel (-1: none chosen) */
};

struct rlact_env {
    struct rlact_item q[RLACT_QUEUE];
    int head, n;
    int64_t tick;               /* ticks the hook saw since the reset */
    int64_t last_t;             /* the tick of the last op given */
    int64_t emitted, refused, failed, done;
    FILE *log;
};

int rlact_on(const struct nwrl_actconf *c);
int rlact_conf_ok(const struct nwrl_actconf *c);
void rlact_reset(struct rlact_env *e);
/* before a tick of the env (its env current): first is the step's first
 * tick (its ops join the queue); a gets the tick's op, if any */
void rlact_tick(const struct nwrl_actconf *c, struct rlact_env *e, struct session *ss, int first, struct pool_act *a);
void rlact_info(const struct nwrl_actconf *c, const struct rlact_env *e, struct session *ss, struct nwrl_actinfo *out);

#endif
