/* A pending grave restored into another image must sweep like the source image. */
#include "../engine/arena.h"
#include "../engine/env.h"
#include "../engine/grave.h"
#include "../engine/living.h"

#include <stdio.h>

int main(void)
{
#if !defined(__linux__) || !defined(__x86_64__)
    puts("GRAVE-IMAGE SKIP: the image grave runs on Linux x86-64");
    return 0;
#else
    struct env *old = neth_env, *a = env_new(), *b = env_new();
    if (!a || !b) return puts("GRAVE-IMAGE FAIL: env allocation"), 1;
    grave_enable_images(1);

    neth_env = a;
    struct living *la = living_alloc();
    struct an_ent *ea = an_ent_alloc();
    la->entity_id = 101;
    grave_bury(la, ea);
    struct grave_rec saved = a->grave.recs[0];
    size_t first_sweep = a->grave.next_sweep;

    neth_env = b;
    struct living *lb = living_alloc();
    struct an_ent *eb = an_ent_alloc();
    lb->entity_id = 202;
    grave_bury(lb, eb);
    /* Model the image copy at the same pool slots. The record's bytes must remain valid after relocation. */
    b->grave.recs[0] = saved;
    b->grave.next_sweep = first_sweep;
    lb->entity_id = la->entity_id;
    int ok = 1;
    for (int tick = 0; tick < 4; ++tick)
    {
        /* One identical burial per tick drives both copies through successive sweep thresholds. */
        neth_env = a;
        struct living *xa = living_alloc();
        struct an_ent *ya = an_ent_alloc();
        xa->entity_id = 102 + tick;
        grave_bury(xa, ya);
        grave_collect();
        int alive_a = a->arena.livings.live, entries_a = a->arena.an_ents.live;
        size_t pending_a = a->grave.nrecs, next_a = a->grave.next_sweep;
        neth_env = b;
        struct living *xb = living_alloc();
        struct an_ent *yb = an_ent_alloc();
        xb->entity_id = 102 + tick;
        grave_bury(xb, yb);
        grave_collect();
        if (alive_a != b->arena.livings.live || entries_a != b->arena.an_ents.live ||
            pending_a != b->grave.nrecs || next_a != b->grave.next_sweep || la->entity_id != lb->entity_id)
        {
            printf("GRAVE-IMAGE DETAIL tick %d: A free %d/%d pending %zu next %zu; B free %d/%d pending %zu next %zu\n",
                   tick, a->arena.livings.top - alive_a, a->arena.an_ents.top - entries_a, pending_a, next_a,
                   b->arena.livings.top - b->arena.livings.live, b->arena.an_ents.top - b->arena.an_ents.live,
                   b->grave.nrecs, b->grave.next_sweep);
            ok = 0;
            break;
        }
    }
    if (!ok)
        puts("GRAVE-IMAGE FAIL: source and restored image diverged");
    neth_env = old;
    env_free(b);
    env_free(a);
    puts(ok ? "GRAVE-IMAGE PASS: source and restored image sweep equally" : "GRAVE-IMAGE FAIL: image ownership");
    return ok ? 0 : 1;
#endif
}
