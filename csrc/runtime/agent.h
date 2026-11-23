/* The agent form of an act (pool.h struct pool_act, java Act.java's agent
 * form) made into the engine's act for one tick. */
#ifndef NETHERITE_RUNTIME_AGENT_H
#define NETHERITE_RUNTIME_AGENT_H

#include <stddef.h>

#include "../engine/player.h"

struct pool_act;
struct session;

struct agent_state {
    int refused_step;       /* the step's first act was refused whole */
    int ops_refused;        /* gui ops dropped over the step */
    struct act_agent agent; /* the tick's keys and ops, resolved in the tick */
};

void agent_begin(struct agent_state *ag);
/* The act for one tick of src (hold_only: a later tick of a one-act step,
 * Act.holdOnly). rd > 0: the client's render distance option (the
 * observation's rd); a step's first act carries it as an opts step (Act's
 * opts rd, as play's does when the slider moves) whenever GameSettings
 * holds another, and the server's view distance follows it at the tick's
 * tail (IntegratedServer.tick). 0 when the step is refused whole (err says
 * why). */
int agent_act(struct agent_state *ag, struct session *ss, const struct pool_act *src, int hold_only, int rd,
              struct act *a, char *err, size_t n);
/* after the tick: its refused ops counted */
void agent_end_tick(struct agent_state *ag);

#endif
