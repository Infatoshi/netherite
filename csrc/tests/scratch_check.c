/* A pool worker's scratch (env.h nw_thread_scratch) serves every
 * environment the worker steps, so nothing in it may keep memory of one
 * environment past the call that took it (lane/slabfix: a collision list
 * that grew past its own 512 boxes kept its slab block for the next query,
 * the next env's queries wrote their boxes into the first env's image, and
 * the first env's reset had given those bytes to its slab's free chains: a
 * chain link overwritten with a double, a general protection fault in
 * slab_alloc, iteration 15 of a 496-world GRPO run).
 *
 *   scratch_check (make -C csrc scratch-check; make test and make smoke run it)
 *
 * Two image environments on one thread with one thread scratch, as a
 * worker has them: env a's collision list grows into its slab, then env b
 * takes the list. b's list must not point into a's image, and after b
 * grows its own list past a's room, every free chain of both slabs must
 * name blocks of its own slab only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/collide.h"
#include "../engine/env.h"
#include "../engine/image.h"

static int fails;

static void fail(const char *what)
{
    printf("FAIL %s\n", what);
    ++fails;
}

/* a collision query's list from the scratch, n boxes pushed */
static void query(int n)
{
    struct collide_list *l COLLIDE_SCRATCH = collide_scratch_begin();

    collide_list_clear(l);
    for (int i = 0; i < n; ++i)
    {
        struct aabb b = {i, 64.0, -i, i + 1.0, 65.0, 1.0 - i};
        collide_list_push(l, &b);
    }
}

/* every free chain of e's slab names offsets inside it */
static int chains_own(struct env *e)
{
    const struct slab *s = &e->arena.slab;

    for (int k = 0; k < SLAB_CLASSES; ++k)
        for (int64_t off = s->free_head[k]; off != -1;)
        {
            if (off < 0 || (uint64_t)off >= s->used) return 0;
            memcpy(&off, s->base + off, sizeof off);
        }
    return 1;
}

int main(void)
{
    struct env *a = env_new(), *b = env_new();

    if (a == NULL || b == NULL)
    {
        printf("FAIL no image environment\n");
        return 1;
    }
    nw_thread_scratch = env_scratch_new();

    nw_env = a;
    query(COLLIDE_MAX_BOXES + 88);   /* past the list's own room: a slab block of a's */

    nw_env = b;
    {
        struct collide_list *l COLLIDE_SCRATCH = collide_scratch_begin();

        if (image_holds(a, l->box)) fail("env b's collision list writes into env a's image");
    }
    query(4 * COLLIDE_MAX_BOXES);    /* b's list grows past a's room */
    if (!chains_own(b)) fail("env b's slab chains name a block outside its slab");
    nw_env = a;
    if (!chains_own(a)) fail("env a's slab chains name a block outside its slab");
    query(COLLIDE_MAX_BOXES + 88);
    if (!chains_own(a)) fail("env a's slab chains name a block outside its slab after its next query");

    env_scratch_free(nw_thread_scratch);
    nw_thread_scratch = NULL;
    if (fails) return 1;
    printf("scratch_check: OK: two environments on one thread scratch, each list's slab block its own env's\n");
    return 0;
}
