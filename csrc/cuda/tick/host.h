/* S6 on the device (GPU plan L17), the replay's side: environments are
 * registered with a child process that holds the device (forked before the
 * device is touched, as worldgen/ahead.c's generators are: the grave's scan of
 * the replay's own mappings faults on a CUDA driver thread's stack), each
 * environment's image mirrored on the device at the image's own address.
 * A run sends the host's writes since the last exchange (env_export into
 * tick_down), runs S6 of one world on the device, and brings the device's
 * writes back (tick_up, for env_import).
 *
 * Runs batch: every live runner of the process (tick_live) submits its
 * environment's S6 and the last one to submit launches them all, one thread
 * block each. A runner that submitted and waits calls the yield hook (the
 * batch driver's baton, cuda/tick/batch.c) until its result is in. */
#ifndef NETHERITE_TICK_HOST_H
#define NETHERITE_TICK_HOST_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../engine/imgxfer.h"

struct env;
struct tick_env;

/* one run's outcome */
struct tick_result {
    uint32_t fault;           /* NWDEV_F_* (dev.h): 0, or the run must be redone in C */
    uint64_t fault_addr;
    uint64_t writes;          /* writes the barrier saw */
    uint64_t pages_up;        /* pages the run wrote */
    uint32_t batch;           /* environments in the launch */
    double kernel_ms;         /* the launch's device time (the whole batch) */
    double down_ms, up_ms;    /* the child's upload and download around it (device and copy time) */
};

/* Fork the child (before anything touches the device; a second call does
 * nothing), with stack_bytes of device stack per thread (0: 32 KB). 0 on
 * success. */
int tick_start(size_t stack_bytes);
/* Before tick_start: the calling process keeps the device and the replay
 * goes on in the fork (Nsight Compute profiles the process it started, and
 * a forked child of an injected process deadlocks in cuInit); the process
 * exits with the replay's status when the replay calls tick_stop or ends. */
void tick_device_root(int on);
/* Before tick_start: the device module's function addresses, one "ADDR
 * NAME" a line, written to path (a profile's PCs named by them) */
void tick_device_fnmap(const char *path);
/* Before tick_start: the environments one thread block runs, a thread each
 * (1 to 32; 1 by default: each environment a warp to itself; 32: a warp of
 * environments in step where their code agrees) */
void tick_device_pack(int n);
void tick_stop(void);
/* the live runners a launch waits for (1 by default) and the waiting
 * runner's hook */
void tick_live(int n);
void tick_set_yield(void (*yield)(void));
/* a runner finished: one fewer to wait for (it may launch the rest) */
void tick_runner_done(void);

/* Register an image environment; its transfer areas (shared with the
 * child). NULL on failure. */
struct tick_env *tick_env_add(struct env *e);
struct img_xfer *tick_down(struct tick_env *ce);
struct img_xfer *tick_up(struct tick_env *ce);

/* The down area holds a whole image (env_export without a tracker): the
 * child uploads it. 0 on success. */
int tick_upload(struct tick_env *ce);

/* S6 of the world whose struct servertick is st (in the image), after the
 * host's writes were exported into the down area: submits, waits for the
 * batch, and leaves the device's writes in the up area. 0 on success (see
 * res->fault), -1 if the child failed. */
int tick_run(struct tick_env *ce, uint64_t st, struct tick_result *res);
/* The same for one of the world tick's phases servertick_phase runs alone
 * (phase.h's id: S1, S2, S1T, S4, S5, S6, SG). */
int tick_run_phase(struct tick_env *ce, uint64_t st, int phase, struct tick_result *res);

/* After a faulted run: the pages the device wrote (the up area's page list)
 * are sent again from the host image, which the run did not touch. */
int tick_restore(struct tick_env *ce);

/* The device copy's pages listed in the down area's first n entries, into
 * the up area's bytes (a check reads what the device holds). */
int tick_read(struct tick_env *ce, size_t n);

/* the child's device-memory and mapping totals, for the report */
void tick_report(FILE *to);

#endif
