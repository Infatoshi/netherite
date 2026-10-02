/* The light capture's device consumer (lightcap.h struct lc_sink): the
 * stream goes to a child process, forked before the device is touched as
 * worldgen/ahead.c's generators are (the grave's scan of the replay's own
 * mappings faults on a CUDA driver thread's stack), through two shared
 * buffers: the replay fills one while the device replays the other. Linked
 * only into the CUDA build (out/native/cuda/light_snapshots). */
#define _GNU_SOURCE
#include "lighting.h"
#include "../../engine/lightdefer.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CL_BUF ((size_t)64 << 20)

struct cl_shm {
    struct lighting_result res;
    int32_t rc;
    uint8_t buf[2][CL_BUF];
};

struct cl_msg {
    int32_t b;       /* the buffer, -1 to finish */
    uint32_t op;     /* CL_OP_* */
    uint64_t n;
};

/* a message's work: replay the buffer (the capture, or the deferred mode's
 * records), or replay it and collect the written slots into the back area
 * (the deferred mode's sync) */
enum { CL_OP_RUN = 0, CL_OP_SYNC = 1 };

struct cl_client {
    struct cl_shm *shm;
    int fd;
    pid_t pid;
    int cur, pending[2], failed;
    double wait_s;
    struct lc_sink sink;
    /* the deferred mode's back area (shared with the child) */
    uint8_t *back;
    size_t back_cap;
    size_t cap;                 /* the stream buffers' bytes in use */
};

/* the deferred mode's stream buffers: a sync's records and the puts of the
 * chunks it reads for the first time */
#define CL_DEFER_BUF ((size_t)16 << 20)

/* the deferred mode's back area for NSLOTS slots */
static size_t back_bytes(int nslots)
{
    struct lc_layout L;
    int nback = nslots / 2 + LC_WIN_N < nslots ? nslots / 2 + LC_WIN_N : nslots;

    lightcap_layout(&L);
    return sizeof(struct lc_back) + (size_t)nback * (sizeof(struct lc_after) + ((L.chunk_bytes + 7) & ~(size_t)7) + 16 * 4096);
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

static double now_s(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void serve(int fd, struct cl_shm *shm, uint8_t *back, size_t back_cap, int nslots, size_t cap)
{
    struct lc_layout L;
    uint8_t op[4096], lt[4096], air[4096];

    lightcap_layout(&L);
    lightcap_blocks(op, lt, air);

    struct lighting *g = lighting_create_n(&L, op, lt, air, cap, nslots, -1);
    char ok = g != NULL;

    if (full_write(fd, &ok, 1) || !ok) _exit(1);
    for (;;)
    {
        struct cl_msg m;
        char ack;

        if (full_read(fd, &m, sizeof m)) break;
        if (m.b < 0)
        {
            lighting_result(g, &shm->res);
            ack = 0;
            full_write(fd, &ack, 1);
            break;
        }
        int rc = shm->rc != 0 ? shm->rc : lighting_run(g, shm->buf[m.b], m.n);
        size_t bn = 0;

        /* the deferred mode's sync: the written slots back (a record the
         * device could not run is in the back stream's status) */
        if (m.op == CL_OP_SYNC && rc >= 0 && lighting_collect(g, back, back_cap, &bn)) rc = -1;
        if (m.op == CL_OP_SYNC && rc == 1) rc = 0;
        if (rc != 0)
        {
            lighting_result(g, &shm->res);
            shm->rc = rc;
        }
        ack = (char)(rc != 0);
        if (full_write(fd, &ack, 1)) break;
    }
    lighting_free(g);
    _exit(0);
}

/* the oldest submitted buffer's turn back */
static int wait_ack(struct cl_client *k, int b)
{
    char ack;
    double t = now_s();

    if (!k->pending[b]) return 0;
    if (full_read(k->fd, &ack, 1)) ack = 1;
    k->wait_s += now_s() - t;
    k->pending[b] = 0;
    if (ack) k->failed = 1;
    return ack ? -1 : 0;
}

static uint8_t *cl_submit(void *ctx, size_t n)
{
    struct cl_client *k = ctx;
    struct cl_msg m = {k->cur, 0, n};
    int other = 1 - k->cur;

    if (k->failed || full_write(k->fd, &m, sizeof m)) return NULL;
    k->pending[k->cur] = 1;
    if (wait_ack(k, other)) return NULL;
    k->cur = other;
    k->sink.cap = k->cap;
    return k->shm->buf[other];
}

/* the deferred mode's sync: the last buffer run and collected after the
 * other, then the back area */
static int cl_sync(void *ctx, size_t n, const uint8_t **back)
{
    struct cl_client *k = ctx;
    struct cl_msg m = {k->cur, CL_OP_SYNC, n};
    int other = 1 - k->cur;

    *back = NULL;
    if (k->failed || full_write(k->fd, &m, sizeof m)) return -1;
    k->pending[k->cur] = 1;
    if (wait_ack(k, other) || wait_ack(k, k->cur)) return -1;
    k->sink.buf = k->shm->buf[k->cur];
    k->sink.cap = k->cap;
    *back = k->back;
    return 0;
}

static int cl_finish(void *ctx, FILE *out)
{
    struct cl_client *k = ctx;
    const struct lighting_result *r = &k->shm->res;
    struct cl_msg m = {-1, 0, 0};
    char ack;

    /* in submission order: the other buffer went first */
    wait_ack(k, 1 - k->cur);
    wait_ack(k, k->cur);
    if (full_write(k->fd, &m, sizeof m) || full_read(k->fd, &ack, 1))
    {
        fprintf(out, "light device: the device process died\n");
        return -1;
    }
    return lighting_report(r, k->failed, k->wait_s, out);
}

static void cl_free(void *ctx)
{
    struct cl_client *k = ctx;

    close(k->fd);
    waitpid(k->pid, NULL, 0);
    munmap(k->shm, sizeof *k->shm);
    if (k->back != NULL) munmap(k->back, k->back_cap);
    free(k);
}

/* the device process, with a mirror of nslots slots (and the deferred
 * mode's back area when defer) */
static struct lc_sink *sink_fork(int nslots, int defer)
{
    struct cl_shm *shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    size_t back_cap = defer ? back_bytes(nslots) : 0;
    uint8_t *back = NULL;
    int sv[2];

    if (shm == MAP_FAILED) return NULL;
    if (defer && (back = mmap(NULL, back_cap, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
    {
        munmap(shm, sizeof *shm);
        return NULL;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
    {
        munmap(shm, sizeof *shm);
        if (back != NULL) munmap(back, back_cap);
        return NULL;
    }
    fflush(NULL);

    pid_t pid = fork();

    if (pid == 0)
    {
        close(sv[0]);
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        serve(sv[1], shm, back, back_cap, nslots, defer ? CL_DEFER_BUF : CL_BUF);
    }
    close(sv[1]);

    char ok = 0;

    if (pid < 0 || full_read(sv[0], &ok, 1) || !ok)
    {
        fprintf(stderr, "light capture: the device process did not start\n");
        close(sv[0]);
        if (pid > 0) waitpid(pid, NULL, 0);
        munmap(shm, sizeof *shm);
        if (back != NULL) munmap(back, back_cap);
        return NULL;
    }

    struct cl_client *k = calloc(1, sizeof *k);

    k->shm = shm;
    k->fd = sv[0];
    k->pid = pid;
    k->back = back;
    k->back_cap = back_cap;
    k->cap = defer ? CL_DEFER_BUF : CL_BUF;
    k->sink = (struct lc_sink){k, shm->buf[0], k->cap, cl_submit, cl_finish, cl_free, defer ? cl_sync : NULL};
    return &k->sink;
}

struct lc_sink *lc_sink_cuda_create(void)
{
    return sink_fork(LC_SLOTS, 0);
}

/* ------------------------------------- the deferred client light's executor */

/* the mirror's slots per environment: the client world's chunks at render
 * distance 4 to 8 and the windows around them, with room for a sync's half */
#define CL_DEFER_SLOTS 512

static int ex_run(void *ctx, struct world *w, const struct ld_op *ops, size_t n)
{
    return lightcap_defer_run(ctx, w, ops, n);
}

static void ex_forget(void *ctx, const struct world *w)
{
    lightcap_defer_forget(ctx, w);
}

static void ex_free(void *ctx)
{
    lightcap_free(ctx);
}

static int exec_on(struct lightdefer_exec *ex, struct lc_sink *sink, int nslots)
{
    struct lightcap *c = sink != NULL ? lightcap_new_defer(sink, nslots) : NULL;

    if (c == NULL)
    {
        if (sink != NULL && sink->free != NULL) sink->free(sink->ctx);
        return -1;
    }
    *ex = (struct lightdefer_exec){c, ex_run, ex_forget, ex_free};
    return 0;
}

int lc_defer_exec_cuda(struct lightdefer_exec *ex)
{
    return exec_on(ex, sink_fork(CL_DEFER_SLOTS, 1), CL_DEFER_SLOTS);
}

/* the device in this process: each buffer run as it is handed over */
struct cl_inproc {
    struct lighting *g;
    uint8_t *buf, *back;
    size_t back_cap;
    int failed;
    struct lc_sink sink;
};

static uint8_t *ip_submit(void *ctx, size_t n)
{
    struct cl_inproc *k = ctx;

    if (k->failed || lighting_run(k->g, k->buf, n) < 0) k->failed = 1;
    return k->failed ? NULL : k->buf;
}

static int ip_sync(void *ctx, size_t n, const uint8_t **back)
{
    struct cl_inproc *k = ctx;
    size_t bn;

    *back = NULL;
    if (k->failed || (n > 0 && lighting_run(k->g, k->buf, n) < 0) || lighting_collect(k->g, k->back, k->back_cap, &bn))
    {
        k->failed = 1;
        return -1;
    }
    k->sink.buf = k->buf;
    *back = k->back;
    return 0;
}

static int ip_finish(void *ctx, FILE *out)
{
    struct cl_inproc *k = ctx;

    (void)out;
    return k->failed ? -1 : 0;
}

static void ip_free(void *ctx)
{
    struct cl_inproc *k = ctx;

    lighting_free(k->g);
    free(k->buf);
    free(k->back);
    free(k);
}

/* the in-process stream buffer: a sync's records, and the puts of chunks
 * seen for the first time (a chunk load's burst) */
#define CL_IP_BUF ((size_t)16 << 20)

int lc_defer_exec_cuda_inproc(struct lightdefer_exec *ex, int dev, int nslots)
{
    struct lc_layout L;
    uint8_t op[4096], lt[4096], air[4096];

    if (nslots <= 0) nslots = CL_DEFER_SLOTS;
    lightcap_layout(&L);
    lightcap_blocks(op, lt, air);

    struct cl_inproc *k = calloc(1, sizeof *k);

    if (k == NULL) return -1;
    k->g = lighting_create_n(&L, op, lt, air, CL_IP_BUF, nslots, dev);
    k->back_cap = k->g != NULL ? lighting_collect_cap(k->g) : 0;
    k->buf = malloc(CL_IP_BUF);
    k->back = k->back_cap ? malloc(k->back_cap) : NULL;
    if (k->g == NULL || k->buf == NULL || k->back == NULL)
    {
        lighting_free(k->g);
        free(k->buf);
        free(k->back);
        free(k);
        return -1;
    }
    k->sink = (struct lc_sink){k, k->buf, CL_IP_BUF, ip_submit, ip_finish, ip_free, ip_sync};
    return exec_on(ex, &k->sink, nslots);
}
