/* See blockwl.h. */
#include "blockwl.h"
#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define B (&nw_env->bwl)

#ifdef NETHERITE_WL_DEPTH
struct wl_depth wl_depth;

__attribute__((destructor)) static void wl_depth_report(void)
{
    fprintf(stderr, "wldepth: bwl_frames=%d bwl_emit=%d flow_cost=%d nbt=%d potion=%d riding=%d envstack=%d "
            "tick_stack=%d\n", wl_depth.bwl_frames, wl_depth.bwl_emit, wl_depth.flow_cost, wl_depth.nbt, wl_depth.potion,
            wl_depth.riding, wl_depth.envstack, wl_depth.tick_stack);
}
#endif

static void bwl_fail(const char *what)
{
    fprintf(stderr, "blockwl: %s (stack %d, emitted %d)\n", what, B->sp, B->nemit);
    abort();
}

void bwl_emit_(int kind, struct world *w, int x, int y, int z, const int *a, void *p)
{
    struct bwl *b = B;

    if (!b->running) bwl_fail("an op emitted outside a run");
    if (b->nemit >= BWL_EMIT_CAP) bwl_fail("one step emitted more than BWL_EMIT_CAP ops");

    struct bwl_op *o = &b->emit[b->nemit++];

    o->kind = (uint8_t)kind;
    o->x = x;
    o->y = y;
    o->z = z;
    memcpy(o->a, a, sizeof o->a);
    o->w = w;
    o->p = p;
}

void bwl_order_fail(void)
{
    bwl_fail("a step read the world after it emitted a call");
}

static int step(struct bwl_frame *f)
{
    switch (f->op.kind)
    {
    case BWL_SET_BLOCK:
    case BWL_SET_META:
    case BWL_NOTIFY:
    case BWL_NEIGHBOR:
    case BWL_CHEST_ADDED:
        return world_bwl_step(f);

    case BWL_SCHED:
    case BWL_FALL_TAIL:
        return ticks_bwl_step(f);

    case BWL_LIQUID_TICK:
    case BWL_STATIC_TICK:
    case BWL_SPRING_FIZZ:
        return springs_bwl_step(f);

    default:
        return blockcb_bwl_step(f);
    }
}

/* Run the stack down to base. After each step its emitted ops go on top,
 * the first emitted on top, so they run in emission order before the
 * stepped frame resumes (or, when it finished, before its caller does). */
static void run(struct bwl *b, int base)
{
    ++b->running;

    while (b->sp > base)
    {
        struct bwl_frame *f = &b->stack[b->sp - 1];

        b->nemit = 0;

        if (step(f)) --b->sp;

        int n = b->nemit;

        if (n == 0) continue;
        if (b->sp + n > BWL_CAP) bwl_fail("stack overflow (BWL_CAP)");
        WL_DEPTH_NOTE(bwl_emit, n);

        for (int i = n - 1; i >= 0; --i)
        {
            struct bwl_frame *g = &b->stack[b->sp++];

            g->op = b->emit[i];
            g->pc = 0;
            g->i = 0;
        }

        WL_DEPTH_NOTE(bwl_frames, b->sp);
        b->nemit = 0;
    }

    --b->running;
}

void bwl_call(int kind, struct world *w, int x, int y, int z, const int *a, void *p)
{
    struct bwl *b = B;

    /* an entry reached from inside a step runs in place, which is program
     * order only while that step has not emitted anything yet */
    if (b->nemit != 0) bwl_fail("an entry point called after the step emitted");
    if (b->sp >= BWL_CAP) bwl_fail("stack overflow (BWL_CAP)");

    int base = b->sp;
    struct bwl_frame *g = &b->stack[b->sp++];

    g->op.kind = (uint8_t)kind;
    g->op.x = x;
    g->op.y = y;
    g->op.z = z;
    memcpy(g->op.a, a, sizeof g->op.a);
    g->op.w = w;
    g->op.p = p;
    g->pc = 0;
    g->i = 0;

    WL_DEPTH_NOTE(bwl_frames, b->sp);

    run(b, base);
}
