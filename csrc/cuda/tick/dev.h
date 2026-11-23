/* What the device side of S6 (cuda/tick/dev.c) and its host side
 * (cuda/tick/host.c) share: a launch's jobs and the translation tables. */
#ifndef NETHERITE_TICK_DEV_H
#define NETHERITE_TICK_DEV_H

#include <stdint.h>

/* the why of a flagged run (nwdev_job.fault) */
#define NWDEV_F_UNBACKED 1u     /* a write where the device holds only the zero granule */
#define NWDEV_F_IN 2u           /* a host address with no device counterpart was loaded */
#define NWDEV_F_UNSUPPORTED 4u  /* a function the device build does not run */

/* one environment's phase in a launch (one thread block) */
struct nwdev_job {
    uint64_t env;               /* the image's base: its struct env */
    uint64_t st;                /* the world's struct servertick, in the image */
    uint32_t *dirty;            /* a bit per image page the run wrote (device memory, cleared by the host) */
    const uint32_t *real;       /* a bit per 2 MB granule the device holds real memory for */
    unsigned char *scratch;     /* a zeroed page an untranslated read is pointed at */
    uint32_t fault;             /* NWDEV_F_* */
    uint32_t done;
    uint64_t fault_addr;        /* the first flagged address */
    uint64_t writes;            /* unused */
    uint32_t *dlist;            /* the pages the run wrote, in the order it first wrote them */
    uint32_t ndl, dlcap;
    uint64_t msg;               /* the format of the last message the tick printed */
    uint32_t phase;             /* phase.h's id: S6, or another servertick_phase runs */
    uint32_t pad;
};
#define NWDEV_F_DLIST 8u        /* more pages written than dlist holds */
#define NWDEV_F_OUT 32u         /* a device-only address was stored into the image: fault_addr is it */
#define NWDEV_F_ABORT 16u       /* the tick called abort() or exit(): fault_addr is the last message's format */

/* a sorted map entry: [from, from + size) goes to [to, ...); size 0 is one address */
struct nwdev_map {
    uint64_t from, to, size;
};

struct nwdev_tabs {
    const struct nwdev_map *in;   /* host to device, by host address */
    const struct nwdev_map *out;  /* device to host, by device address */
    uint32_t nin, nout;
    uint64_t in_lo, in_span;      /* the host addresses nwdev_in looks up: the executable's mapping */
    uint64_t gl_lo, gl_span;      /* the device's globals */
    uint64_t fn_lo, fn_span;      /* the device's functions */
    /* the device's functions by address (their "addresses" spread over the
     * whole space, so a range says nothing): open addressing, fn_mask + 1
     * slots of {device, host}, 0 empty */
    const uint64_t *fn_hash;
    uint64_t fn_mask;
};

/* nwdev_syms: the pass's table of the module's globals and address-taken
 * functions */
struct nwdev_sym {
    uint64_t dev;               /* its device address */
    uint64_t size;              /* bytes (0 for a function) */
    uint64_t name;              /* device address of its name */
    uint32_t flags;             /* 1 constant, 2 holds pointers, 4 a function */
    uint32_t pad;
};

/* the device side's per-thread state (dev.c), in the block's shared memory
 * by thread: a block runs up to NWDEV_PACK environments, one a thread */
#define NWDEV_PACK 32
#if defined(__NVPTX__)
struct env;
extern struct env *__attribute__((address_space(3))) nwdev_envs[NWDEV_PACK];
extern struct nwdev_job *__attribute__((address_space(3))) nwdev_curs[NWDEV_PACK];
extern uint64_t __attribute__((address_space(3))) nwdev_bases[NWDEV_PACK], nwdev_sizes[NWDEV_PACK];
#define nwdev_cur (nwdev_curs[__nvvm_read_ptx_sreg_tid_x()])
#define nwdev_base (nwdev_bases[__nvvm_read_ptx_sreg_tid_x()])
#define nwdev_size (nwdev_sizes[__nvvm_read_ptx_sreg_tid_x()])
#endif

#endif
