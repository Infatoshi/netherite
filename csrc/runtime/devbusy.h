/* The device's busy time as its own timeline has it (CUPTI's activity
 * records: every kernel, copy and memset on every stream of the process),
 * for pipe_bench --profile: the union of the intervals, so work that
 * overlaps on several streams counts once (the renders' CUDA events, summed
 * over the streams, gave "the device 345% of the wall"). Linux and CUDA
 * (the dev host, the GPU host) only, like the rest of csrc/runtime. */
#ifndef NETHERITE_RUNTIME_DEVBUSY_H
#define NETHERITE_RUNTIME_DEVBUSY_H

#include <stdint.h>

/* Start recording (before the process's first CUDA call: the stream
 * threads' contexts); 0 on success. */
int devbusy_start(void);

/* The device's clock now (CUPTI's timestamps, ns): a window's ends. */
uint64_t devbusy_now(void);

/* Over the window [t0, t1): the device's busy ms (the union of every
 * interval), the kernels' union and the copies' and memsets' union, their
 * summed durations (what summing overlapping streams gives), the records
 * seen, and those dropped (CUPTI's buffers full: the union is then a lower
 * bound). The records before t1 are consumed. */
struct devbusy {
    double busy_ms, kernel_ms, copy_ms, summed_ms;
    uint64_t records, dropped;
};
void devbusy_read(uint64_t t0, uint64_t t1, struct devbusy *out);

#endif
