/* Two images built to hold the same state must sweep the same way (grave.h grave_sweep_joint).
 *
 * csrc/runtime/pool.c names an image's pointers by the words that differ between it and its twin, which holds only
 * if every pool slot holds the same thing in both. A sweep keeps a buried living when a stale word in its image names
 * it, and the stale words differ from one image to the other (here: the senses cache's owner, set in one image), so
 * two independent sweeps free different livings, the free lists diverge, and the next livings land in other slots.
 * The check buries the same three livings in two images, plants the stale word in the first only, ends the build the
 * way the pool does and allocates two livings in each: the slots must be the same. With -DGRAVE_TWIN_INDEPENDENT the
 * build ends in two independent sweeps (the code before lane/crash2) and the check fails. */
#include "../engine/arena.h"
#include "../engine/env.h"
#include "../engine/grave.h"
#include "../engine/living.h"

#include <stdio.h>

static void build(struct env *e, int planted, struct living *l[3])
{
    neth_env = e;
    grave_hold_env(1);
    for (int i = 0; i < 3; ++i)
    {
        l[i] = living_alloc();
        struct an_ent *en = an_ent_alloc();
        l[i]->entity_id = 100 + i;
        grave_bury(l[i], en);
    }
    if (planted) e->senses.owner = lv_ref(l[1]);
}

int main(void)
{
#if !defined(__linux__) || !defined(__x86_64__)
    puts("GRAVE-TWIN SKIP: the image grave runs on Linux x86-64");
    return 0;
#else
    struct env *old = neth_env, *a = env_new(), *b = env_new();
    if (!a || !b) return puts("GRAVE-TWIN FAIL: env allocation"), 1;
    grave_enable_images(1);

    struct living *la[3], *lb[3];
    build(a, 1, la);
    build(b, 0, lb);

#ifdef GRAVE_TWIN_INDEPENDENT
    /* the images swept on their own, as each did once its burials passed the threshold */
    for (int i = 0; i < 2; ++i)
    {
        neth_env = i ? b : a;
        grave_hold_env(0);
        grave_collect();
    }
#else
    grave_sweep_joint(a, b);
#endif

    int slot[2][2];
    for (int i = 0; i < 2; ++i)
    {
        neth_env = i ? b : a;
        for (int k = 0; k < 2; ++k) slot[i][k] = pool_index(&neth_arena()->livings, living_alloc());
    }
    int live_a = a->arena.livings.live, live_b = b->arena.livings.live;
    int ok = slot[0][0] == slot[1][0] && slot[0][1] == slot[1][1] && live_a == live_b;
    printf("GRAVE-TWIN DETAIL: image A slots %d %d (live %d), image B slots %d %d (live %d)\n", slot[0][0], slot[0][1], live_a,
           slot[1][0], slot[1][1], live_b);
    /* a long build, as a save's two replays: 600 burials a image, in rounds of 3, the pair swept once 16 are
     * waiting (grave_pair_collect), a stale lref planted in the first image only on every other round; the slots must
     * match at the end and what waits at once must stay near the threshold, not grow with the build */
    enum { ROUNDS = 200, THRESHOLD = 16, PEAK_MAX = THRESHOLD + 3 + 3 };
    struct env *c = env_new(), *d = env_new();
    if (!c || !d) return puts("GRAVE-TWIN FAIL: env allocation"), 1;
    grave_enable_images(THRESHOLD);
    neth_env = c; grave_hold_env(1);
    neth_env = d; grave_hold_env(1);
    size_t peak = 0, sweeps = 0, last_live = 0;
    for (int round = 0; round < ROUNDS; ++round)
    {
        for (int i = 0; i < 2; ++i)
        {
            struct env *e = i ? d : c;
            neth_env = e;
            struct living *l[3];
            for (int k = 0; k < 3; ++k)
            {
                l[k] = living_alloc();
                struct an_ent *en = an_ent_alloc();
                l[k]->entity_id = 1000 + round * 3 + k;
                grave_bury(l[k], en);
            }
            if (i == 0 && (round & 1)) e->senses.owner = lv_ref(l[1]);
        }
#ifdef GRAVE_TWIN_INDEPENDENT
        for (int i = 0; i < 2; ++i)
        {
            neth_env = i ? d : c;
            grave_hold_env(0);
            grave_collect();
            grave_hold_env(1);
        }
#else
        grave_pair_collect(c, d);
#endif
        size_t held = c->grave.nrecs > d->grave.nrecs ? c->grave.nrecs : d->grave.nrecs;
        if (held > peak) peak = held;
        size_t live = (size_t)c->arena.livings.live;
        if (live < last_live) ++sweeps;
        last_live = live;
    }
    grave_sweep_joint(c, d);
    int cs[3], ds[3];
    for (int k = 0; k < 3; ++k)
    {
        neth_env = c; cs[k] = pool_index(&neth_arena()->livings, living_alloc());
        neth_env = d; ds[k] = pool_index(&neth_arena()->livings, living_alloc());
    }
    int same = cs[0] == ds[0] && cs[1] == ds[1] && cs[2] == ds[2] && c->arena.livings.live == d->arena.livings.live;
    printf("GRAVE-TWIN LONG: %d burials an image, %zu sweeps, most waiting at once %zu (bound %d), slots %d %d %d | %d %d %d\n",
           ROUNDS * 3, sweeps, peak, (int)PEAK_MAX, cs[0], cs[1], cs[2], ds[0], ds[1], ds[2]);
    ok = ok && same && peak <= PEAK_MAX && sweeps >= 5;
    neth_env = old;
    env_free(d);
    env_free(c);
    env_free(b);
    env_free(a);
    puts(ok ? "GRAVE-TWIN PASS: the twin images hold the same slots after their sweep"
            : "GRAVE-TWIN FAIL: the twin images hold other livings in the same slots");
    return ok ? 0 : 1;
#endif
}
