/* ntloop's --ab R: warm group steps, then R pairs of S group steps a group in the order A B B A (A vec_bench's loop
 * or, --ab-a loop, this loop with the stand-in and no learner; B this one; --ab-swap B A A B), so a drift through the
 * window cancels; each stretch ends with every launched group sent. The env's own step cost drifts with the game's
 * state (the workers' step 0.147 to 0.217 ms an env-step between 300-step stretches of one window) and spikes where the
 * trajectory does something costly (0.338 ms in one 50-step stretch, at the same place in two windows), so the
 * stretches are short and summed by mode, and the gate's figure is the median of the quads' ratios (B's env-steps/s
 * over A's). A B stretch starts its groups' segments afresh: the learner trains on the policy's own steps alone. */
#define _GNU_SOURCE
#include "neth_pipe_internal.h"
#include "neth_runconf.h"

void neth_bench_run(struct neth_pipe *L, const struct neth_runconf *c)
{
    int groups = L->G, ab = c->ab, steps = c->steps, learn = c->learn;
    int loop_a = !strcmp(c->ab_a, "loop");
    struct neth_mark m0, m1;
    for (int it = 0; it < c->warm * groups && !L->fail; ++it)
        if (neth_pipe_iterate(L, 0)) break;
    neth_pipe_drain(L);
    struct neth_mark acc[2], zero;
    memset(acc, 0, sizeof acc);
    memset(&zero, 0, sizeof zero);
    double es[2] = {0, 0}, busy_w[2] = {0, 0}, *quad = calloc((size_t)ab, sizeof *quad);
    int nq = 0;
    for (int r = 0; r < 2 * ab && !L->fail; ++r)
    {
        int host = (r % 4 == 0 || r % 4 == 3) != c->ab_swap;
        L->pol = host && loop_a ? &L->PA : &L->P;
        L->learning = L->learn && !host;
        if (!host)
            for (int g = 0; g < groups; ++g) L->tg[g] = 0;
        neth_pipe_mark(L, &m0);
        for (int it = 0; it < steps * groups && !L->fail; ++it)
            if (neth_pipe_iterate(L, host && !loop_a)) break;
        neth_pipe_drain(L);
        neth_pipe_mark(L, &m1);
        if (L->fail) break;
        struct neth_mark *a = &acc[!host];
        for (int q = 0; q < 8; ++q) a->s[q] += m1.s[q] - m0.s[q];
        for (int q = 0; q < T_N; ++q) a->tm[q] += m1.tm[q] - m0.tm[q];
        a->t += m1.t - m0.t;
        a->gsteps += m1.gsteps - m0.gsteps;
        a->jobs += m1.jobs - m0.jobs;
        a->gpu_learn += m1.gpu_learn - m0.gpu_learn;
        a->t_cast += m1.t_cast - m0.t_cast;
        a->casts += m1.casts - m0.casts;
        a->cast_groups += m1.cast_groups - m0.cast_groups;
        busy_w[!host] += (m1.s[7] - m0.s[7]) * 1e-9;
        double e = (m1.s[0] - m0.s[0]) / (m1.t - m0.t);
        es[!host] += e;
        printf("%s%c %.0fk %.3f%s", r ? "" : "ab stretches (env-steps/s, the workers' ms an env-step): ", host ? 'A' : 'B',
               e / 1e3, (m1.s[7] - m0.s[7]) * 1e-6 / (m1.s[0] - m0.s[0] + m1.s[1] - m0.s[1]),
               r == 2 * ab - 1 ? "\n" : ", ");
        if (r % 4 == 3)
        {
            quad[nq++] = es[1] / es[0];
            es[0] = es[1] = 0;
        }
    }
    if (!L->fail && nq > 0)
    {
        double ea = neth_pipe_report(L, &zero, &acc[0], loop_a ? "ab A ntloop's stand-in" : "ab A vec_bench's loop",
                                     L->t_make);
        double eb = neth_pipe_report(L, &zero, &acc[1], learn ? "ab B the trainer" : "ab B ntloop", L->t_make);
        /* the quads' median */
        for (int i = 1; i < nq; ++i)
            for (int j = i; j > 0 && quad[j] < quad[j - 1]; --j)
            {
                double x = quad[j];
                quad[j] = quad[j - 1];
                quad[j - 1] = x;
            }
        double med = nq % 2 ? quad[nq / 2] : 0.5 * (quad[nq / 2 - 1] + quad[nq / 2]);
        printf("ab: %s %.0f, %s %.0f env-steps/s over %d stretches of %d group steps each: %.1f%%; "
               "the %d quads' median %.1f%% (%.1f%% to %.1f%%); the workers stepping %.2f and %.2f on average\n",
               loop_a ? "ntloop's stand-in" : "vec_bench's loop", ea, learn ? "the trainer" : "ntloop", eb, ab, steps,
               100 * eb / ea, nq, 100 * med, 100 * quad[0], 100 * quad[nq - 1], busy_w[0] / acc[0].t,
               busy_w[1] / acc[1].t);
    }
    free(quad);
}
