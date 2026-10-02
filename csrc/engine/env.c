/* See env.h. */
#include "env.h"

#include "collide.h"
#include "envstack.h"
#include "grave.h"
#include "image.h"
#include "spill.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__NVPTX__)
#include <sys/mman.h>
#endif

/* The process's first environment: the one every tool that runs a single
 * world uses. Its storage is here, set up before main. */
_Static_assert(offsetof(struct env, arena) == 0, "nw_arena() reads the env as its arena");

#if !defined(__NVPTX__)   /* the device build names the environment per thread block (arena.h) */
static struct env env_first;

_Thread_local struct env *nw_env __attribute__((tls_model(NW_ENV_TLS_MODEL))) = &env_first;
_Thread_local struct env_scratch *nw_thread_scratch __attribute__((tls_model(NW_ENV_TLS_MODEL)));
#endif

/* The non-zero starting values of a zeroed environment. */
static void env_init(struct env *e)
{
    e->ticks.cur = &e->ticks.own;
    e->blockcb.env.role = DET_OTHER;
    e->features_trees.bigtree_scaled = 1;
    e->snap_runtime.player_id = -1;
    e->survival.attacker = -1;
    collide_env_init(e->collide.fence_bounds, e->collide.chest_bounds, e->collide.ladder_bounds);
    block_rand_born(e->rng.block);
    e->nbtjson.scratch_cur = -1;
    e->itemtag.nentries = 1;   /* 0 is the null tag */
    if (e->img.size) arena_env_init_in(&e->arena, image_arena_base(e), arena_env_bytes());
    else arena_env_init(&e->arena);
}

#if !defined(__NVPTX__)
__attribute__((constructor)) static void env_first_init(void)
{
    env_init(&env_first);
    /* the scratch stack is empty between rows, where the grave sweeps: a
     * stale address in it would only pin a buried living */
    grave_ignore(env_first.scratch.stack, sizeof env_first.scratch.stack);
}

/* A new environment is an image (image.h): one mapping holding the env,
 * its arena and its heap. */
struct env *env_new(void)
{
    struct env *e = image_env_new();

    if (e != NULL) env_init(e);
    return e;
}

struct env_scratch *env_scratch_new(void)
{
    /* zeroed pages, committed as the calls write them */
    void *s = mmap(NULL, sizeof(struct env_scratch), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    return s == MAP_FAILED ? NULL : s;
}

void env_scratch_free(struct env_scratch *s)
{
    if (s != NULL) munmap(s, sizeof *s);
}

void env_free(struct env *e)
{
    if (e == NULL || e == &env_first) return;

    /* the tick set's arrays are in the arena's slab, which goes with it */
    free(e->phase.written);
    rspill_close(&e->rspill);
    arena_env_free(&e->arena);
    if (e->img.size) image_env_free(e);
    else free(e);
}
#endif

void *envstack_take(size_t n)
{
    size_t top = nw_scratch->stack_top;
    size_t end = top + ((n + 63) & ~(size_t)63);

    if (end > ENVSTACK_CAP)
    {
        fprintf(stderr, "envstack: %zu bytes over the %u-byte scratch stack\n", end - ENVSTACK_CAP, ENVSTACK_CAP);
        abort();
    }
    nw_scratch->stack_top = end;
    WL_DEPTH_NOTE(envstack, (int)end);
    return nw_scratch->stack + top;
}

void *envstack_zeroed(size_t n)
{
    return memset(envstack_take(n), 0, n);
}

void envstack_give(void *var)
{
    nw_scratch->stack_top = (size_t)(*(unsigned char **)var - nw_scratch->stack);
}
