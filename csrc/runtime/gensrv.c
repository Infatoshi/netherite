/* The generation server and its clients (gensrv.h). */
#define _GNU_SOURCE
#include "gensrv.h"
#include "../engine/genahead.h"

#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define GS_BATCHERS 4
/* a client's rounds: at most this many requests a call (the pool splits a
 * larger round into calls of max_batch) */
#define GS_CLIENT_BATCH 512

static uint64_t gs_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void gs_err(char *err, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (err && n) vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

/* path's address: a name starting with @ is in the abstract namespace (no
 * file); returns its length, 0 when too long */
static socklen_t gs_addr(const char *path, struct sockaddr_un *a)
{
    size_t k = strlen(path);
    memset(a, 0, sizeof *a);
    a->sun_family = AF_UNIX;
    if (k == 0 || k >= sizeof a->sun_path) return 0;
    memcpy(a->sun_path, path, k);
    if (path[0] == '@') a->sun_path[0] = 0;
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + k + (path[0] != '@'));
}

static int gs_full(int fd, void *b, size_t n, int wr)
{
    for (size_t k = 0; k < n;)
    {
        ssize_t r = wr ? write(fd, (char *)b + k, n - k) : read(fd, (char *)b + k, n - k);
        if (r <= 0)
        {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
        k += (size_t)r;
    }
    return 0;
}

/* ----------------------------------------------------------- the batches */

/* one caller's run_built, waiting for a batcher */
struct gs_job {
    const struct ga_req *req;
    int n;
    struct ga_built *built;
    uint8_t *bands, *ids, *biome, *tops;
    uint64_t *rand;
    int rc, top, done;
    uint64_t t_queued;
    struct gs_job *next;
};

struct gs_batcher {
    struct gensrv *s;
    struct ga_gen *gen;
    pthread_t th;
    int started;
    /* the batch's buffers (the generator's pinned pages): its unique
     * requests, and each caller's request's slot among them */
    struct ga_req *req;
    int *slot, *tbl;
    size_t tcap;
    struct ga_built *built;
    uint8_t *bands, *ids, *biome, *tops;
    uint64_t *rand;
};

struct gs_client {
    struct gensrv *s;
    int fd;
    uint8_t *map;
    size_t size;
    pthread_t th;
    struct gs_client *next;
};

struct gensrv {
    pthread_mutex_t mu;
    pthread_cond_t work, done, left;
    struct gs_job *head, *tail;
    int stop;
    int nb;
    struct gs_batcher b[GS_BATCHERS];
    int max_batch;
    char path[108];
    int lfd;
    pthread_t acceptor;
    int accepting;
    int nclients;               /* connected */
    struct gs_client *clients;  /* every connection made (joined at the stop) */
    struct gensrv_stats st;
};

static void *gs_host(const struct ga_gen *g, size_t bytes)
{
    void *p = g->host_alloc ? g->host_alloc(g->ctx, bytes) : NULL;
    return p != NULL ? p : malloc(bytes);
}

static void gs_host_free(const struct ga_gen *g, void *p)
{
    if (p == NULL) return;
    if (g->host_free) g->host_free(g->ctx, p);
    else free(p);
}

/* a job's results out of the batch's buffers (its requests' slots from at) */
static void gs_scatter(const struct gs_batcher *b, struct gs_job *j, int at)
{
    for (int k = 0; k < j->n; ++k)
    {
        size_t i = (size_t)b->slot[at + k], o = (size_t)k;
        if (j->req[k].dim == 0)
        {
            const struct ga_built *h = &b->built[i];
            j->built[o] = *h;
            size_t nb = h->mask != 0 ? (size_t)(32 - __builtin_clz(h->mask)) : 0;
            if (nb) memcpy(j->bands + o * 16 * GA_BAND_BYTES, b->bands + i * 16 * GA_BAND_BYTES, nb * GA_BAND_BYTES);
            if (h->mask > 0 && (int)nb - 1 > j->top) j->top = (int)nb - 1;
        }
        else
            memcpy(j->ids + o * GA_CELLS, b->ids + i * GA_CELLS, GA_CELLS);
        memcpy(j->biome + o * 256, b->biome + i * 256, 256);
        memcpy(j->tops + o * 256, b->tops + i * 256, 256);
        j->rand[o] = b->rand[i];
    }
}

static void *gs_batch_main(void *arg)
{
    struct gs_batcher *b = arg;
    struct gensrv *s = b->s;
    struct gs_job *take[256];
    pthread_mutex_lock(&s->mu);
    for (;;)
    {
        while (!s->stop && s->head == NULL) pthread_cond_wait(&s->work, &s->mu);
        if (s->head == NULL) break;
        /* every waiting call, in order, up to max_batch requests */
        int m = 0, total = 0;
        while (s->head != NULL && m < 256 && total + s->head->n <= s->max_batch)
        {
            take[m++] = s->head;
            total += s->head->n;
            s->head = s->head->next;
        }
        if (s->head == NULL) s->tail = NULL;
        uint64_t t0 = gs_now();
        for (int i = 0; i < m; ++i) s->st.queued_ns += t0 - take[i]->t_queued;
        pthread_mutex_unlock(&s->mu);

        /* the unique requests (processes of one seed ask for the same
         * chunks), each caller's request given its slot */
        int nu = 0;
        memset(b->tbl, 0xff, b->tcap * sizeof *b->tbl);
        for (int i = 0, at = 0; i < m; at += take[i]->n, ++i)
            for (int k = 0; k < take[i]->n; ++k)
            {
                const struct ga_req *r = &take[i]->req[k];
                uint64_t h = (uint64_t)r->seed * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uint32_t)r->cx * 0xC2B2AE3D27D4EB4Full ^
                             (uint64_t)(uint32_t)r->cz * 0x165667B19E3779F9ull ^ (uint64_t)(uint32_t)(r->dim + 1);
                size_t q = (h ^ h >> 29) & (b->tcap - 1);
                for (;; q = (q + 1) & (b->tcap - 1))
                {
                    int u = b->tbl[q];
                    if (u < 0)
                    {
                        b->tbl[q] = nu;
                        b->req[nu++] = *r;
                        break;
                    }
                    if (b->req[u].seed == r->seed && b->req[u].dim == r->dim && b->req[u].cx == r->cx && b->req[u].cz == r->cz)
                        break;
                }
                b->slot[at + k] = b->tbl[q];
            }
        int top = -1;
        int rc = b->gen->run_built(b->gen->ctx, b->req, nu, b->built, b->bands, &top, b->ids, b->biome, b->tops, b->rand);
        uint64_t t1 = gs_now();
        for (int i = 0, at = 0; i < m; at += take[i]->n, ++i)
        {
            take[i]->rc = rc;
            take[i]->top = -1;
            if (rc == 0) gs_scatter(b, take[i], at);
        }
        uint64_t t2 = gs_now();

        pthread_mutex_lock(&s->mu);
        ++s->st.rounds;
        s->st.calls += (uint64_t)m;
        s->st.requests += (uint64_t)total;
        s->st.unique += (uint64_t)nu;
        if ((uint64_t)total > s->st.max_requests) s->st.max_requests = (uint64_t)total;
        s->st.run_ns += t1 - t0;
        s->st.scatter_ns += t2 - t1;
        for (int i = 0; i < m; ++i) take[i]->done = 1;
        pthread_cond_broadcast(&s->done);
    }
    pthread_mutex_unlock(&s->mu);
    return NULL;
}

/* a call joins the next batch and waits for its results */
static int gs_call(struct gensrv *s, struct gs_job *j)
{
    if (j->n <= 0) return 0;
    if (j->n > s->max_batch) return -1;
    j->done = 0;
    j->next = NULL;
    pthread_mutex_lock(&s->mu);
    j->t_queued = gs_now();
    if (s->tail) s->tail->next = j;
    else s->head = j;
    s->tail = j;
    pthread_cond_signal(&s->work);
    while (!j->done) pthread_cond_wait(&s->done, &s->mu);
    pthread_mutex_unlock(&s->mu);
    return j->rc;
}

/* ------------------------------------------------------- the local front */

static int front_run(void *ctx, const struct ga_req *req, int n, uint8_t *ids, uint8_t *metas, uint8_t *biome,
                     uint8_t *tops, uint64_t *rand)
{
    (void)ctx, (void)req, (void)n, (void)ids, (void)metas, (void)biome, (void)tops, (void)rand;
    return -1;   /* raw overworld arrays are not served */
}

static int front_run_built(void *ctx, const struct ga_req *req, int n, struct ga_built *built, uint8_t *bands,
                           int *top_band, uint8_t *ids, uint8_t *biome, uint8_t *tops, uint64_t *rand)
{
    struct gs_job j = {.req = req, .n = n, .built = built, .bands = bands, .ids = ids, .biome = biome, .tops = tops,
                       .rand = rand};
    int rc = gs_call(ctx, &j);
    if (top_band) *top_band = j.top;
    return rc;
}

static void front_free(void *ctx) { (void)ctx; }

static struct ga_gen *front_new(struct gensrv *s);

static struct ga_gen *front_another(const struct ga_gen *g) { return front_new(g->ctx); }

static struct ga_gen *front_new(struct gensrv *s)
{
    struct ga_gen *g = calloc(1, sizeof *g);
    if (g == NULL) return NULL;
    g->name = "gensrv";
    g->ctx = s;
    g->max_batch = s->max_batch;
    g->run = front_run;
    g->run_built = front_run_built;
    g->free = front_free;
    g->another = front_another;
    return g;
}

struct ga_gen *gensrv_front(struct gensrv *s) { return front_new(s); }

/* ------------------------------------------------------------ the socket */

/* the request a client sends: its call's slots as offsets in its mapping */
struct gs_msg {
    int32_t n, pad;
    uint64_t req, built, bands, ids, biome, tops, rand;
};
struct gs_reply { int32_t rc, top; };
#define GS_HELLO 0x4e57475352563031ull   /* "NWGSRV01" */

static int gs_in(const struct gs_client *c, uint64_t off, size_t bytes)
{
    return off <= c->size && bytes <= c->size - off;
}

static void *gs_client_main(void *arg)
{
    struct gs_client *c = arg;
    struct gensrv *s = c->s;
    struct gs_msg m;
    while (gs_full(c->fd, &m, sizeof m, 0) == 0)
    {
        struct gs_reply r = {-1, -1};
        size_t n = m.n > 0 ? (size_t)m.n : 0;
        if (m.n >= 0 && m.n <= s->max_batch && gs_in(c, m.req, n * sizeof(struct ga_req)) &&
            gs_in(c, m.built, n * sizeof(struct ga_built)) && gs_in(c, m.bands, n * 16 * GA_BAND_BYTES) &&
            gs_in(c, m.ids, n * GA_CELLS) && gs_in(c, m.biome, n * 256) && gs_in(c, m.tops, n * 256) &&
            gs_in(c, m.rand, n * 8))
        {
            struct gs_job j = {.req = (const struct ga_req *)(c->map + m.req), .n = m.n,
                               .built = (struct ga_built *)(c->map + m.built), .bands = c->map + m.bands,
                               .ids = c->map + m.ids, .biome = c->map + m.biome, .tops = c->map + m.tops,
                               .rand = (uint64_t *)(c->map + m.rand)};
            r.rc = gs_call(s, &j);
            r.top = j.top;
        }
        if (gs_full(c->fd, &r, sizeof r, 1)) break;
    }
    pthread_mutex_lock(&s->mu);
    --s->nclients;
    pthread_cond_broadcast(&s->left);
    pthread_mutex_unlock(&s->mu);
    return NULL;
}

/* a client's mapping: its memfd, passed with its size */
static int gs_recv_fd(int fd, uint64_t *hello, uint64_t *size)
{
    uint64_t v[2];
    char cb[CMSG_SPACE(sizeof(int))];
    struct iovec io = {v, sizeof v};
    struct msghdr mh = {0};
    mh.msg_iov = &io;
    mh.msg_iovlen = 1;
    mh.msg_control = cb;
    mh.msg_controllen = sizeof cb;
    if (recvmsg(fd, &mh, MSG_WAITALL) != (ssize_t)sizeof v) return -1;
    struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
    if (cm == NULL || cm->cmsg_type != SCM_RIGHTS) return -1;
    int mfd;
    memcpy(&mfd, CMSG_DATA(cm), sizeof mfd);
    *hello = v[0];
    *size = v[1];
    return mfd;
}

static void *gs_accept_main(void *arg)
{
    struct gensrv *s = arg;
    for (;;)
    {
        int fd = accept(s->lfd, NULL, NULL);
        if (fd < 0)
        {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            break;   /* the listener was shut down */
        }
        uint64_t hello = 0, size = 0;
        int mfd = gs_recv_fd(fd, &hello, &size);
        void *map = mfd >= 0 && hello == GS_HELLO && size > 0
                        ? mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0) : MAP_FAILED;
        if (mfd >= 0) close(mfd);
        char ok = map != MAP_FAILED;
        struct gs_client *c = ok ? calloc(1, sizeof *c) : NULL;
        if (c == NULL || gs_full(fd, &ok, 1, 1))
        {
            if (map != MAP_FAILED) munmap(map, (size_t)size);
            free(c);
            close(fd);
            continue;
        }
        c->s = s;
        c->fd = fd;
        c->map = map;
        c->size = (size_t)size;
        pthread_mutex_lock(&s->mu);
        ++s->nclients;
        ++s->st.clients;
        c->next = s->clients;
        s->clients = c;
        pthread_mutex_unlock(&s->mu);
        pthread_create(&c->th, NULL, gs_client_main, c);
    }
    return NULL;
}

struct gensrv *gensrv_start(const char *path, struct ga_gen *const *gens, int n, char *err, size_t errn)
{
    struct sockaddr_un a;
    socklen_t al = gs_addr(path, &a);
    if (n < 1 || al == 0) { gs_err(err, errn, "gensrv: no generator or a long path"); return NULL; }
    struct gensrv *s = calloc(1, sizeof *s);
    pthread_mutex_init(&s->mu, NULL);
    pthread_cond_init(&s->work, NULL);
    pthread_cond_init(&s->done, NULL);
    pthread_cond_init(&s->left, NULL);
    s->max_batch = gens[0]->max_batch;
    s->nb = n < GS_BATCHERS ? n : GS_BATCHERS;
    for (int i = 0; i < s->nb; ++i)
    {
        struct gs_batcher *b = &s->b[i];
        size_t B = (size_t)gens[i]->max_batch;
        if (gens[i]->run_built == NULL || gens[i]->max_batch < s->max_batch)
        { gs_err(err, errn, "gensrv: a generator without run_built"); gensrv_stop(s, 0); return NULL; }
        b->s = s;
        b->gen = gens[i];
        b->req = malloc(sizeof *b->req * B);
        b->slot = malloc(sizeof *b->slot * B);
        for (b->tcap = 16; b->tcap < 2 * B; b->tcap *= 2) {}
        b->tbl = malloc(sizeof *b->tbl * b->tcap);
        b->built = gs_host(b->gen, sizeof *b->built * B);
        b->bands = gs_host(b->gen, B * 16 * GA_BAND_BYTES);
        b->ids = gs_host(b->gen, B * GA_CELLS);
        b->biome = gs_host(b->gen, B * 256);
        b->tops = gs_host(b->gen, B * 256);
        b->rand = gs_host(b->gen, B * 8);
        pthread_create(&b->th, NULL, gs_batch_main, b);
        b->started = 1;
    }
    snprintf(s->path, sizeof s->path, "%s", path);
    if (path[0] != '@') unlink(path);
    s->lfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s->lfd < 0 || bind(s->lfd, (struct sockaddr *)&a, al) || listen(s->lfd, 64))
    {
        gs_err(err, errn, "gensrv: cannot listen on %s: %s", path, strerror(errno));
        gensrv_stop(s, 0);
        return NULL;
    }
    pthread_create(&s->acceptor, NULL, gs_accept_main, s);
    s->accepting = 1;
    return s;
}

void gensrv_stats(struct gensrv *s, struct gensrv_stats *st)
{
    pthread_mutex_lock(&s->mu);
    *st = s->st;
    pthread_mutex_unlock(&s->mu);
}

void gensrv_stop(struct gensrv *s, int wait_s)
{
    if (s == NULL) return;
    if (s->accepting)
    {
        shutdown(s->lfd, SHUT_RDWR);
        pthread_join(s->acceptor, NULL);
    }
    if (s->lfd > 0) close(s->lfd);
    if (s->path[0] && s->path[0] != '@') unlink(s->path);
    /* the clients finish their runs */
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += wait_s;
    pthread_mutex_lock(&s->mu);
    while (s->nclients > 0 && pthread_cond_timedwait(&s->left, &s->mu, &dl) == 0) {}
    int left = s->nclients;
    pthread_mutex_unlock(&s->mu);
    for (struct gs_client *c = s->clients; c; c = c->next)
    {
        if (left) shutdown(c->fd, SHUT_RDWR);
        pthread_join(c->th, NULL);
    }
    pthread_mutex_lock(&s->mu);
    s->stop = 1;
    pthread_cond_broadcast(&s->work);
    pthread_mutex_unlock(&s->mu);
    for (int i = 0; i < s->nb; ++i)
    {
        struct gs_batcher *b = &s->b[i];
        if (b->started) pthread_join(b->th, NULL);
        free(b->req);
        free(b->slot);
        free(b->tbl);
        void *v[6] = {b->built, b->bands, b->ids, b->biome, b->tops, b->rand};
        for (int k = 0; k < 6; ++k) gs_host_free(b->gen, v[k]);
    }
    for (struct gs_client *c = s->clients, *nx; c; c = nx)
    {
        nx = c->next;
        close(c->fd);
        munmap(c->map, c->size);
        free(c);
    }
    free(s);
}

/* ------------------------------------------------------------ the client */

struct gs_remote {
    int fd;
    uint8_t *map;
    size_t size, used;
    char path[108];
    int wait_s;
};

static int remote_off(const struct gs_remote *r, const void *p, size_t bytes, uint64_t *off)
{
    const uint8_t *q = p;
    if (q < r->map || q + bytes > r->map + r->size) return -1;
    *off = (uint64_t)(q - r->map);
    return 0;
}

static int remote_run(void *ctx, const struct ga_req *req, int n, uint8_t *ids, uint8_t *metas, uint8_t *biome,
                      uint8_t *tops, uint64_t *rand)
{
    (void)ctx, (void)req, (void)n, (void)ids, (void)metas, (void)biome, (void)tops, (void)rand;
    return -1;
}

/* the requests go in the mapping's head; the result slots are the pool's
 * buffers, which host_alloc placed in the mapping */
static int remote_run_built(void *ctx, const struct ga_req *req, int n, struct ga_built *built, uint8_t *bands,
                            int *top_band, uint8_t *ids, uint8_t *biome, uint8_t *tops, uint64_t *rand)
{
    struct gs_remote *r = ctx;
    size_t N = (size_t)n;
    struct gs_msg m = {.n = n};
    if (n > GS_CLIENT_BATCH || remote_off(r, built, N * sizeof *built, &m.built) ||
        remote_off(r, bands, N * 16 * GA_BAND_BYTES, &m.bands) || remote_off(r, ids, N * GA_CELLS, &m.ids) ||
        remote_off(r, biome, N * 256, &m.biome) || remote_off(r, tops, N * 256, &m.tops) || remote_off(r, rand, N * 8, &m.rand))
        return -1;
    memcpy(r->map, req, sizeof *req * N);
    m.req = 0;
    struct gs_reply rp;
    if (gs_full(r->fd, &m, sizeof m, 1) || gs_full(r->fd, &rp, sizeof rp, 0)) return -1;
    if (top_band) *top_band = rp.top;
    return rp.rc;
}

/* the pool's round buffers, carved from the mapping after the requests */
static void *remote_host_alloc(void *ctx, size_t bytes)
{
    struct gs_remote *r = ctx;
    size_t at = (r->used + 63) & ~(size_t)63;
    if (at + bytes > r->size) return NULL;
    r->used = at + bytes;
    return r->map + at;
}

static void remote_host_free(void *ctx, void *p)
{
    struct gs_remote *r = ctx;
    /* the pool frees its buffers together, to make bigger ones: the arena
     * starts over once the last is back (only the head's requests stay) */
    (void)p;
    r->used = (size_t)GS_CLIENT_BATCH * sizeof(struct ga_req);
}

static void remote_free(void *ctx)
{
    struct gs_remote *r = ctx;
    close(r->fd);
    munmap(r->map, r->size);
    free(r);
}

static struct ga_gen *remote_new(const char *path, int wait_s, char *err, size_t errn);

static struct ga_gen *remote_another(const struct ga_gen *g)
{
    const struct gs_remote *r = g->ctx;
    return remote_new(r->path, r->wait_s, NULL, 0);
}

static struct ga_gen *remote_new(const char *path, int wait_s, char *err, size_t errn)
{
    struct sockaddr_un a;
    socklen_t al = gs_addr(path, &a);
    if (al == 0) { gs_err(err, errn, "gensrv: a long path"); return NULL; }
    int fd = -1;
    for (int t = 0; t <= wait_s * 20; ++t)
    {
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&a, al) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
        usleep(50000);
    }
    if (fd < 0) { gs_err(err, errn, "gensrv: no server at %s", path); return NULL; }
    /* the requests, then one round's buffers as the pool makes them */
    size_t B = GS_CLIENT_BATCH;
    size_t size = B * sizeof(struct ga_req) + B * (GA_CELLS + 16 * GA_BAND_BYTES + sizeof(struct ga_built) + 512 + 8) +
                  ((size_t)16 << 20);
    int mfd = memfd_create("nw-gensrv", MFD_CLOEXEC);
    void *map = mfd >= 0 && ftruncate(mfd, (off_t)size) == 0
                    ? mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0) : MAP_FAILED;
    uint64_t v[2] = {GS_HELLO, size};
    char cb[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec io = {v, sizeof v};
    struct msghdr mh = {0};
    mh.msg_iov = &io;
    mh.msg_iovlen = 1;
    mh.msg_control = cb;
    mh.msg_controllen = sizeof cb;
    struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type = SCM_RIGHTS;
    cm->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cm), &mfd, sizeof mfd);
    char ok = 0;
    if (map == MAP_FAILED || sendmsg(fd, &mh, 0) != (ssize_t)sizeof v || gs_full(fd, &ok, 1, 0) || !ok)
    {
        gs_err(err, errn, "gensrv: the server at %s refused the mapping", path);
        if (map != MAP_FAILED) munmap(map, size);
        if (mfd >= 0) close(mfd);
        close(fd);
        return NULL;
    }
    close(mfd);
    struct gs_remote *r = calloc(1, sizeof *r);
    struct ga_gen *g = calloc(1, sizeof *g);
    r->fd = fd;
    r->map = map;
    r->size = size;
    r->used = B * sizeof(struct ga_req);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->wait_s = wait_s;
    g->name = "gensrv client";
    g->ctx = r;
    g->max_batch = GS_CLIENT_BATCH;
    g->run = remote_run;
    g->run_built = remote_run_built;
    g->free = remote_free;
    g->host_alloc = remote_host_alloc;
    g->host_free = remote_host_free;
    g->another = remote_another;
    return g;
}

struct ga_gen *ga_gen_remote_create(const char *path, int wait_s, char *err, size_t errn)
{
    return remote_new(path, wait_s, err, errn);
}
