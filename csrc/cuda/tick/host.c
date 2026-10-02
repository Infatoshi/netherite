/* S6 on the device, the host side (host.h): the replay's client and the
 * child process that holds the device.
 *
 * The child. Forked before anything touches the device, it first unmaps
 * the image window (image.h's IMG_WINDOW, all IMG_SLOTS slots), which drops its
 * copies of the parent's images, so each
 * environment's device mirror can be reserved at the image's own address
 * (cuMemAddressReserve at a fixed address). It loads the device module
 * (tick.cubin: the engine compiled for the device, cuda/tick/tick.mk), reads the
 * pass's symbol table (nwdev_syms) and resolves every name in its own
 * executable's symbol table (the same program as the parent: the host build
 * carries the same names, nw-rename). A mutable global gets the host's
 * bytes; the translation maps go to the device (dev.c nwdev_in and
 * nwdev_out). A constant without pointers is compared with the host's and
 * left unmapped if it differs.
 *
 * Device memory per environment, by 2 MB granule of the image: a granule
 * that ever held a nonzero page, or sits at the growing end of a live
 * extent, is real (its own memory); the rest of the live extents (and a
 * margin past the heap's end) map the environment's one zero granule, so a
 * read there reads zero as on the host, and a write there is flagged by the
 * barrier: that run is redone in C and the granule made real.
 *
 * A run: the host's exported pages go to a staging buffer and are scattered
 * into the image; the batch's S6 runs, one thread block per environment;
 * each environment's written pages (the barrier's list) are sorted, gathered
 * and copied into its up area. The transfer areas are one memfd per
 * environment, mapped by both processes; commands and results go over a
 * socket. */
/* the child drops the parent's images, the current environment's among
 * them: this file allocates from the C library's heap, never an image's */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#define _GNU_SOURCE
#include "host.h"
#include "dev.h"

#include <cuda.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../engine/image.h"
#include "../../engine/env.h"
#include "../../engine/phase.h"

#ifndef S6_CUBIN
#define S6_CUBIN "out/native/cuda/tick/tick.cubin"
#endif

#define S6_MAX_ENVS 128
#define S6_MAX_EXT 64
#define S6_GRAN ((uint64_t)2 << 20)
#define S6_DLCAP (1u << 20)             /* pages a run may write */
#define S6_HEAP_MARGIN ((uint64_t)256 << 20)

enum { OP_REG = 1, OP_UPLOAD, OP_LAUNCH, OP_RESTORE, OP_REPORT, OP_READ, OP_QUIT };

struct s6_ext {
    uint64_t off, len;
    int32_t part;                     /* image.h IMG_PART_* */
    int32_t pad;
};

struct s6_msg {
    uint32_t op, n;                   /* n: jobs of a launch */
};

struct s6_reg {
    int32_t id;
    uint32_t pad;
    uint64_t base, size;
};

struct s6_job {
    int32_t id;
    uint32_t next;
    uint64_t st;
    uint32_t phase, pad;              /* phase.h's id */
    uint64_t npages, ndata;           /* the down area's transfer */
    struct s6_ext ext[S6_MAX_EXT];    /* the image's live extents now */
};

struct s6_res {
    int32_t id;
    uint32_t fault;
    uint64_t fault_addr, writes, pages_up;
    double kernel_ms, down_ms, up_ms;
};

static uint64_t idx_bytes(uint64_t size)
{
    return ((size / 4096) * 4 + 4095) & ~(uint64_t)4095;
}

/* the memfd's layout: down page list, down bytes, up page list, up bytes */
static uint64_t shm_len(uint64_t size) { return 2 * (idx_bytes(size) + size); }

static void xfer_views(unsigned char *shm, uint64_t size, struct img_xfer *down, struct img_xfer *up)
{
    uint64_t ib = idx_bytes(size);
    memset(down, 0, sizeof *down);
    memset(up, 0, sizeof *up);
    down->cap = up->cap = size / 4096;
    down->page = (uint32_t *)(void *)shm;
    down->bytes = shm + ib;
    up->page = (uint32_t *)(void *)(shm + ib + size);
    up->bytes = shm + 2 * ib + size;
    down->size = up->size = size;
}

static int full_read(int fd, void *b, size_t n)
{
    for (size_t k = 0; k < n;)
    {
        ssize_t r = read(fd, (char *)b + k, n - k);
        if (r <= 0)
        {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
        k += (size_t)r;
    }
    return 0;
}

static int full_write(int fd, const void *b, size_t n)
{
    for (size_t k = 0; k < n;)
    {
        ssize_t r = write(fd, (const char *)b + k, n - k);
        if (r <= 0)
        {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
        k += (size_t)r;
    }
    return 0;
}

static int send_fd(int sock, int fd, const void *b, size_t n)
{
    struct iovec iov = {(void *)b, n};
    char ctl[CMSG_SPACE(sizeof(int))];
    struct msghdr m = {0};
    memset(ctl, 0, sizeof ctl);
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = ctl;
    m.msg_controllen = sizeof ctl;
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof fd);
    return sendmsg(sock, &m, 0) == (ssize_t)n ? 0 : -1;
}

static int recv_fd(int sock, int *fd, void *b, size_t n)
{
    struct iovec iov = {b, n};
    char ctl[CMSG_SPACE(sizeof(int))];
    struct msghdr m = {0};
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = ctl;
    m.msg_controllen = sizeof ctl;
    if (recvmsg(sock, &m, MSG_WAITALL) != (ssize_t)n) return -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    if (c == NULL || c->cmsg_type != SCM_RIGHTS) return -1;
    memcpy(fd, CMSG_DATA(c), sizeof *fd);
    return 0;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

/* ================================================================ the child */

struct cenv {
    int used;
    uint64_t base, size;
    unsigned char *shm;
    struct img_xfer down, up;
    uint8_t *gstate;                  /* per granule: 0 unmapped, 1 the zero granule, 2 real */
    size_t ngran;
    uint32_t *real_h;
    CUdeviceptr real_d, dirty, dlist, scratch;
    int real_changed;
    CUmemGenericAllocationHandle zero;
    int zero_clear;                   /* the zero granule is mapped and zeroed */
    uint64_t n_real, n_zero;
    uint32_t *sorted;                 /* the last run's written pages, ascending (pinned) */
    uint32_t nsorted;
    uint64_t pin_down, pin_up;        /* the transfer areas' pinned heads, bytes (pin_areas) */
};

static struct {
    CUdevice dev;
    CUcontext ctx;
    CUmodule mod;
    CUfunction k_chunkloop, k_tick_scatter, k_tick_gather, k_tick_clear;
    CUstream s;
    CUevent e0, e1;
    CUmemAllocationProp prop;
    CUmemAccessDesc acc;
    struct cenv env[S6_MAX_ENVS];
    CUdeviceptr stage, stage_addr, stage_src, jobs;
    uint64_t stage_cap;
    uint64_t *addr_h;                 /* pinned: each staged page's image address */
    int64_t *src_h;                   /* pinned: its stage index (-1 zeros), or a dirty word's address */
    size_t mapped_real, mapped_zero, phys_bytes;
    long mismatched_consts, unmatched_mutable, mapped_syms, copied_globals;
    size_t stack;                     /* device stack per thread, set before the fork */
    int root;                         /* the device stays in the process that called tick_start (tick_device_root) */
    char fnmap[512];                  /* tick_device_fnmap's file */
    uint32_t pack;                    /* environments a thread block runs, one a thread (tick_device_pack) */
    pid_t replay;                     /* then: the forked replay, waited for at the end */
    long notes;
    char log[1 << 14];
} C;

#define CK(x)                                                                              \
    do {                                                                                   \
        CUresult r_ = (x);                                                                 \
        if (r_ != CUDA_SUCCESS)                                                            \
        {                                                                                  \
            const char *s_ = "?";                                                          \
            cuGetErrorString(r_, &s_);                                                     \
            fprintf(stderr, "tick_host child: %s: %s (line %d)\n", #x, s_, __LINE__);         \
            return -1;                                                                     \
        }                                                                                  \
    } while (0)

/* ---- the executable's symbol table */

struct hsym {
    const char *name;
    uint64_t addr, size;
};
static struct hsym *hs;
static size_t nhs, caphs;
static uint64_t exe_lo, exe_hi, exe_bias;

static uint64_t fnv(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    while (*s) h = (h ^ (unsigned char)*s++) * 1099511628211ull;
    return h;
}

static int phdr_cb(struct dl_phdr_info *info, size_t sz, void *arg)
{
    (void)sz;
    (void)arg;
    exe_bias = info->dlpi_addr;
    exe_lo = UINT64_MAX;
    exe_hi = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD) continue;
        uint64_t lo = exe_bias + p->p_vaddr, hi = lo + p->p_memsz;
        if (lo < exe_lo) exe_lo = lo;
        if (hi > exe_hi) exe_hi = hi;
    }
    return 1;   /* the first object is the executable */
}

static int load_symtab(void)
{
    dl_iterate_phdr(phdr_cb, NULL);
    int fd = open("/proc/self/exe", O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    fstat(fd, &st);
    unsigned char *f = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (f == MAP_FAILED) return -1;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)f;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(f + eh->e_shoff);
    for (int i = 0; i < eh->e_shnum; ++i)
    {
        if (sh[i].sh_type != SHT_SYMTAB) continue;
        const Elf64_Sym *sy = (const Elf64_Sym *)(f + sh[i].sh_offset);
        const char *str = (const char *)(f + sh[sh[i].sh_link].sh_offset);
        size_t n = sh[i].sh_size / sizeof *sy;
        caphs = 1;
        while (caphs < n * 2) caphs <<= 1;
        hs = calloc(caphs, sizeof *hs);
        for (size_t k = 0; k < n; ++k)
        {
            int ty = ELF64_ST_TYPE(sy[k].st_info);
            if (sy[k].st_shndx == SHN_UNDEF || sy[k].st_value == 0 || (ty != STT_OBJECT && ty != STT_FUNC)) continue;
            const char *nm = str + sy[k].st_name;
            uint64_t h = fnv(nm) & (caphs - 1);
            while (hs[h].name != NULL && strcmp(hs[h].name, nm)) h = (h + 1) & (caphs - 1);
            if (hs[h].name != NULL) continue;   /* a duplicate local name: the first stands */
            hs[h].name = nm;
            hs[h].addr = exe_bias + sy[k].st_value;
            hs[h].size = sy[k].st_size;
            ++nhs;
        }
    }
    return nhs ? 0 : -1;
}

static const struct hsym *hs_find(const char *nm)
{
    uint64_t h = fnv(nm) & (caphs - 1);
    while (hs[h].name != NULL)
    {
        if (!strcmp(hs[h].name, nm)) return &hs[h];
        h = (h + 1) & (caphs - 1);
    }
    return NULL;
}

/* the host symbol holding a, for the flagged runs' notes */
static const char *hs_name_at(uint64_t a, uint64_t *off)
{
    const struct hsym *best = NULL;
    for (size_t k = 0; k < caphs; ++k)
        if (hs[k].name != NULL && hs[k].addr <= a && (best == NULL || hs[k].addr > best->addr)) best = &hs[k];
    if (best == NULL) return "?";
    *off = a - best->addr;
    return best->name;
}

/* the device symbols by address, for the notes */
static struct dsym { uint64_t dev, size; char name[96]; } *ds;
static uint32_t nds;
static const char *dev_name_at(uint64_t a)
{
    static char out[160];
    const struct dsym *best = NULL;
    for (uint32_t k = 0; k < nds; ++k)
        if (ds[k].dev <= a && (best == NULL || ds[k].dev > best->dev)) best = &ds[k];
    if (best == NULL) return "?";
    snprintf(out, sizeof out, "after %s+%#llx, %llu bytes", best->name, (unsigned long long)(a - best->dev),
             (unsigned long long)best->size);
    return out;
}

static int map_cmp(const void *a, const void *b)
{
    const struct nwdev_map *x = a, *y = b;
    return x->from < y->from ? -1 : x->from > y->from;
}

/* the pass's table against the host's symbols: globals' bytes, the maps */
static int bind_symbols(FILE *log)
{
    FILE *fnmap = C.fnmap[0] ? fopen(C.fnmap, "w") : NULL;
    CUdeviceptr d;
    size_t sz;
    CK(cuModuleGetGlobal(&d, &sz, C.mod, "nwdev_nsyms"));
    uint32_t n;
    CK(cuMemcpyDtoH(&n, d, 4));
    CK(cuModuleGetGlobal(&d, &sz, C.mod, "nwdev_syms"));
    struct nwdev_sym *sy = malloc(n * sizeof *sy);
    CK(cuMemcpyDtoH(sy, d, n * sizeof *sy));
    if (load_symtab() != 0)
    {
        fprintf(stderr, "tick_host child: no symbol table in /proc/self/exe\n");
        return -1;
    }
    struct nwdev_map *in = calloc(n, sizeof *in), *out = calloc(n, sizeof *out);
    ds = calloc(n, sizeof *ds);
    uint32_t nin = 0, nout = 0;
    uint64_t gl_lo = UINT64_MAX, gl_hi = 0, fn_lo = UINT64_MAX, fn_hi = 0;
    char name[512];
    unsigned char *buf = NULL;
    size_t bufcap = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        /* a name ends where its allocation may: read it a byte at a time
         * past the first failure */
        size_t k = 0;
        while (k < sizeof name - 1)
        {
            size_t step = 32;
            if (k + step > sizeof name - 1) step = sizeof name - 1 - k;
            if (cuMemcpyDtoH(name + k, sy[i].name + k, step) != CUDA_SUCCESS)
            {
                for (step = 0; k < sizeof name - 1; ++k)
                {
                    CK(cuMemcpyDtoH(name + k, sy[i].name + k, 1));
                    if (name[k] == 0) break;
                }
                break;
            }
            if (memchr(name + k, 0, step) != NULL) break;
            k += step;
        }
        name[sizeof name - 1] = 0;
        ds[nds].dev = sy[i].dev;
        ds[nds].size = sy[i].size;
        snprintf(ds[nds++].name, sizeof ds->name, "%s", name);
        int fn = sy[i].flags & 4, cst = sy[i].flags & 1, ptrs = sy[i].flags & 2;
        if (fn && fnmap != NULL) fprintf(fnmap, "%llx %s\n", (unsigned long long)sy[i].dev, name);
        if (fn) { if (sy[i].dev < fn_lo) fn_lo = sy[i].dev; if (sy[i].dev + 1 > fn_hi) fn_hi = sy[i].dev + 1; }
        else { if (sy[i].dev < gl_lo) gl_lo = sy[i].dev; if (sy[i].dev + sy[i].size > gl_hi) gl_hi = sy[i].dev + sy[i].size; }
        const struct hsym *h = hs_find(name);
        if (h == NULL)
        {
            if (!cst && !fn && strcmp(name, "stderr") && strcmp(name, "stdout"))
            {
                if (C.unmatched_mutable++ < 20) fprintf(log, "tick_host: mutable device global %s has no host symbol\n", name);
            }
            continue;
        }
        if (!fn)
        {
            if (h->size != sy[i].size)
            {
                fprintf(log, "tick_host: %s is %llu bytes on the host, %llu on the device\n", name,
                        (unsigned long long)h->size, (unsigned long long)sy[i].size);
                if (!cst) return -1;
                ++C.mismatched_consts;
                continue;
            }
            if (cst && !ptrs && sy[i].size)
            {
                if (bufcap < sy[i].size) buf = realloc(buf, bufcap = sy[i].size);
                CK(cuMemcpyDtoH(buf, sy[i].dev, sy[i].size));
                if (memcmp(buf, (const void *)h->addr, sy[i].size))
                {
                    if (C.mismatched_consts++ < 20) fprintf(log, "tick_host: constant %s differs between host and device, unmapped\n", name);
                    continue;
                }
            }
            if (!cst && sy[i].size)
            {
                CK(cuMemcpyHtoD(sy[i].dev, (const void *)h->addr, sy[i].size));
                ++C.copied_globals;
            }
        }
        in[nin++] = (struct nwdev_map){h->addr, sy[i].dev, fn ? 0 : sy[i].size};
        out[nout++] = (struct nwdev_map){sy[i].dev, h->addr, fn ? 0 : sy[i].size};
        ++C.mapped_syms;
    }
    qsort(in, nin, sizeof *in, map_cmp);
    qsort(out, nout, sizeof *out, map_cmp);
    struct nwdev_tabs t = {0};
    CUdeviceptr din, dout;
    CK(cuMemAlloc(&din, (nin + 1) * sizeof *in));
    CK(cuMemAlloc(&dout, (nout + 1) * sizeof *out));
    CK(cuMemcpyHtoD(din, in, nin * sizeof *in));
    CK(cuMemcpyHtoD(dout, out, nout * sizeof *out));
    t.in = (const struct nwdev_map *)din;
    t.out = (const struct nwdev_map *)dout;
    t.nin = nin;
    t.nout = nout;
    t.in_lo = exe_lo;
    t.in_span = exe_hi - exe_lo;
    t.gl_lo = gl_lo;
    t.gl_span = gl_hi > gl_lo ? gl_hi - gl_lo : 0;
    t.fn_lo = fn_lo;
    t.fn_span = fn_hi > fn_lo ? fn_hi - fn_lo : 0;
    /* the functions' hash (inl.c nwdev_out): at most half full */
    uint64_t hn = 1;
    uint32_t nfn = 0;
    for (uint32_t i = 0; i < nout; ++i) nfn += out[i].size == 0;
    while (hn < 2 * (uint64_t)nfn + 2) hn <<= 1;
    uint64_t *fh = calloc(2 * hn, sizeof *fh);
    for (uint32_t i = 0; i < nout; ++i)
    {
        if (out[i].size != 0) continue;
        uint64_t p = out[i].from;
        for (uint64_t h = (p * 0x9e3779b97f4a7c15ull) >> 40;; ++h)
            if (fh[2 * (h & (hn - 1))] == 0)
            {
                fh[2 * (h & (hn - 1))] = p;
                fh[2 * (h & (hn - 1)) + 1] = out[i].to;
                break;
            }
    }
    CUdeviceptr dfh;
    CK(cuMemAlloc(&dfh, 2 * hn * sizeof *fh));
    CK(cuMemcpyHtoD(dfh, fh, 2 * hn * sizeof *fh));
    free(fh);
    t.fn_hash = (const uint64_t *)dfh;
    t.fn_mask = hn - 1;
    CK(cuModuleGetGlobal(&d, &sz, C.mod, "nwdev_tabs"));
    CK(cuMemcpyHtoD(d, &t, sizeof t));
    fprintf(log, "tick_host: %u device symbols, %ld bound to the host's (%ld mutable globals copied), %ld constants left "
                 "unmapped, %ld mutable globals without a host symbol; host executable %#llx-%#llx\n",
            n, C.mapped_syms, C.copied_globals, C.mismatched_consts, C.unmatched_mutable, (unsigned long long)exe_lo,
            (unsigned long long)exe_hi);
    fprintf(log, "tick_host: device globals %#llx-%#llx, functions %#llx-%#llx\n", (unsigned long long)gl_lo,
            (unsigned long long)gl_hi, (unsigned long long)fn_lo, (unsigned long long)fn_hi);
    free(sy);
    free(in);
    free(out);
    free(buf);
    if (fnmap != NULL) fclose(fnmap);
    return 0;
}

static int child_init(FILE *log)
{
    CK(cuInit(0));
    CK(cuDeviceGet(&C.dev, 0));
    CK(cuDevicePrimaryCtxRetain(&C.ctx, C.dev));
    CK(cuCtxSetCurrent(C.ctx));
    CK(cuCtxSetLimit(CU_LIMIT_STACK_SIZE, C.stack ? C.stack : 32768));
    CK(cuModuleLoad(&C.mod, S6_CUBIN));
    CK(cuModuleGetFunction(&C.k_chunkloop, C.mod, "chunkloop"));
    CK(cuModuleGetFunction(&C.k_tick_scatter, C.mod, "tick_scatter"));
    CK(cuModuleGetFunction(&C.k_tick_gather, C.mod, "tick_gather"));
    CK(cuModuleGetFunction(&C.k_tick_clear, C.mod, "tick_clear"));
    CK(cuStreamCreate(&C.s, CU_STREAM_NON_BLOCKING));
    CK(cuEventCreate(&C.e0, CU_EVENT_DEFAULT));
    CK(cuEventCreate(&C.e1, CU_EVENT_DEFAULT));
    memset(&C.prop, 0, sizeof C.prop);
    C.prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    C.prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    C.prop.location.id = 0;
    C.acc.location = C.prop.location;
    C.acc.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    CK(cuMemAlloc(&C.jobs, S6_MAX_ENVS * sizeof(struct nwdev_job)));
    return bind_symbols(log);
}

static int map_one(CUdeviceptr at, CUmemGenericAllocationHandle h, uint64_t off)
{
    CK(cuMemMap(at, S6_GRAN, off, h, 0));
    CK(cuMemSetAccess(at, S6_GRAN, &C.acc, 1));
    return 0;
}

/* granule g of e: real memory, zeroed */
static int make_real(struct cenv *e, size_t g)
{
    if (e->gstate[g] == 2) return 0;
    CUdeviceptr at = e->base + g * S6_GRAN;
    if (e->gstate[g] == 1)
    {
        /* nothing in flight may touch the range it unmaps */
        CK(cuStreamSynchronize(C.s));
        CK(cuMemUnmap(at, S6_GRAN));
        --e->n_zero;
        --C.mapped_zero;
    }
    /* cuMemMap maps a whole allocation (its offset must be 0): one per
     * granule, released at once (the mapping keeps the memory) */
    CUmemGenericAllocationHandle h;
    CK(cuMemCreate(&h, S6_GRAN, &C.prop, 0));
    C.phys_bytes += S6_GRAN;
    if (map_one(at, h, 0) != 0) return -1;
    CK(cuMemRelease(h));
    CK(cuMemsetD8Async(at, 0, S6_GRAN, C.s));
    e->gstate[g] = 2;
    e->real_h[g >> 5] |= 1u << (g & 31);
    e->real_changed = 1;
    ++e->n_real;
    ++C.mapped_real;
    return 0;
}

static int make_zero(struct cenv *e, size_t g)
{
    if (e->gstate[g] != 0) return 0;
    if (map_one(e->base + g * S6_GRAN, e->zero, 0) != 0) return -1;
    if (!e->zero_clear)
    {
        /* a new allocation's bytes are undefined */
        CK(cuMemsetD8Async(e->base + g * S6_GRAN, 0, S6_GRAN, C.s));
        CK(cuStreamSynchronize(C.s));
        e->zero_clear = 1;
    }
    e->gstate[g] = 1;
    ++e->n_zero;
    ++C.mapped_zero;
    return 0;
}

/* The heads of each transfer area's bytes are pinned, S6_PIN_DOWN and
 * S6_PIN_UP bytes (a speedrun row sends 5.3 MB down and 0.34 MB up): a copy
 * from there is a DMA at the link's speed, without the driver's staging
 * through pageable memory (an environment's upload took 1.2 ms of the
 * launch before). The rest of a longer transfer goes the pageable way. */
#define S6_PIN_DOWN ((uint64_t)32 << 20)
#define S6_PIN_UP ((uint64_t)8 << 20)
static void pin_areas(struct cenv *e)
{
    e->pin_down = cuMemHostRegister(e->down.bytes, S6_PIN_DOWN, 0) == CUDA_SUCCESS ? S6_PIN_DOWN : 0;
    e->pin_up = cuMemHostRegister(e->up.bytes, S6_PIN_UP, 0) == CUDA_SUCCESS ? S6_PIN_UP : 0;
}

/* a copy of n bytes between an area and the device, its pinned head and
 * the rest apart */
static int copy_down(CUdeviceptr to, const unsigned char *from, uint64_t n, uint64_t pinned)
{
    uint64_t a = n < pinned ? n : pinned;
    if (a) CK(cuMemcpyHtoDAsync(to, from, a, C.s));
    if (n > a) CK(cuMemcpyHtoDAsync(to + a, from + a, n - a, C.s));
    return 0;
}

static int copy_up(unsigned char *to, CUdeviceptr from, uint64_t n, uint64_t pinned)
{
    uint64_t a = n < pinned ? n : pinned;
    if (a) CK(cuMemcpyDtoHAsync(to, from, a, C.s));
    if (n > a) CK(cuMemcpyDtoHAsync(to + a, from + a, n - a, C.s));
    return 0;
}

static int child_reg(const struct s6_reg *r, int fd, FILE *log)
{
    if (r->id < 0 || r->id >= S6_MAX_ENVS) return -1;
    struct cenv *e = &C.env[r->id];
    memset(e, 0, sizeof *e);
    e->used = 1;
    e->base = r->base;
    e->size = r->size;
    e->shm = mmap(NULL, shm_len(r->size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (e->shm == MAP_FAILED) return -1;
    xfer_views(e->shm, r->size, &e->down, &e->up);
    uint64_t span = (r->size + S6_GRAN - 1) & ~(S6_GRAN - 1);
    CUdeviceptr va;
    CK(cuMemAddressReserve(&va, span, S6_GRAN, r->base, 0));
    if (va != r->base)
    {
        fprintf(log, "tick_host: the device reserved %#llx, not the image's %#llx\n", (unsigned long long)va,
                (unsigned long long)r->base);
        return -1;
    }
    e->ngran = (r->size + S6_GRAN - 1) / S6_GRAN;
    e->gstate = calloc(e->ngran, 1);
    e->real_h = calloc((e->ngran + 31) / 32, 4);
    CK(cuMemAlloc(&e->real_d, (e->ngran + 31) / 32 * 4));
    CK(cuMemsetD8(e->real_d, 0, (e->ngran + 31) / 32 * 4));
    size_t dwords = (r->size / 4096 + 31) / 32;
    CK(cuMemAlloc(&e->dirty, dwords * 4));
    CK(cuMemsetD8(e->dirty, 0, dwords * 4));
    CK(cuMemAlloc(&e->dlist, (size_t)S6_DLCAP * 4));
    CK(cuMemAlloc(&e->scratch, 4096));
    CK(cuMemsetD8(e->scratch, 0, 4096));
    CK(cuMemCreate(&e->zero, S6_GRAN, &C.prop, 0));
    C.phys_bytes += S6_GRAN;
    CK(cuMemAllocHost((void **)&e->sorted, (size_t)S6_DLCAP * 4));
    pin_areas(e);
    CK(cuCtxSynchronize());
    return 0;
}

/* The transfer's buffers, sized for a whole launch: the device staging
 * (pages, and per page its destination or source address and its stage
 * index), and their host halves, pinned so every copy is a DMA that does not
 * wait for the host. */
static int stage_reserve(uint64_t pages)
{
    if (pages <= C.stage_cap) return 0;
    CK(cuStreamSynchronize(C.s));
    if (C.stage_cap)
    {
        cuMemFree(C.stage);
        cuMemFree(C.stage_addr);
        cuMemFree(C.stage_src);
        cuMemFreeHost(C.addr_h);
        cuMemFreeHost(C.src_h);
    }
    uint64_t cap = pages < 4096 ? 4096 : pages * 2;
    CK(cuMemAlloc(&C.stage, cap * 4096));
    CK(cuMemAlloc(&C.stage_addr, cap * 8));
    CK(cuMemAlloc(&C.stage_src, cap * 8));
    CK(cuMemAllocHost((void **)&C.addr_h, cap * 8));
    CK(cuMemAllocHost((void **)&C.src_h, cap * 8));
    C.stage_cap = cap;
    return 0;
}

/* A launch's upload is staged for every environment, then copied and
 * scattered at once (up_begin, up_add for each, up_end): one scatter and
 * one wait a launch, not one an environment. */
static struct {
    uint64_t pages, entries;          /* the stage's pages and the scatter's entries so far */
} U;

static void up_begin(void) { U.pages = U.entries = 0; }

/* the down area's npages into e's image: granules as the pages and the
 * extents need them, the data copied to the stage (the stage holds the
 * launch's pages: up_reserve before) */
static int up_add(struct cenv *e, uint64_t npages, uint64_t ndata, const struct s6_ext *ext, uint32_t next)
{
    for (uint64_t k = 0; k < npages; ++k)
        if (!(e->down.page[k] & IMG_XFER_ZERO) && make_real(e, (size_t)((uint64_t)e->down.page[k] * 4096 / S6_GRAN)) != 0) return -1;
    for (uint32_t i = 0; i < next; ++i)
    {
        uint64_t lo = ext[i].off, hi = ext[i].off + ext[i].len;
        int heap = ext[i].part == IMG_PART_HEAP;
        if (hi <= lo) continue;
        uint64_t end = hi + (heap ? S6_HEAP_MARGIN : S6_GRAN);
        if (end > e->size) end = e->size;
        /* struct env itself (its scratch stacks, the worklists) is written
         * everywhere: real throughout */
        for (uint64_t g = lo / S6_GRAN; g * S6_GRAN < end; ++g)
            if ((ext[i].part == IMG_PART_ENV ? make_real(e, (size_t)g) : make_zero(e, (size_t)g)) != 0) return -1;
        /* the growing end: real */
        for (uint64_t g = (hi ? hi - 1 : 0) / S6_GRAN; g <= (hi - 1) / S6_GRAN + 1 && g < e->ngran; ++g)
            if (make_real(e, (size_t)g) != 0) return -1;
    }
    if (e->real_changed)
    {
        CK(cuMemcpyHtoDAsync(e->real_d, e->real_h, (e->ngran + 31) / 32 * 4, C.s));
        e->real_changed = 0;
    }
    if (npages == 0) return 0;
    int64_t d = (int64_t)U.pages;
    for (uint64_t k = 0; k < npages; ++k)
    {
        uint32_t p = e->down.page[k];
        uint32_t pg = p & ~IMG_XFER_ZERO;
        size_t g = (size_t)((uint64_t)pg * 4096 / S6_GRAN);
        /* a zero page where the device holds only zeros is already there */
        if (p & IMG_XFER_ZERO)
        {
            if (e->gstate[g] != 2) continue;
            C.src_h[U.entries] = -1;
        }
        else C.src_h[U.entries] = d++;
        C.addr_h[U.entries++] = e->base + (uint64_t)pg * 4096;
    }
    if (ndata && copy_down(C.stage + U.pages * 4096, e->down.bytes, ndata * 4096, e->pin_down) != 0) return -1;
    U.pages += ndata;
    return 0;
}

static int up_end(void)
{
    if (U.entries)
    {
        CK(cuMemcpyHtoDAsync(C.stage_addr, C.addr_h, U.entries * 8, C.s));
        CK(cuMemcpyHtoDAsync(C.stage_src, C.src_h, U.entries * 8, C.s));
        void *args[] = {&C.stage_addr, &C.stage_src, &C.stage};
        CK(cuLaunchKernel(C.k_tick_scatter, (unsigned)U.entries, 1, 1, 256, 1, 1, 0, C.s, args, NULL));
    }
    CK(cuStreamSynchronize(C.s));
    return 0;
}

/* one environment's upload by itself */
static int child_upload(struct cenv *e, uint64_t npages, uint64_t ndata, const struct s6_ext *ext, uint32_t next)
{
    if (stage_reserve(npages) != 0) return -1;
    up_begin();
    if (up_add(e, npages, ndata, ext, next) != 0) return -1;
    return up_end();
}

static int u32_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* The launch's download (n jobs): every environment's written pages (the
 * barrier's list, sorted) gathered into the stage by one kernel, their
 * dirty bits cleared by another, and copied to each up area; three waits a
 * launch. */
static int child_download(const struct s6_job *jobs, const struct nwdev_job *dj, uint32_t n, struct s6_res *res)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        struct cenv *e = &C.env[jobs[i].id];
        uint32_t k = dj[i].ndl < S6_DLCAP ? dj[i].ndl : S6_DLCAP;
        res[i].pages_up = e->nsorted = k;
        if (k) CK(cuMemcpyDtoHAsync(e->sorted, e->dlist, (size_t)k * 4, C.s));
        total += k;
    }
    if (total == 0) return 0;
    if (stage_reserve(total) != 0) return -1;
    CK(cuStreamSynchronize(C.s));
    uint64_t m = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        struct cenv *e = &C.env[jobs[i].id];
        qsort(e->sorted, e->nsorted, 4, u32_cmp);
        for (uint32_t k = 0; k < e->nsorted; ++k, ++m)
        {
            C.addr_h[m] = e->base + (uint64_t)e->sorted[k] * 4096;
            C.src_h[m] = (int64_t)(e->dirty + (uint64_t)(e->sorted[k] >> 5) * 4);
        }
    }
    CK(cuMemcpyHtoDAsync(C.stage_addr, C.addr_h, total * 8, C.s));
    CK(cuMemcpyHtoDAsync(C.stage_src, C.src_h, total * 8, C.s));
    void *ga[] = {&C.stage_addr, &C.stage};
    CK(cuLaunchKernel(C.k_tick_gather, (unsigned)total, 1, 1, 256, 1, 1, 0, C.s, ga, NULL));
    uint32_t t32 = (uint32_t)total;
    void *ca[] = {&C.stage_src, &t32};
    CK(cuLaunchKernel(C.k_tick_clear, (t32 + 255) / 256, 1, 1, 256, 1, 1, 0, C.s, ca, NULL));
    m = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        struct cenv *e = &C.env[jobs[i].id];
        if (e->nsorted && copy_up(e->up.bytes, C.stage + m * 4096, (uint64_t)e->nsorted * 4096, e->pin_up) != 0) return -1;
        m += e->nsorted;
    }
    CK(cuStreamSynchronize(C.s));
    for (uint32_t i = 0; i < n; ++i)
    {
        struct cenv *e = &C.env[jobs[i].id];
        memcpy(e->up.page, e->sorted, (size_t)e->nsorted * 4);
    }
    return 0;
}

static int child_launch(int sock, uint32_t n, FILE *log)
{
    static struct s6_job jobs[S6_MAX_ENVS];
    static struct nwdev_job dj[S6_MAX_ENVS];
    static struct s6_res res[S6_MAX_ENVS];
    if (n == 0 || n > S6_MAX_ENVS || full_read(sock, jobs, n * sizeof *jobs) != 0) return -1;
    double t0 = now_ms();
    uint64_t pages = 0;
    for (uint32_t i = 0; i < n; ++i) pages += jobs[i].npages;
    if (stage_reserve(pages) != 0) return -1;
    up_begin();
    for (uint32_t i = 0; i < n; ++i)
    {
        struct cenv *e = &C.env[jobs[i].id];
        if (!e->used) return -1;
        if (up_add(e, jobs[i].npages, jobs[i].ndata, jobs[i].ext, jobs[i].next) != 0) return -1;
        memset(&dj[i], 0, sizeof dj[i]);
        dj[i].env = e->base;
        dj[i].st = jobs[i].st;
        dj[i].phase = jobs[i].phase;
        dj[i].dirty = (uint32_t *)e->dirty;
        dj[i].real = (const uint32_t *)e->real_d;
        dj[i].scratch = (unsigned char *)e->scratch;
        dj[i].dlist = (uint32_t *)e->dlist;
        dj[i].dlcap = S6_DLCAP;
    }
    if (up_end() != 0) return -1;
    CK(cuMemcpyHtoDAsync(C.jobs, dj, n * sizeof *dj, C.s));
    CK(cuStreamSynchronize(C.s));
    double t1 = now_ms();
    void *args[] = {&C.jobs, &n};
    uint32_t w = C.pack;
    CK(cuEventRecord(C.e0, C.s));
    CK(cuLaunchKernel(C.k_chunkloop, (n + w - 1) / w, 1, 1, w, 1, 1, 0, C.s, args, NULL));
    CK(cuEventRecord(C.e1, C.s));
    CK(cuStreamSynchronize(C.s));
    float kms = 0;
    CK(cuEventElapsedTime(&kms, C.e0, C.e1));
    double t2 = now_ms();
    CK(cuMemcpyDtoH(dj, C.jobs, n * sizeof *dj));
    if (child_download(jobs, dj, n, res) != 0) return -1;
    for (uint32_t i = 0; i < n; ++i)
    {
        struct cenv *e = &C.env[jobs[i].id];
        uint64_t up = res[i].pages_up;
        memset(&res[i], 0, sizeof res[i]);
        res[i].pages_up = up;
        res[i].id = jobs[i].id;
        /* a block that ended early says why in fault (an abort, a call the
         * device cannot make); without one it stopped where it should not */
        res[i].fault = dj[i].fault | (dj[i].done || dj[i].fault ? 0 : NWDEV_F_UNSUPPORTED);
        res[i].fault_addr = dj[i].fault_addr;
        res[i].writes = dj[i].writes;
        /* the first flagged runs of each environment, named */
        if (res[i].fault && C.notes < 200)
        {
            ++C.notes;
            char what[256] = "";
            uint64_t off = 0, a = res[i].fault_addr;
            if ((res[i].fault & NWDEV_F_UNSUPPORTED) && !(res[i].fault & (NWDEV_F_IN | NWDEV_F_ABORT)))
            {
                for (size_t k = 0; k + 1 < sizeof what; ++k)
                    if (cuMemcpyDtoH(what + k, a + k, 1) != CUDA_SUCCESS || what[k] == 0) break;
                what[sizeof what - 1] = 0;
            }
            else if (res[i].fault & NWDEV_F_ABORT)
            {
                char msg[200] = "";
                for (size_t k = 0; a && k + 1 < sizeof msg; ++k)
                    if (cuMemcpyDtoH(msg + k, a + k, 1) != CUDA_SUCCESS || msg[k] == 0) break;
                msg[sizeof msg - 1] = 0;
                snprintf(what, sizeof what, "abort() after \"%s\"", msg);
            }
            else if (res[i].fault & NWDEV_F_OUT)
                snprintf(what, sizeof what, "the device-only address %#llx stored into the image (%s)", (unsigned long long)a,
                         dev_name_at(a));
            else if (res[i].fault & NWDEV_F_IN)
            {
                const char *n = hs_name_at(a, &off);
                snprintf(what, sizeof what, "the host address %#llx (%s+%#llx)", (unsigned long long)a, n,
                         (unsigned long long)off);
            }
            else snprintf(what, sizeof what, "a write at image offset %#llx", (unsigned long long)(a - e->base));
            fprintf(log, "tick_host: env %d flagged %#x: %s\n", jobs[i].id, res[i].fault, what);
        }
    }
    double t3 = now_ms();
    for (uint32_t i = 0; i < n; ++i)
    {
        res[i].kernel_ms = kms;
        res[i].down_ms = t1 - t0;
        res[i].up_ms = t3 - t2;
    }
    return full_write(sock, res, n * sizeof *res);
}

/* after a flagged run: the host's pages are in the down area; the zero
 * granule and the scratch page are zeroed, the flagged granule made real */
static int child_restore(struct cenv *e, uint64_t npages, uint64_t fault_addr)
{
    CK(cuMemsetD8Async(e->scratch, 0, 4096, C.s));
    /* the zero granule: map it somewhere to clear it (any of its aliases) */
    for (size_t g = 0; g < e->ngran; ++g)
        if (e->gstate[g] == 1)
        {
            CK(cuMemsetD8Async(e->base + g * S6_GRAN, 0, S6_GRAN, C.s));
            break;
        }
    if (fault_addr - e->base < e->size && make_real(e, (size_t)((fault_addr - e->base) / S6_GRAN)) != 0) return -1;
    return child_upload(e, npages, npages, NULL, 0);
}

/* the device side's end: the child's, or with tick_device_root the
 * process's, with the replay's status */
static void child_end(int rc)
{
    if (C.root)
    {
        int st = 0;
        if (waitpid(C.replay, &st, 0) == C.replay) rc = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        else if (rc == 0) rc = 1;
        fflush(NULL);
    }
    _exit(rc);
}

static void child_main(int sock)
{
    if (!C.root) prctl(PR_SET_PDEATHSIG, SIGKILL);
    FILE *log = stderr;
    /* the image window emptied before the device is touched, so the
     * parent's images copied into the fork go and the device can reserve
     * their addresses (a reservation held over the window at cuInit makes
     * the driver place every later one elsewhere; host mappings go top down
     * from near 0x7f.., far above it) */
    if (munmap((void *)IMG_WINDOW, IMG_SLOT * IMG_SLOTS) != 0) child_end(3);
    int32_t ok = child_init(log) == 0 ? 0 : -1;
    if (full_write(sock, &ok, 4) != 0 || ok != 0) child_end(4);
    for (;;)
    {
        struct s6_msg m;
        if (full_read(sock, &m, sizeof m) != 0) child_end(0);
        int32_t rc = 0;
        switch (m.op)
        {
        case OP_REG:
        {
            struct s6_reg r;
            int fd;
            if (recv_fd(sock, &fd, &r, sizeof r) != 0) child_end(5);
            rc = child_reg(&r, fd, log);
            if (full_write(sock, &rc, 4) != 0) child_end(5);
            break;
        }
        case OP_UPLOAD:
        {
            struct s6_job j;
            if (full_read(sock, &j, sizeof j) != 0) child_end(6);
            rc = child_upload(&C.env[j.id], j.npages, j.ndata, j.ext, j.next);
            if (full_write(sock, &rc, 4) != 0) child_end(6);
            break;
        }
        case OP_LAUNCH:
            if (child_launch(sock, m.n, log) != 0)
            {
                fprintf(stderr, "tick_host child: the launch failed\n");
                child_end(7);
            }
            break;
        case OP_RESTORE:
        {
            struct s6_job j;
            if (full_read(sock, &j, sizeof j) != 0) child_end(8);
            rc = child_restore(&C.env[j.id], j.npages, j.st);
            if (full_write(sock, &rc, 4) != 0) child_end(8);
            break;
        }
        case OP_READ:
        {
            /* the device copy of the down area's listed pages, into the up area */
            struct s6_job j;
            if (full_read(sock, &j, sizeof j) != 0) child_end(10);
            struct cenv *e = &C.env[j.id];
            rc = 0;
            for (uint64_t k = 0; k < j.npages && rc == 0; ++k)
            {
                uint32_t pg = e->down.page[k] & ~IMG_XFER_ZERO;
                size_t g = (size_t)((uint64_t)pg * 4096 / S6_GRAN);
                if (e->gstate[g] == 0) memset(e->up.bytes + k * 4096, 0, 4096);
                else if (cuMemcpyDtoH(e->up.bytes + k * 4096, e->base + (uint64_t)pg * 4096, 4096) != CUDA_SUCCESS) rc = -1;
            }
            if (full_write(sock, &rc, 4) != 0) child_end(10);
            break;
        }
        case OP_REPORT:
        {
            char line[512];
            size_t free_b = 0, total = 0;
            cuMemGetInfo(&free_b, &total);
            int k = snprintf(line, sizeof line,
                             "s6 device: %zu real granules and %zu zero-granule mappings over the environments, %.1f MB of "
                             "physical memory taken; the device has %.1f of %.1f GB free\n",
                             C.mapped_real, C.mapped_zero, (double)C.phys_bytes / 1e6, (double)free_b / 1e9,
                             (double)total / 1e9);
            uint32_t len = (uint32_t)k;
            if (full_write(sock, &len, 4) != 0 || full_write(sock, line, len) != 0) child_end(9);
            break;
        }
        case OP_QUIT:
        default:
            child_end(0);
        }
    }
}

/* =============================================================== the parent */

struct tick_env {
    int id;
    struct env *e;
    unsigned char *shm;
    struct img_xfer down, up;
    uint64_t st;
    uint32_t phase;
    int submitted, ready, rc;
    struct tick_result res;
    uint64_t last_fault_addr;
};

static struct {
    int sock;
    pid_t pid;
    int started;
    int nenv;
    pthread_mutex_t mu;
    int live;
    int nsub;
    struct tick_env *sub[S6_MAX_ENVS];
    void (*yield)(void);
} P = {.sock = -1, .mu = PTHREAD_MUTEX_INITIALIZER, .live = 1};

void tick_device_root(int on) { C.root = on; }
void tick_device_pack(int n) { C.pack = n >= 1 && n <= NWDEV_PACK ? (uint32_t)n : 1; }
void tick_device_fnmap(const char *path) { snprintf(C.fnmap, sizeof C.fnmap, "%s", path); }

int tick_start(size_t stack_bytes)
{
    if (P.started) return 0;
    C.stack = stack_bytes;
    if (C.pack == 0) C.pack = 1;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (C.root && pid > 0)
    {
        /* the device here, the replay goes on in the fork */
        close(sv[0]);
        C.replay = pid;
        child_main(sv[1]);
        child_end(0);
    }
    if (C.root)
    {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        pid = getppid();
    }
    else if (pid == 0)
    {
        close(sv[0]);
        child_main(sv[1]);
        _exit(0);
    }
    close(sv[1]);
    P.sock = sv[0];
    P.pid = pid;
    int32_t ok = -1;
    if (full_read(P.sock, &ok, 4) != 0 || ok != 0)
    {
        int st = 0;
        if (!C.root) waitpid(pid, &st, 0);
        fprintf(stderr, "tick_host: the device child did not start (exit %d)\n", WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st));
        return -1;
    }
    P.started = 1;
    return 0;
}

void tick_stop(void)
{
    if (!P.started) return;
    struct s6_msg m = {OP_QUIT, 0};
    (void)full_write(P.sock, &m, sizeof m);
    close(P.sock);
    if (!C.root) waitpid(P.pid, NULL, 0);
    P.started = 0;
}

void tick_live(int n) { P.live = n; }
void tick_set_yield(void (*yield)(void)) { P.yield = yield; }

struct tick_env *tick_env_add(struct env *e)
{
    if (!P.started || P.nenv == S6_MAX_ENVS || !e->img.size) return NULL;
    struct tick_env *ce = calloc(1, sizeof *ce);
    ce->id = P.nenv++;
    ce->e = e;
    int fd = memfd_create("nw-s6", MFD_CLOEXEC);
    uint64_t len = shm_len(e->img.size);
    if (fd < 0 || ftruncate(fd, (off_t)len) != 0) return NULL;
    ce->shm = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ce->shm == MAP_FAILED) return NULL;
    xfer_views(ce->shm, e->img.size, &ce->down, &ce->up);
    struct s6_msg m = {OP_REG, 0};
    struct s6_reg r = {ce->id, 0, (uint64_t)(uintptr_t)e, e->img.size};
    int32_t rc = -1;
    if (full_write(P.sock, &m, sizeof m) != 0 || send_fd(P.sock, fd, &r, sizeof r) != 0 || full_read(P.sock, &rc, 4) != 0 || rc != 0)
    {
        close(fd);
        return NULL;
    }
    close(fd);
    return ce;
}

struct img_xfer *tick_down(struct tick_env *ce) { return &ce->down; }
struct img_xfer *tick_up(struct tick_env *ce) { return &ce->up; }

static void job_of(struct tick_env *ce, struct s6_job *j)
{
    memset(j, 0, sizeof *j);
    j->id = ce->id;
    j->st = ce->st;
    j->phase = ce->phase;
    j->npages = ce->down.npages;
    j->ndata = ce->down.ndata;
    struct img_extent ext[S6_MAX_EXT];
    int n = image_extents(ce->e, ext, S6_MAX_EXT);
    for (int i = 0; i < n; ++i) j->ext[i] = (struct s6_ext){ext[i].off, ext[i].len, ext[i].part, 0};
    j->next = (uint32_t)n;
}

int tick_upload(struct tick_env *ce)
{
    struct s6_msg m = {OP_UPLOAD, 0};
    struct s6_job j;
    job_of(ce, &j);
    int32_t rc = -1;
    if (full_write(P.sock, &m, sizeof m) != 0 || full_write(P.sock, &j, sizeof j) != 0 || full_read(P.sock, &rc, 4) != 0) return -1;
    return rc;
}

/* the submitted batch, under P.mu */
static int launch_locked(void)
{
    static struct s6_job jobs[S6_MAX_ENVS];
    static struct s6_res res[S6_MAX_ENVS];
    uint32_t n = (uint32_t)P.nsub;
    for (uint32_t i = 0; i < n; ++i) job_of(P.sub[i], &jobs[i]);
    struct s6_msg m = {OP_LAUNCH, n};
    int bad = full_write(P.sock, &m, sizeof m) != 0 || full_write(P.sock, jobs, n * sizeof *jobs) != 0 ||
              full_read(P.sock, res, n * sizeof *res) != 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        struct tick_env *ce = P.sub[i];
        ce->rc = bad ? -1 : 0;
        memset(&ce->res, 0, sizeof ce->res);
        if (!bad)
        {
            ce->res.fault = res[i].fault;
            ce->res.fault_addr = res[i].fault_addr;
            ce->res.writes = res[i].writes;
            ce->res.pages_up = res[i].pages_up;
            ce->res.kernel_ms = res[i].kernel_ms;
            ce->res.down_ms = res[i].down_ms;
            ce->res.up_ms = res[i].up_ms;
            ce->res.batch = n;
            ce->up.base = (uint64_t)(uintptr_t)ce->e;
            ce->up.size = ce->e->img.size;
            ce->up.full = 0;
            ce->up.npages = ce->up.ndata = res[i].pages_up;
        }
        ce->submitted = 0;
        ce->ready = 1;
    }
    P.nsub = 0;
    return bad ? -1 : 0;
}

int tick_run(struct tick_env *ce, uint64_t st, struct tick_result *res)
{
    return tick_run_phase(ce, st, PH_S6, res);
}

int tick_run_phase(struct tick_env *ce, uint64_t st, int phase, struct tick_result *res)
{
    pthread_mutex_lock(&P.mu);
    ce->st = st;
    ce->phase = (uint32_t)phase;
    ce->ready = 0;
    ce->submitted = 1;
    P.sub[P.nsub++] = ce;
    if (P.nsub >= P.live) launch_locked();
    pthread_mutex_unlock(&P.mu);
    while (!__atomic_load_n(&ce->ready, __ATOMIC_ACQUIRE))
    {
        if (P.yield == NULL)
        {
            fprintf(stderr, "tick_host: a run waits for %d more runners and there is no yield hook\n", P.live - P.nsub);
            return -1;
        }
        P.yield();
    }
    *res = ce->res;
    return ce->rc;
}

void tick_runner_done(void)
{
    pthread_mutex_lock(&P.mu);
    --P.live;
    if (P.nsub > 0 && P.nsub >= P.live) launch_locked();
    pthread_mutex_unlock(&P.mu);
}

int tick_restore(struct tick_env *ce)
{
    /* the pages the device wrote, from the host image the run left alone */
    size_t n = ce->up.npages;
    for (size_t k = 0; k < n; ++k)
    {
        ce->down.page[k] = ce->up.page[k];
        memcpy(ce->down.bytes + k * 4096, (const unsigned char *)ce->e + (size_t)ce->up.page[k] * 4096, 4096);
    }
    struct s6_msg m = {OP_RESTORE, 0};
    struct s6_job j;
    memset(&j, 0, sizeof j);
    j.id = ce->id;
    j.st = ce->res.fault_addr;
    j.npages = j.ndata = n;
    int32_t rc = -1;
    if (full_write(P.sock, &m, sizeof m) != 0 || full_write(P.sock, &j, sizeof j) != 0 || full_read(P.sock, &rc, 4) != 0) return -1;
    return rc;
}

int tick_read(struct tick_env *ce, size_t n)
{
    struct s6_msg m = {OP_READ, 0};
    struct s6_job j;
    memset(&j, 0, sizeof j);
    j.id = ce->id;
    j.npages = n;
    int32_t rc = -1;
    if (full_write(P.sock, &m, sizeof m) != 0 || full_write(P.sock, &j, sizeof j) != 0 || full_read(P.sock, &rc, 4) != 0) return -1;
    return rc;
}

void tick_report(FILE *to)
{
    if (!P.started) return;
    struct s6_msg m = {OP_REPORT, 0};
    uint32_t len = 0;
    char line[600];
    if (full_write(P.sock, &m, sizeof m) != 0 || full_read(P.sock, &len, 4) != 0 || len >= sizeof line ||
        full_read(P.sock, line, len) != 0)
        return;
    line[len] = 0;
    fputs(line, to);
}
