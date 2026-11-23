/* ntloop, the native trainer's loop (SPEC.md "The environment as configuration" item 6): libneth's vector env
 * (vecenv.h) driven on one thread as the trainer drives it, a policy launched a group (loop.h: the stand-in, the no-op
 * or train/trainer.cu's actor), learn.h's PPO the learner, fastppo.py (mcsr-speedrun) the reference.
 *
 * The contract every change keeps (the digests, train-loop-check and train-wire-check hold it):
 *  - a group step: recv (the next group the workers finished), its books, then on the group's own stream the cast
 *    (or the host cast's bytes) and the scalars into the segment's step, the policy's launch, the actions copied back
 *    into the env's buffer and the group's event; the group is sent once its event has fired.
 *  - --digest hashes a received step's reward, driver, kind, chosen and base scalars before anything rewrites them:
 *    the clock wrap, --zero-sc and ppo.stage_scale come after; --critic's privileged scalars are copied before
 *    --zero-sc masks the actor's.
 *  - a step's reward, end and driver are the previous action's: they go into the segment at t - 1, across a segment's
 *    rollover too.
 *  - each group owns two segments of T steps, one filling and one the learner's. A full segment's bootstrap (the next
 *    segment's first value, zero where its episode ended) is made on the group's stream before its ready event; the
 *    learner takes segments oldest first, one update in flight on its own stream, and a group about to refill a
 *    segment the learner has not read waits for it.
 *  - the actor has two weight images: an update writes the one launches do not read, after every group's launches so
 *    far, and launches take it only once the update has run on the device.
 *  - --depth D leaves at most D groups launched and unsent (0: each sent before the next recv); --serve (the default
 *    with the cast on the device) sends every finished group, takes every received group, casts the groups received
 *    together in one launch on the first one's stream and blocks in recv only with nothing launched.
 *  - the episodes a recv ends belong to the group received (another group's records wait for its own recv); the
 *    recorder takes a group's buffers before the group is sent again.
 *  - the ladder changes an env's rung only at its reset, the curriculum's weights move at a log row, and a checkpoint
 *    is the next update's weights.
 *  - nothing on the hot path waits on the device but the sends (and --frame-digest, a check).
 *
 * --digest is vec_bench --digest's (FNV-1a an env over each receive past the first, the envs' hashes summed), so with
 * the stand-in the two are equal; --frame-digest also hashes what the policy and learner read from the segment. --out
 * DIR appends episodes.jsonl and iters.jsonl (fastppo.py's formats) and saves model.ntw with its Adam state. The flags
 * DIR appends episodes.jsonl and iters.jsonl (fastppo.py's formats) and saves model.ntw with its Adam state.
 *
 * This file is the command line's adapter; the trainer is the neth_* modules (neth_pipe_internal.h has their state):
 *  - neth_runconf.c: every setting, one table (flag, run file key, type, default, help): the defaults, the run file's
 *    policy part, the flags, the checks across settings, the vector env's options; the usage line is the table's.
 *  - neth_schedule.c: the starts' mix and curriculum, the sim's fidelity, the ladder.
 *  - neth_pipe.c: the lifecycle (create, run, finish, destroy) and the hot path, with two hooks (staged, received)
 *    that the observation's and the score's options set; neth_loop_bench.c: --ab.
 *  - neth_segment.c, neth_train_stage.c, neth_distill_stage.c: the segments, the policy and the learner, --teacher.
 *  - neth_report.c, neth_checkpoint.c, neth_eval.c, neth_record_hook.c: the rows and the score, the model's files,
 *    the checkpoint eval's children, the recorder.
 *  - neth_loop_check.c: --check-learn and --check-actor, the gates without the env. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "neth_pipe_internal.h"
#include "neth_runconf.h"

_Static_assert(NETH_NET_NS == NETH_VEC_NSCALAR + NETH_NET_LATE * NETH_VEC_LATE_N + NETH_NET_HSEE * NETH_VEC_HSEE_N +
                           NETH_NET_DIG * NETH_VEC_DIG_N + NETH_NET_ORE * NETH_VEC_ORE_N + NETH_NET_GV * NETH_VEC_GV_N +
                           NETH_NET_TASK * NETH_VEC_TASK_N &&
                   NETH_NET_LATE_N == NETH_VEC_LATE_N && NETH_NET_HSEE_N == NETH_VEC_HSEE_N && NETH_NET_DIG_N == NETH_VEC_DIG_N &&
                   NETH_NET_ORE_N == NETH_VEC_ORE_N && NETH_NET_GV_N == NETH_VEC_GV_N && NETH_NET_TASK_N == NETH_VEC_TASK_N,
               "net.h's scalars are the vector env's");
_Static_assert(NETH_NET_ACTION == NETH_VEC_ACTION + NETH_NET_HOTBAR, "net.h's action is the vector env's (NETH_VEC_HOTBAR's with "
               "the hotbar)");
_Static_assert(NETH_NET_HOT == NETH_VEC_HOT, "the hotbar's categories");

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--mopts"))
        {
            neth_vec_mopt_list(stdout);   /* the macro options, their words and defaults */
            return 0;
        }
    /* the settings: the defaults, the run file, the flags; the gates without the env; the settings' checks */
    static struct neth_runconf c;
    int rc = neth_runconf_parse(&c, argc, argv);
    if (rc) return rc;
    if (c.check_l) return neth_check_learn(c.check_l, &c.hy, c.ent, c.epochs, c.mb, c.prec);
    if (c.check_a) return neth_check_actor(c.check_a);
    if ((rc = neth_runconf_resolve(&c))) return rc;
    /* the vector env's options, its starts and fidelity; then the pipe made, run (or --ab), finished, freed */
    double t_start = wall_s();
    struct neth_vec_opts o;
    if ((rc = neth_runconf_vec_opts(&c, &o))) return rc;
    static struct neth_pipe L0;
    struct neth_pipe *L = &L0;
    L->t_start = t_start;
    if ((rc = neth_schedule_starts(L, &c, &o)) || (rc = neth_schedule_fidelity(L, &c, &o))) return rc;
    rc = neth_pipe_create(L, &c, &o, argc, argv);
    if (!rc)
    {
        if (c.ab) neth_bench_run(L, &c);
        else neth_pipe_run(L, &c);
        neth_pipe_finish(L, &c);
        rc = L->fail;
    }
    neth_pipe_destroy(L);
    return rc;
}
