/* The block-callback worklist: World.setBlock's callbacks without recursion.
 *
 * In vanilla a write runs the old block's breakBlock and the new block's
 * onBlockAdded, a flag-1 write tells the six neighbours (onNeighborBlockChange),
 * and each of those callbacks can write again, notify again, or (under
 * World.scheduledUpdatesAreImmediate) run a liquid's updateTick at once: one
 * mutually recursive cycle through setBlock, the notifications, every block
 * callback and the liquid flow. Here that cycle runs on an explicit stack of
 * frames in the environment (env.h), in exactly vanilla's depth-first order,
 * so the C call graph from the tick has no cycle and a GPU thread needs no
 * deep recursion.
 *
 * A frame is one call in progress: an op (what was called, with its
 * arguments), a resume point and the locals that live across a call. A step
 * runs the frame from its resume point until it either finishes or makes the
 * calls vanilla makes next; those it emits (bwl_emit) instead of calling. The
 * run loop pushes a step's emitted ops so that they execute in emission order,
 * each with everything it triggers, before the frame's next step (a frame that
 * finished with emitted ops has made tail calls). A step never reads the world
 * or spends a Random after it emitted: a callback whose body goes on after a
 * nested call is split there into another step or a continuation op, and the
 * block reads in the callback files check this (bwl_quiet).
 *
 * The public entry points (world_set_block, world_set_meta with flag 1,
 * world_notify_neighbors, ticks_schedule_block_update under the immediate
 * flag, liquid_update_tick, block_on_neighbor_changed and the blockcb_*
 * calls) push one frame and run the stack down to where it was. Nothing on
 * the stack calls an entry point; one reached from outside the stack while it
 * runs (nothing does today) runs in place, in program order. */
#ifndef NETHERITE_BLOCKWL_H
#define NETHERITE_BLOCKWL_H

#include <stdint.h>

struct world;

enum bwl_kind {
    BWL_NONE,
    /* world.c */
    BWL_SET_BLOCK,       /* x y z, a0 id, a1 meta, a2 flags; ret = changed */
    BWL_SET_META,        /* x y z, a0 meta, a1 flags (setBlockMetadataWithNotify) */
    BWL_NOTIFY,          /* x y z, a0 source, a1 the skipped side's index or -1 */
    BWL_NEIGHBOR,        /* x y z, a0 source: onNeighborBlockChange of the block there */
    BWL_CHEST_ADDED,     /* x y z, a0 self */
    /* ticks.c */
    BWL_SCHED,           /* x y z, a0 block, a1 delay */
    BWL_FALL_TAIL,       /* x y z, a0 id */
    /* features_springs.c */
    BWL_LIQUID_TICK,     /* x y z, a0 id, p the Random */
    BWL_STATIC_TICK,     /* x y z, a0 id, p the Random */
    BWL_SPRING_FIZZ,     /* x y z */
    /* blockcb.c */
    BWL_LAVA_FIZZ,       /* the fizz after lava_water_check's write */
    BWL_LIQUID_ADDED,    /* x y z, a0 id: the dynamic liquid's tick after the lava check */
    BWL_STATIC_NEIGHBOR, /* x y z, a0 id: setNotStationary after the lava check */
    BWL_PORTAL_PLACE,    /* a0..a2 origin, a3 width, a4 height, a5 axis */
    BWL_TORCH_ADDED,     /* x y z */
    BWL_TORCH_STAY,      /* x y z */
    BWL_TRIPWIRE_HOOKS,  /* x y z, a0 meta */
    BWL_HOOK_UPDATE,     /* x y z, a0 removing, a1 meta, a2 can_update, a3 wire_pos, a4 wire_meta */
    BWL_HOOK_SOUND,      /* a0 connected, a1 active, a2 powered, a3 was_powered */
    BWL_WIRE_AROUND,     /* x y z */
    BWL_WIRE_DIAG,       /* x y z, a0 dx, a1 dz */
    BWL_DIODE_NOTIFY,    /* x y z, a0 source */
    BWL_BUTTON_NOTIFY,   /* x y z, a0 facing, a1 id */
    BWL_RAIL_RESHAPE,    /* x y z, a0 place */
    BWL_RAIL_ADDED_TAIL, /* x y z, a0 id */
    BWL_DETECTOR,        /* x y z */
    BWL_DOOR,            /* x y z, a0 source, a1 self */
    BWL_TRAPDOOR_TAIL,   /* x y z, a0 source, a1 popped */
    BWL_DROP_STACK,      /* x y z, a0 item, a1 damage */
    /* comparator.c */
    BWL_CMP_NOTIFY,      /* x y z, a0 the block: World.func_147453_f */
    BWL_NKINDS
};

#define BWL_ARGS 6
#define BWL_LOCALS 34
/* frames the stack holds; the deepest recording needs far fewer (DEVLOG) */
#define BWL_CAP 4096
/* ops one step emits at most (a tripwire hook's line: 55) */
#define BWL_EMIT_CAP 128

struct bwl_op {
    uint8_t kind;
    int x, y, z;
    int a[BWL_ARGS];
    struct world *w;
    void *p;
};

struct bwl_frame {
    struct bwl_op op;
    int pc, i;
    /* the locals a step keeps across its calls */
    union { int l[BWL_LOCALS]; void *align; } u;
};

struct bwl {
    int sp;          /* frames on the stack */
    int nemit;       /* ops the running step emitted */
    int ret;         /* the last finished SET_BLOCK's result */
    int write_flags; /* the flags of the write the world's on_block reports (2 for a quiet store) */
    int running;     /* run loops in progress */
    struct bwl_op emit[BWL_EMIT_CAP];
    struct bwl_frame stack[BWL_CAP];
};

/* Emit one call from the running step. */
#define BWL_EMIT(k, w, x, y, z, ...) bwl_emit_(k, w, x, y, z, (int[BWL_ARGS]){__VA_ARGS__}, NULL)
void bwl_emit_(int kind, struct world *w, int x, int y, int z, const int *a, void *p);

/* Push one op and run the stack down to where it was; the entry points. */
void bwl_call(int kind, struct world *w, int x, int y, int z, const int *a, void *p);
#define BWL_CALL(k, w, x, y, z, ...) bwl_call(k, w, x, y, z, (int[BWL_ARGS]){__VA_ARGS__}, NULL)

/* 1 when the running step has emitted a call: its body may not read the
 * world or draw after that (vanilla would see the call's effects). The check
 * itself (bwl_quiet) sits in the callback files' block reads and draws. Both
 * are macros over the environment (a file that uses them includes env.h). */
#define bwl_pending() (nw_env->bwl.nemit != 0)
#define bwl_quiet() do { if (nw_env->bwl.nemit != 0) bwl_order_fail(); } while (0)
void bwl_order_fail(void);

/* the steps, one per file; each returns 1 when the frame finished */
int world_bwl_step(struct bwl_frame *f);
int ticks_bwl_step(struct bwl_frame *f);
int springs_bwl_step(struct bwl_frame *f);
int blockcb_bwl_step(struct bwl_frame *f);

/* The depth check build (make -C csrc worklist-depth, which compiles with
 * -DNETHERITE_WL_DEPTH): each explicit stack that replaced a recursion in the
 * tick notes the deepest it went, and the process prints them at exit; the
 * env's scratch stack (envstack.h) notes its high-water mark in bytes, and
 * session_tick the deepest stack a row took (session.c). */
#ifdef NETHERITE_WL_DEPTH
struct wl_depth { int bwl_frames, bwl_emit, flow_cost, nbt, potion, riding, envstack, tick_stack; };
extern struct wl_depth wl_depth;
#define WL_DEPTH_NOTE(field, v) do { if ((v) > wl_depth.field) wl_depth.field = (v); } while (0)
#else
#define WL_DEPTH_NOTE(field, v) ((void)0)
#endif

#endif
