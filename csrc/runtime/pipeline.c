/* The pipeline (pipeline.h). */
#define _GNU_SOURCE
#include "pipeline.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../cuda/worldgen/worldgen.h"
#include "../cuda/render/render.h"
#include "../engine/env.h"
#include "../engine/genahead.h"
#include "gensrv.h"
#include "../engine/lightcap.h"
#include "../engine/lightdefer.h"
#include "../engine/raster_obs.h"
#include "../engine/worldconf.h"
#include "view.h"

enum { P_IDLE, P_STEP, P_QUEUED, P_READY };

struct penv {
    int stream, slot;           /* the stream that keeps its render state, its slot there */
    int state;
    uint64_t frames;
    struct pipe_result res;
    /* when its step was asked for, came back from the pool, was taken by
     * its stream, was ready, and was polled (pipe_stats' latencies) */
    uint64_t t_step, t_done, t_taken, t_ready, t_polled;
};

struct pstream {
    struct pipe *p;
    int index;
    pthread_t th;
    int started, failed;
    struct render *r;
    int nenvs;                  /* the envs whose render state it keeps (slots 0..nenvs-1) */
    int *q, nq;                 /* frames queued (env ids, in arrival order) */
    int inflight;               /* its envs stepping or queued */
    /* with the device meshing: the mesher thread (mth) takes the queued
     * frames and meshes them; mq holds those meshed for the drawer (th),
     * and past counts the frames taken and not yet ready */
    pthread_t mth;
    int mstarted;
    int *mq, nmq, past;
    uint64_t mesh_ns;
    int *forget, nforget;       /* slots whose envs were reset since its last render */
    uint64_t render_ns, idle_ns, renders, frames, upload_bytes;
    uint64_t mesh_bytes, atlas_bytes, tex_bytes, quad_bytes, fresh_meshes, mesh_resets, trips, trip_overs;
    uint64_t dmesh_requests, dmesh_checked, dmesh_diffs, dmesh_upload_bytes, frames_failed;
    uint64_t dmesh_band_bytes, dmesh_chunk_bytes, dmesh_list_bytes, dmesh_req_bytes, dmesh_ctl_bytes, dmesh_band_run_bytes;
    double upload_ms, device_ms, dmesh_ms;
};

struct pipe {
    struct pipe_config cfg;
    struct ga_gen *gen;           /* cfg.gen_device's generator (cfg.gen_client's: the server's) */
    struct ga_gen *gen2, *front;  /* cfg.gen_serve: the second generator, the pool's front */
    struct ga_gen *remote;        /* cfg.gen_client's */
    struct gensrv *srv;
    struct pipe_params par;
    struct pool *pool;
    struct view_set *views;
    int n;
    struct penv *e;
    struct pstream *s;
    int ns;
    unsigned char *buf;
    size_t frame_bytes;
    pthread_t collector;
    pthread_mutex_t mu;
    pthread_cond_t cv_collect, cv_stream, cv_ready;
    int in_pool;                /* envs stepping in the pool */
    int pending;                /* envs stepped and not yet polled */
    int *ready;
    size_t ready_head, nready;
    int stop, stop_streams;
    uint64_t steps, poll_wait_ns;
    uint64_t lat[5];            /* pipe_stats' lat_*_ns */
    uint64_t base_ins, base_cyc, base_pool_ns;
    struct view_times base_view;
    /* device_light: each env's deferred client light (installed in the env
     * at each reset: the reset's image copy replaces the env's own) */
    struct lightdefer **ld;
    uint64_t base_light_syncs, base_light_ops;
    uint64_t (*base_light_cause)[3];
    /* PIPE_LAYOUT_DOMS in force: the cache domains, and each env's (its
     * stream's), the pool's env_dom */
    int ndom;
    int *env_dom;
};

/* the view's callbacks with the env's deferred client light around them:
 * installed (and the device's copy forgotten) at the reset, run before the
 * frame */
static int pl_view_reset(void *ctx, int id, struct session *ss, char *err, size_t n)
{
    struct pipe *p = ctx;
    /* (a save's replay views, ids n on, run the host's light) */
    if (p->ld && id < p->n)
    {
        struct lightdefer *d = p->ld[id];
        d->n = 0;
        d->w = NULL;
        if (d->exec.forget) d->exec.forget(d->exec.ctx, NULL);
        nw_env->light.defer = d;
    }
    return view_reset_cb(p->views, id, ss, err, n);
}

/* a save's view memory (pool.h view_mem, view_reload, view_resume); the
 * reload installs the env's deferred light as the reset does */
static int pl_view_mem(void *ctx, int id, struct pool_vmem *m)
{
    struct pipe *p = ctx;
    return view_mem(p->views, id, m);
}

static int pl_view_reload(void *ctx, int id, struct session *ss, struct pool_vmem *m, char *err, size_t n)
{
    struct pipe *p = ctx;
    (void)ss;
    if (p->ld && id < p->n)
    {
        struct lightdefer *d = p->ld[id];
        d->n = 0;
        d->w = NULL;
        if (d->exec.forget) d->exec.forget(d->exec.ctx, NULL);
        nw_env->light.defer = d;
    }
    return view_reload(p->views, id, m, err, n);
}

static void pl_view_resume(void *ctx, int id)
{
    struct pipe *p = ctx;
    view_resume(p->views, id);
}

static void pl_view_tick(void *ctx, int id, int64_t t, const struct act *a, int end)
{
    struct pipe *p = ctx;
    view_tick_cb(p->views, id, t, a, end);
}

static void pl_view_step(void *ctx, int id, struct pool_result *r)
{
    struct pipe *p = ctx;
    if (p->ld && id < p->n && lightdefer_sync(p->ld[id]) != 0)
    {
        r->obs = NULL;
        r->obs_rc = -3;
        return;
    }
    view_step_cb(p->views, id, r);
}

static void light_sums(const struct pipe *p, uint64_t *syncs, uint64_t *ops, uint64_t *failed)
{
    *syncs = *ops = *failed = 0;
    for (int i = 0; p->ld && i < p->n; ++i)
    {
        *syncs += p->ld[i]->st.runs;
        *ops += p->ld[i]->st.ran;
        *failed += p->ld[i]->st.failed != 0;
    }
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void seterr(char *err, size_t n, const char *msg, const char *detail)
{
    if (err && n) snprintf(err, n, "%s%s%s", msg, detail ? ": " : "", detail ? detail : "");
}

/* ----------------------------------------------------------- the streams */

static uint64_t now_ns(void);

static void push_ready(struct pipe *p, int id)
{
    p->ready[(p->ready_head + p->nready) % (size_t)p->n] = id;
    ++p->nready;
    p->e[id].state = P_READY;
    p->e[id].t_ready = now_ns();
}

/* frames ids[0..k) of a render (at[i]: frame i's place in it, -1 for none;
 * rc the render's result) to the ready list; under the lock */
static void publish(struct pstream *s, const int *ids, const int *at, int k, int rc)
{
    struct pipe *p = s->p;
    for (int i = 0; i < k; ++i)
    {
        struct penv *pe = &p->e[ids[i]];
        if (pe->res.frame_rc == 0 && rc != 0) pe->res.frame_rc = -2;
        /* a frame whose device meshes were lost (its view meshes
         * everything again at its next frame) */
        if (pe->res.frame_rc == 0 && at[i] >= 0 && render_failed(s->r, at[i])) { pe->res.frame_rc = -2; ++s->frames_failed; }
        if (pe->res.frame_rc == 0)
        {
            pe->res.obs.data = p->buf + ((size_t)pe->res.obs.slot * (size_t)p->n + (size_t)ids[i]) * p->frame_bytes;
            ++pe->frames;
        }
        else pe->res.obs.data = NULL;
        --s->inflight;
        push_ready(p, ids[i]);
    }
}

/* the frames IDS[0..K) taken from the queue set on the stream's renderer,
 * in place: those it refuses go to the ready list (under the lock); the
 * rest's slots in SLOTS; their number */
static int set_frames(struct pstream *s, int *ids, int k, int *slots)
{
    struct pipe *p = s->p;
    int nm = 0, np = 0;
    int *pids = slots + k;
    for (int i = 0; i < k; ++i)
    {
        struct penv *pe = &p->e[ids[i]];
        if (render_set(s->r, pe->slot, pe->res.r->obs) != 0)
        {
            pe->res.frame_rc = -2;
            pids[np++] = ids[i];
            continue;
        }
        pe->res.frame_rc = 0;
        pe->res.obs.slot = (int)(pe->frames % (uint64_t)p->par.depth);
        ids[nm] = ids[i];
        slots[nm++] = pe->slot;
    }
    if (np)
    {
        int *at = pids + np;
        for (int i = 0; i < np; ++i) at[i] = -1;
        pthread_mutex_lock(&p->mu);
        publish(s, pids, at, np, 0);
        s->past -= np;
        pthread_cond_broadcast(&p->cv_ready);
        pthread_mutex_unlock(&p->mu);
    }
    return nm;
}

/* The mesher (with the device meshing): the frames queued, as a batch comes
 * (B frames, or every env still in flight), have their mesh feeds applied
 * and their section passes counted and written on the device's mesher
 * stream (render_mesh_start, _finish: the wait for the counts is this
 * thread's), then go to the drawer; meanwhile the drawer draws the frames
 * meshed before, so a frame waits for neither's turn but its own. */
static void *mesher_main(void *arg)
{
    struct pstream *s = arg;
    struct pipe *p = s->p;
    if (p->ndom > 1) pool_domain_pin(s->index % p->ndom);
    render_device(p->cfg.device);
    const size_t cap = (size_t)(s->nenvs + 1);
    int *ids = proc_malloc(sizeof(int) * cap);
    int *slots = proc_malloc(sizeof(int) * 3 * cap);
    int *forget = proc_malloc(sizeof(int) * cap);
    const int B = p->par.batch;
    pthread_mutex_lock(&p->mu);
    s->mstarted = 1;
    pthread_cond_broadcast(&p->cv_ready);
    for (;;)
    {
        uint64_t w0 = now_ns();
        /* a batch is B frames queued, or every env still in flight queued
         * or past the queue */
#define TAKE (s->nq >= B || (s->nq > 0 && s->nq + s->past == s->inflight))
        while (!p->stop_streams && !TAKE) pthread_cond_wait(&p->cv_stream, &p->mu);
#undef TAKE
        s->idle_ns += now_ns() - w0;
        if (p->stop_streams) break;
        int k = s->nq;
        memcpy(ids, s->q, sizeof(int) * (size_t)k);
        s->nq = 0;
        s->past += k;
        int nf = s->nforget;
        memcpy(forget, s->forget, sizeof(int) * (size_t)nf);
        s->nforget = 0;
        pthread_mutex_unlock(&p->mu);

        uint64_t t0 = now_ns();
        render_range("stream mesh", 1);
        for (int i = 0; i < k; ++i) p->e[ids[i]].t_taken = t0;
        for (int i = 0; i < nf; ++i) render_forget(s->r, forget[i]);
        int nm = set_frames(s, ids, k, slots);
        if (nm)
        {
            render_mesh_start(s->r, nm, slots);
            render_mesh_finish(s->r);
        }
        render_range(NULL, 0);
        uint64_t t1 = now_ns();
        pthread_mutex_lock(&p->mu);
        s->mesh_ns += t1 - t0;
        memcpy(s->mq + s->nmq, ids, sizeof(int) * (size_t)nm);
        s->nmq += nm;
        pthread_cond_broadcast(&p->cv_stream);
    }
    pthread_mutex_unlock(&p->mu);
    proc_free(ids);
    proc_free(slots);
    proc_free(forget);
    return NULL;
}

/* The stream's drawer: with the host meshing it takes the frames queued (a
 * batch as the mesher's) and draws them; with the device meshing, those
 * the mesher has meshed. It makes the stream's renderer (and the mesher
 * thread, once it has one) and frees it. */
static void *stream_main(void *arg)
{
    struct pstream *s = arg;
    struct pipe *p = s->p;
    const int w = p->cfg.pool.obs.w, h = p->cfg.pool.obs.h;
    const int dmesh = p->cfg.device_mesh != 0;
    if (p->ndom > 1) pool_domain_pin(s->index % p->ndom);
    render_device(p->cfg.device);
    /* the device is shared: its memory comes and goes, so a renderer that
     * does not fit is tried again for a while */
    struct render *r = NULL;
    for (int tries = 0; tries < 60 && !r; ++tries)
    {
        r = render_new(s->nenvs, w, h, p->cfg.arena_mb > 0 ? p->cfg.arena_mb : 128);
        if (!r) usleep(500000);
    }
    if (r) render_mesh_tab(r, p->cfg.mesh_tab);
    if (r) render_one_trip(r, !p->cfg.usual_trip);
    const size_t cap = (size_t)(s->nenvs + 1);
    int *ids = proc_malloc(sizeof(int) * cap);
    int *slots = proc_malloc(sizeof(int) * 3 * cap);
    int *forget = proc_malloc(sizeof(int) * cap);
    int *at = proc_malloc(sizeof(int) * cap);
    unsigned char **outs = proc_malloc(sizeof(unsigned char *) * cap);
    pthread_mutex_lock(&p->mu);
    s->r = r;
    s->failed = r == NULL;
    if (r && dmesh && pthread_create(&s->mth, NULL, mesher_main, s) != 0) s->failed = 1;
    while (r && dmesh && !s->failed && !s->mstarted) pthread_cond_wait(&p->cv_ready, &p->mu);
    s->started = 1;
    pthread_cond_broadcast(&p->cv_ready);
    const int B = p->par.batch;
    while (r && !s->failed)
    {
        uint64_t w0 = now_ns();
#define TAKE (dmesh ? s->nmq > 0 : (s->nq >= B || (s->nq > 0 && s->nq == s->inflight)))
        while (!p->stop_streams && !TAKE) pthread_cond_wait(&p->cv_stream, &p->mu);
#undef TAKE
        if (!dmesh) s->idle_ns += now_ns() - w0;
        if (p->stop_streams) break;
        /* every frame there goes (those that came during the last render
         * too): B is what a render waits for, not what it takes */
        int k;
        if (dmesh)
        {
            k = s->nmq;
            memcpy(ids, s->mq, sizeof(int) * (size_t)k);
            s->nmq = 0;
        }
        else
        {
            k = s->nq;
            memcpy(ids, s->q, sizeof(int) * (size_t)k);
            s->nq = 0;
            s->past += k;
        }
        int nf = dmesh ? 0 : s->nforget;
        memcpy(forget, s->forget, sizeof(int) * (size_t)nf);
        if (!dmesh) s->nforget = 0;
        pthread_mutex_unlock(&p->mu);

        uint64_t t0 = now_ns();
        render_range("stream render", 1);
        int m = k;
        if (!dmesh)
        {
            for (int i = 0; i < k; ++i) p->e[ids[i]].t_taken = t0;
            for (int i = 0; i < nf; ++i) render_forget(r, forget[i]);
            m = set_frames(s, ids, k, slots);
        }
        for (int i = 0; i < m; ++i)
        {
            struct penv *pe = &p->e[ids[i]];
            outs[i] = p->buf + ((size_t)pe->res.obs.slot * (size_t)p->n + (size_t)ids[i]) * p->frame_bytes;
            at[i] = i;
            slots[i] = pe->slot;
        }
        int rc = m ? render_render_list(r, m, slots, outs) : 0;
        struct render_stats cst = {0};
        if (m) cst = *render_stats(r);
        render_range(NULL, 0);
        uint64_t t1 = now_ns();

        pthread_mutex_lock(&p->mu);
        s->render_ns += t1 - t0;
        if (m)
        {
            ++s->renders;
            s->frames += (uint64_t)m;
            s->upload_ms += cst.upload_ms;
            s->device_ms += cst.total_ms - cst.upload_ms;
            s->upload_bytes += cst.upload_bytes;
            s->mesh_bytes += cst.mesh_bytes;
            s->atlas_bytes += cst.atlas_bytes;
            s->tex_bytes += cst.tex_bytes;
            s->quad_bytes += cst.quad_bytes;
            s->fresh_meshes += cst.fresh_meshes;
            s->mesh_resets += cst.mesh_resets;
            s->trips += cst.trips;
            s->trip_overs += cst.trip_overs;
            s->dmesh_requests += cst.dmesh_requests;
            s->dmesh_checked += cst.dmesh_checked;
            s->dmesh_diffs += cst.dmesh_diffs;
            s->dmesh_upload_bytes += cst.dmesh_upload_bytes;
            s->dmesh_band_bytes += cst.dmesh_band_bytes;
            s->dmesh_chunk_bytes += cst.dmesh_chunk_bytes;
            s->dmesh_list_bytes += cst.dmesh_list_bytes;
            s->dmesh_req_bytes += cst.dmesh_req_bytes;
            s->dmesh_ctl_bytes += cst.dmesh_ctl_bytes;
            s->dmesh_band_run_bytes += cst.dmesh_band_run_bytes;
            s->dmesh_ms += cst.dmesh_count_ms + cst.dmesh_write_ms;
        }
        publish(s, ids, at, m, rc);
        s->past -= m;
        pthread_cond_broadcast(&p->cv_ready);
        /* a mesher waiting for every env in flight may now have them */
        pthread_cond_broadcast(&p->cv_stream);
    }
    int joined = s->mstarted;
    pthread_mutex_unlock(&p->mu);
    if (joined) pthread_join(s->mth, NULL);
    render_free(r);
    proc_free(ids);
    proc_free(slots);
    proc_free(forget);
    proc_free(at);
    proc_free(outs);
    return NULL;
}

static void streams_stop(struct pipe *p)
{
    if (p->s == NULL) return;
    pthread_mutex_lock(&p->mu);
    p->stop_streams = 1;
    pthread_cond_broadcast(&p->cv_stream);
    pthread_mutex_unlock(&p->mu);
    for (int i = 0; i < p->ns; ++i)
        if (p->s[i].started) pthread_join(p->s[i].th, NULL);
    for (int i = 0; i < p->ns; ++i)
    {
        proc_free(p->s[i].q);
        proc_free(p->s[i].mq);
        proc_free(p->s[i].forget);
    }
    proc_free(p->s);
    p->s = NULL;
    p->ns = 0;
    p->stop_streams = 0;
}

/* groups of par.group envs, group g on stream g mod S; an env's slot is its
 * place among its stream's envs */
static int streams_start(struct pipe *p, char *err, size_t n)
{
    struct pipe_params *par = &p->par;
    p->ns = par->streams;
    p->s = proc_calloc((size_t)p->ns, sizeof *p->s);
    for (int id = 0; id < p->n; ++id)
    {
        struct penv *pe = &p->e[id];
        pe->stream = (id / par->group) % p->ns;
        pe->slot = p->s[pe->stream].nenvs++;
        if (p->env_dom) p->env_dom[id] = pe->stream % p->ndom;
    }
    int bad = 0;
    for (int i = 0; i < p->ns; ++i)
    {
        struct pstream *s = &p->s[i];
        s->p = p;
        s->index = i;
        s->q = proc_malloc(sizeof(int) * (size_t)(s->nenvs + 1));
        s->mq = proc_malloc(sizeof(int) * (size_t)(s->nenvs + 1));
        s->forget = proc_malloc(sizeof(int) * (size_t)(s->nenvs + 1));
        /* every slot's state is sent at its first render; no renderer
         * without the device */
        if (p->cfg.no_device) continue;
        if (pthread_create(&s->th, NULL, stream_main, s) != 0) { bad = 1; break; }
    }
    pthread_mutex_lock(&p->mu);
    for (int i = 0; i < p->ns && !bad && !p->cfg.no_device; ++i)
    {
        while (!p->s[i].started) pthread_cond_wait(&p->cv_ready, &p->mu);
        if (p->s[i].failed) bad = 1;
    }
    pthread_mutex_unlock(&p->mu);
    if (bad)
    {
        streams_stop(p);
        seterr(err, n, "a stream's renderer did not start (the device's memory?)", NULL);
        return -1;
    }
    return 0;
}

/* --------------------------------------------------------- the collector */

static void *collect_main(void *arg)
{
    struct pipe *p = arg;
    const struct pool_result **res = proc_malloc(sizeof *res * (size_t)p->n);
    pthread_mutex_lock(&p->mu);
    for (;;)
    {
        while (!p->stop && p->in_pool == 0) pthread_cond_wait(&p->cv_collect, &p->mu);
        if (p->stop) break;
        pthread_mutex_unlock(&p->mu);
        render_range("collector poll", 1);
        int got = pool_poll_some(p->pool, p->n, res);
        render_range(NULL, 0);
        pthread_mutex_lock(&p->mu);
        int to_streams = 0, to_ready = 0;
        for (int k = 0; k < got; ++k)
        {
            int id = res[k]->id;
            struct penv *pe = &p->e[id];
            --p->in_pool;
            pe->t_done = pe->t_taken = now_ns();
            pe->res.r = res[k];
            pe->res.obs.data = NULL;
            if (res[k]->obs != NULL && !p->cfg.no_device)
            {
                struct pstream *s = &p->s[pe->stream];
                pe->state = P_QUEUED;
                s->q[s->nq++] = id;
                to_streams = 1;
            }
            else
            {
                pe->res.frame_rc = res[k]->obs != NULL || p->cfg.no_view ? 0 : -1;
                --p->s[pe->stream].inflight;
                push_ready(p, id);
                to_ready = 1;
            }
        }
        if (to_streams) pthread_cond_broadcast(&p->cv_stream);
        /* a stream whose last env in flight went straight to ready may now
         * hold everything that can come */
        if (to_ready) { pthread_cond_broadcast(&p->cv_ready); pthread_cond_broadcast(&p->cv_stream); }
    }
    pthread_mutex_unlock(&p->mu);
    proc_free(res);
    return NULL;
}

/* ------------------------------------------------------------------- API */

/* The defaults (pipe_tune's sweep measures the rest): with the host
 * meshing, a stream per 8 envs (at most 16) rendering whatever is queued
 * (B 1: a frame waits for no other env); with the device meshing, a stream
 * per 32 envs (at most 4: each stream's count launches last as long as
 * their slowest section, and more of them at once only slow each other),
 * B 1 as well (its mesher and drawer threads each take whatever is there;
 * 2026-09-29, lane/dmeshflow: 2596 against 1826 env-steps/s with all its
 * envs a batch, village N 128). */
static int params_fix(const struct pipe *p, struct pipe_params *par)
{
    int dmesh = p->cfg.device_mesh != 0;
    if (par->streams <= 0)
    {
        par->streams = dmesh ? p->n / 32 : p->n / 8;
        if (par->streams > (dmesh ? 4 : 16)) par->streams = dmesh ? 4 : 16;
        if (par->streams < 1) par->streams = 1;
        /* a stream a domain at least, each domain's streams alike */
        if (p->ndom > 1) par->streams = (par->streams + p->ndom - 1) / p->ndom * p->ndom;
    }
    if (par->streams > p->n) par->streams = p->n;
    if (par->group <= 0) par->group = (p->n + par->streams - 1) / par->streams;
    if (par->depth <= 0) par->depth = 2;
    int per = (p->n + par->streams - 1) / par->streams;
    if (par->batch <= 0) par->batch = 1;
    if (par->batch > per) par->batch = per;
    return 0;
}

static int buffer_make(struct pipe *p, char *err, size_t n)
{
    render_device(p->cfg.device);
    if (p->buf) render_dev_free(p->buf);
    p->buf = render_dev_alloc((size_t)p->par.depth * (size_t)p->n * p->frame_bytes);
    if (p->buf == NULL) { seterr(err, n, "no device memory for the observation buffer", NULL); return -1; }
    return 0;
}

/* The workers when the config leaves them to the pipeline (pool.threads
 * 0), for the streams in p->par: a worker a physical core, less the
 * streams' threads (two a stream with the device meshing), at most one an
 * env: at 128 envs on the GPU host's 64 cores (48 workers beside 16 streams, 56
 * beside 4 with the device meshing) 5 to 11% more env-steps/s than 128
 * workers (the online CPUs) and 4 to 12% more than 96, with 29 to 53% less
 * CPU an env-step (lane/pagefault, out/perf/pagefault.tsv) */
static int default_threads(const struct pipe *p)
{
    const struct pipe_config *cfg = &p->cfg;
    int st = p->par.streams * (cfg->device_mesh && !cfg->no_device ? 2 : 1);
    int t = pool_cores() - (cfg->no_device || cfg->no_view ? 0 : st);
    if (t < pool_cores() / 2) t = pool_cores() / 2;
    if (t > p->n) t = p->n;
    if (t < 1) t = 1;
    return t;
}

int pipe_config_file(struct pipe_config *cfg, const char *path, struct wconf *wc, char *err, size_t n)
{
    struct wconf c;
    if (wconf_load(&c, path, err, n)) return -1;
    if (wconf_unimplemented(&c, err, n)) return -1;
    struct pool_obs *o = &cfg->pool.obs;
    wconf_size(&c, &o->w, &o->h);
    o->ticks = wconf_int(&c, WC_TICKS_PER_STEP);
    o->rd = wconf_int(&c, WC_RENDER_DISTANCE);
    o->hud = wconf_on(&c, WC_HUD);
    o->gamma = (float)atof(c.val[WC_GAMMA]);
    cfg->device_mesh = wconf_device_mesh(&c);
    cfg->gen_device = wconf_on(&c, WC_DEVICE_GENERATION);
    cfg->device_light = wconf_on(&c, WC_DEVICE_LIGHT);
    cfg->pool.config = path;
    if (wc) *wc = c;
    return 0;
}

struct pipe *pipe_make(const struct pipe_config *cfg, char *err, size_t n)
{
    /* without the device the views would mesh on the host: a different
     * host workload from the one device meshing measures, so refused */
    if (cfg->no_device && cfg->device_mesh && !cfg->no_view)
    {
        seterr(err, n, "device meshing needs the device (without it the views mesh on the host)", NULL);
        return NULL;
    }
    struct pipe *p = proc_calloc(1, sizeof *p);
    p->cfg = *cfg;
    p->n = cfg->pool.n;
    if (p->n <= 0) { seterr(err, n, "no envs", NULL); proc_free(p); return NULL; }
    struct pool_obs *o = &p->cfg.pool.obs;
    if (o->w <= 0) o->w = 128;
    if (o->h <= 0) o->h = 128;
    if (o->rd <= 0) o->rd = 4;
    if (o->ticks <= 0) o->ticks = 4;
    p->frame_bytes = (size_t)o->w * (size_t)o->h * 3;
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv_collect, NULL);
    pthread_cond_init(&p->cv_stream, NULL);
    pthread_cond_init(&p->cv_ready, NULL);
    p->e = proc_calloc((size_t)p->n, sizeof *p->e);
    p->ready = proc_calloc((size_t)p->n, sizeof *p->ready);
    for (int i = 0; i < p->n; ++i) p->e[i].res.id = i;

    struct pool_config pc = p->cfg.pool;
    if (!cfg->no_view)
    {
        struct pv_config vc = {cfg->assets, o->w, o->h, o->rd, !o->hud, NULL, cfg->no_device ? 0 : cfg->device_mesh,
                               cfg->mesh_check, o->prec, o->gamma};
        /* pool.h save_slots: two views a save beyond the envs' */
        p->views = view_set_new(cfg->view_so, p->n, 2 * (cfg->pool.save_slots > 0 ? cfg->pool.save_slots : 0), &vc,
                                cfg->draw, !cfg->pool.no_counters, err, n);
        if (p->views == NULL) { pipe_free(p); return NULL; }
        pc.view_reset = pl_view_reset;
        pc.view_tick = pl_view_tick;
        pc.view_step = pl_view_step;
        pc.view_mem = pl_view_mem;
        pc.view_reload = pl_view_reload;
        pc.view_resume = pl_view_resume;
        pc.view_ctx = p;
        if (cfg->device_light && !cfg->no_device)
        {
            p->ld = proc_calloc((size_t)p->n, sizeof *p->ld);
            p->base_light_cause = proc_calloc((size_t)p->n, sizeof *p->base_light_cause);
            for (int i = 0; i < p->n; ++i)
            {
                struct lightdefer_exec ex;
                if (lc_defer_exec_cuda_inproc(&ex, cfg->device, cfg->light_slots) != 0 ||
                    (p->ld[i] = lightdefer_new(&ex)) == NULL)
                {
                    seterr(err, n, "the device's client light does not start (device memory?)", NULL);
                    pipe_free(p);
                    return NULL;
                }
            }
        }
    }
    if (!cfg->no_device) pc.range = render_range;
    if (cfg->gen_device && pc.gen == NULL && (cfg->gen_client == NULL || cfg->gen_serve != NULL))
    {
        /* the generators on the pipeline's device, rounds of up to 1024
         * chunks over up to 1024 world seeds */
        int gen_card = cfg->gen_card ? cfg->gen_card - 1 : cfg->device;
        if (worldgen_device_use(gen_card) != 0)
        {
            seterr(err, n, "the generation device does not exist", NULL);
            pipe_free(p);
            return NULL;
        }
        p->gen = ga_gen_cuda_local_create_prio(1024, 1024, !cfg->gen_high);
        if (p->gen != NULL && cfg->gen_serve && cfg->gen_batchers != 1) p->gen2 = p->gen->another(p->gen);
        worldgen_device_use(cfg->device);
        if (p->gen == NULL) { seterr(err, n, "the device's chunk generators do not start", NULL); pipe_free(p); return NULL; }
        pc.gen = p->gen;
        if (cfg->gen_serve)
        {
            /* the server: this pool's rounds and every client's through one front */
            struct ga_gen *gs[2] = {p->gen, p->gen2};
            if ((p->srv = gensrv_start(cfg->gen_serve, gs, p->gen2 ? 2 : 1, err, n)) == NULL ||
                (p->front = gensrv_front(p->srv)) == NULL)
            {
                pipe_free(p);
                return NULL;
            }
            pc.gen = p->front;
        }
    }
    if (cfg->gen_client && (pc.gen == NULL || p->srv != NULL))
    {
        /* another process's generators (gensrv.h), waiting a while for it
         * to start; with gen_serve too, this process's own server through
         * the socket (pipe_gate's check of the clients' path) */
        if ((p->remote = ga_gen_remote_create(cfg->gen_client, 300, err, n)) == NULL) { pipe_free(p); return NULL; }
        pc.gen = p->remote;
    }
    /* the pipeline's layout: by cache domain (lane/cachefit) */
    int layout = cfg->pool_layout == PIPE_LAYOUT_PIPE ? PIPE_LAYOUT_DOMS : cfg->pool_layout;
    if (layout == PIPE_LAYOUT_DOMS && !cfg->no_device && (p->ndom = pool_domains()) > 1)
    {
        p->env_dom = proc_calloc((size_t)p->n, sizeof *p->env_dom);
        pc.pin = 2, pc.place = PP_DOMS, pc.steal = 1, pc.env_dom = p->env_dom;
    }
    else
        p->ndom = 0;
    p->par = cfg->par;
    params_fix(p, &p->par);
    if (pc.threads <= 0) pc.threads = default_threads(p);
    /* the pipeline's layout: any worker steps any env, anywhere in the mask.
     * Pinning envs to workers and workers to cores (pool.h pin, place,
     * steal) cut the tick's cycles 19 to 32% at N 32, an env or so a worker
     * (its lines stay in its core's caches between steps), but at N 128, 32
     * envs an L3, no layout's DRAM fills fell (+0 to +13%) and none gained
     * env-steps/s in all three kinds: the Nether -2 to -26%, exploring -2 to
     * -20%, the pinned ones losing to envs dividing unevenly over the
     * workers (lane/pincache, out/perf/pincache.tsv) */
    if (layout == PIPE_LAYOUT_ANY) pc.pin = 0, pc.place = PP_NONE, pc.steal = 0;
    p->pool = pool_make(&pc, err, n);
    if (p->pool == NULL) { pipe_free(p); return NULL; }
    if (cfg->no_view) p->cfg.no_device = 1;
    if (!p->cfg.no_device && buffer_make(p, err, n) != 0) { pipe_free(p); return NULL; }
    if (streams_start(p, err, n) != 0) { pipe_free(p); return NULL; }
    pthread_create(&p->collector, NULL, collect_main, p);
    return p;
}

void pipe_free(struct pipe *p)
{
    if (p == NULL) return;
    if (p->pool)
    {
        pthread_mutex_lock(&p->mu);
        p->stop = 1;
        pthread_cond_broadcast(&p->cv_collect);
        pthread_mutex_unlock(&p->mu);
        if (p->collector) pthread_join(p->collector, NULL);
    }
    streams_stop(p);
    pool_free(p->pool);
    /* the server outlives this pool: its clients finish their runs first */
    ga_gen_free(p->remote);
    gensrv_stop(p->srv, 900);
    ga_gen_free(p->front);
    ga_gen_free(p->gen2);
    ga_gen_free(p->gen);
    view_set_free(p->views);
    for (int i = 0; p->ld && i < p->n; ++i) lightdefer_free(p->ld[i]);
    if (p->ld) proc_free(p->ld);
    if (p->base_light_cause) proc_free(p->base_light_cause);
    if (p->env_dom) proc_free(p->env_dom);
    if (p->buf) render_dev_free(p->buf);
    proc_free(p->e);
    proc_free(p->ready);
    proc_free(p);
}

int pipe_size(const struct pipe *p) { return p->n; }
struct pool *pipe_pool(struct pipe *p) { return p->pool; }
struct gensrv *pipe_gensrv(struct pipe *p) { return p->srv; }
struct view_set *pipe_views(struct pipe *p) { return p->views; }

struct pool_start *pipe_start_load(struct pipe *p, const char *dir, char *err, size_t n)
{
    return pool_start_load(p->pool, dir, err, n);
}

int pipe_start_load_many(struct pipe *p, const char *const *dirs, int k, struct pool_start **out, char *err, size_t n)
{
    return pool_start_load_many(p->pool, dirs, k, out, err, n);
}

struct pool_start *pipe_start_from_env(struct pipe *p, int id, char *err, size_t n)
{
    pthread_mutex_lock(&p->mu);
    int idle = id >= 0 && id < p->n && p->e[id].state == P_IDLE;
    pthread_mutex_unlock(&p->mu);
    if (!idle)
    {
        seterr(err, n, "the env is out of range or not idle", NULL);
        return NULL;
    }
    return pool_start_from_env(p->pool, id, err, n);
}

int pipe_start_ready(struct pipe *p, const struct pool_start *s) { return pool_start_ready(p->pool, s); }

int pipe_start_wait(struct pipe *p, struct pool_start *s, char *err, size_t n) { return pool_start_wait(p->pool, s, err, n); }

int pipe_reset(struct pipe *p, const int *ids, int k, struct pool_start *start, char *err, size_t n)
{
    pthread_mutex_lock(&p->mu);
    for (int i = 0; i < k; ++i)
        if (ids[i] < 0 || ids[i] >= p->n || p->e[ids[i]].state != P_IDLE)
        {
            pthread_mutex_unlock(&p->mu);
            seterr(err, n, "an env is out of range or not idle", NULL);
            return -1;
        }
    pthread_mutex_unlock(&p->mu);
    if (pool_reset(p->pool, ids, k, start, err, n) != 0) return -1;
    pthread_mutex_lock(&p->mu);
    for (int i = 0; i < k; ++i)
    {
        struct penv *pe = &p->e[ids[i]];
        struct pstream *s = &p->s[pe->stream];
        /* a slot once (the list holds one entry an env): two resets before
         * the stream's next render overflowed it (lane/rlbind) */
        int queued = 0;
        for (int j = 0; j < s->nforget && !queued; ++j) queued = s->forget[j] == pe->slot;
        if (!queued) s->forget[s->nforget++] = pe->slot;
    }
    pthread_mutex_unlock(&p->mu);
    return 0;
}

int pipe_step(struct pipe *p, const int *ids, int k, const struct pool_act *acts, int n, int per_tick)
{
    pthread_mutex_lock(&p->mu);
    for (int i = 0; i < k; ++i)
        if (ids[i] < 0 || ids[i] >= p->n || p->e[ids[i]].state != P_IDLE)
        {
            pthread_mutex_unlock(&p->mu);
            return -1;
        }
    /* the pool's step under the pipeline's lock: the collector sees the
     * envs in the pool only once they are */
    if (pool_step(p->pool, ids, k, acts, n, per_tick) != 0)
    {
        pthread_mutex_unlock(&p->mu);
        return -1;
    }
    uint64_t ts = now_ns();
    for (int i = 0; i < k; ++i)
    {
        struct penv *pe = &p->e[ids[i]];
        pe->state = P_STEP;
        ++p->s[pe->stream].inflight;
        if (pe->t_polled) p->lat[4] += ts - pe->t_polled;
        pe->t_step = ts;
    }
    p->in_pool += k;
    p->pending += k;
    pthread_cond_broadcast(&p->cv_collect);
    pthread_mutex_unlock(&p->mu);
    return 0;
}

int pipe_poll(struct pipe *p, int b, const struct pipe_result **out)
{
    return pipe_poll_some(p, b, b, out);
}

int pipe_poll_some(struct pipe *p, int min, int b, const struct pipe_result **out)
{
    pthread_mutex_lock(&p->mu);
    if (min > b) min = b;
    int want = min < p->pending ? min : p->pending;
    uint64_t t0 = now_ns();
    if ((int)p->nready < want)
    {
        render_range("trainer poll wait", 1);
        while ((int)p->nready < want) pthread_cond_wait(&p->cv_ready, &p->mu);
        render_range(NULL, 0);
    }
    p->poll_wait_ns += now_ns() - t0;
    int got = 0;
    while (got < b && p->nready > 0)
    {
        int id = p->ready[p->ready_head];
        p->ready_head = (p->ready_head + 1) % (size_t)p->n;
        --p->nready;
        struct penv *pe = &p->e[id];
        pe->state = P_IDLE;
        uint64_t tp = now_ns();
        if (pe->t_step)
        {
            p->lat[0] += pe->t_done - pe->t_step;
            p->lat[1] += pe->t_taken - pe->t_done;
            p->lat[2] += pe->t_ready - pe->t_taken;
            p->lat[3] += tp - pe->t_ready;
        }
        pe->t_polled = tp;
        struct pipe_obs *o = &pe->res.obs;
        o->shape[0] = p->cfg.pool.obs.h;
        o->shape[1] = p->cfg.pool.obs.w;
        o->shape[2] = 3;
        o->strides[0] = (int64_t)p->cfg.pool.obs.w * 3;
        o->strides[1] = 3;
        o->strides[2] = 1;
        o->device = p->cfg.device;
        out[got++] = &pe->res;
    }
    p->pending -= got;
    p->steps += (uint64_t)got;
    pthread_mutex_unlock(&p->mu);
    return got;
}

unsigned char *pipe_buffer(struct pipe *p, int64_t shape[5])
{
    shape[0] = p->par.depth;
    shape[1] = p->n;
    shape[2] = p->cfg.pool.obs.h;
    shape[3] = p->cfg.pool.obs.w;
    shape[4] = 3;
    return p->buf;
}

void pipe_params_get(const struct pipe *p, struct pipe_params *par) { *par = p->par; }

int pipe_params_set(struct pipe *p, const struct pipe_params *par, char *err, size_t n)
{
    pthread_mutex_lock(&p->mu);
    int busy = p->pending > 0;
    pthread_mutex_unlock(&p->mu);
    if (busy) { seterr(err, n, "envs are in flight", NULL); return -1; }
    struct pipe_params np = *par;
    params_fix(p, &np);
    int depth_changed = np.depth != p->par.depth;
    streams_stop(p);
    p->par = np;
    /* the workers the pipeline chose follow the streams' threads */
    if (p->cfg.pool.threads <= 0 && pool_threads_set(p->pool, default_threads(p), err, n) != 0) return -1;
    /* the new renderers hold none of the views' device meshes */
    for (int i = 0; p->views && i < p->n; ++i) view_mesh_resync(p->views, i);
    if (depth_changed && !p->cfg.no_device && buffer_make(p, err, n) != 0) return -1;
    return streams_start(p, err, n);
}

void pipe_stats(struct pipe *p, struct pipe_stats *st)
{
    memset(st, 0, sizeof *st);
    struct pool_stats ps;
    pool_stats(p->pool, &ps);
    struct view_times vt = {0};
    if (p->views) view_times(p->views, &vt);
    pthread_mutex_lock(&p->mu);
    st->steps = p->steps;
    st->poll_wait_ns = p->poll_wait_ns;
    st->lat_step_ns = p->lat[0];
    st->lat_queue_ns = p->lat[1];
    st->lat_render_ns = p->lat[2];
    st->lat_poll_ns = p->lat[3];
    st->lat_trainer_ns = p->lat[4];
    st->tick_ns = ps.ns - p->base_pool_ns;
    st->feed_ms = vt.feed_ms - p->base_view.feed_ms;
    st->pool_instructions = ps.instructions - p->base_ins;
    st->pool_cycles = ps.cycles - p->base_cyc;
    st->view_tick_ns = vt.tick_ns - p->base_view.tick_ns;
    st->view_frame_ns = vt.frame_ns - p->base_view.frame_ns;
    st->view_tick_ins = vt.tick_ins - p->base_view.tick_ins;
    st->view_frame_ins = vt.frame_ins - p->base_view.frame_ins;
    st->view_tick_cyc = vt.tick_cyc - p->base_view.tick_cyc;
    st->view_frame_cyc = vt.frame_cyc - p->base_view.frame_cyc;
    st->meshed = vt.meshed - p->base_view.meshed;
    st->drawn = vt.drawn - p->base_view.drawn;
    st->mesh_reused = vt.reused - p->base_view.reused;
    st->mesh_empty = vt.empty - p->base_view.empty;
    st->mesh_checked = vt.checked - p->base_view.checked;
    st->mesh_check_bad = vt.check_bad - p->base_view.check_bad;
    st->tex_checked = vt.tex_checked - p->base_view.tex_checked;
    st->tex_bad = vt.tex_bad - p->base_view.tex_bad;
    st->band_checked = vt.band_checked - p->base_view.band_checked;
    st->band_bad = vt.band_bad - p->base_view.band_bad;
    st->device_bytes = p->buf ? (uint64_t)p->par.depth * (uint64_t)p->n * p->frame_bytes : 0;
    light_sums(p, &st->light_syncs, &st->light_ops, &st->light_failed);
    st->light_syncs -= p->base_light_syncs;
    st->light_ops -= p->base_light_ops;
    for (int i = 0; p->ld && i < p->n; ++i)
    {
        st->light_sync_write += p->ld[i]->st.syncs[LD_CAUSE_WRITE] - p->base_light_cause[i][0];
        st->light_sync_read += p->ld[i]->st.syncs[LD_CAUSE_READ] - p->base_light_cause[i][1];
        st->light_sync_frame += p->ld[i]->st.syncs[LD_CAUSE_FRAME] - p->base_light_cause[i][2];
    }
    for (int i = 0; i < p->ns; ++i)
    {
        const struct pstream *s = &p->s[i];
        if (s->r) st->device_bytes += render_device_bytes(s->r);
        st->frames += s->frames;
        st->renders += s->renders;
        st->render_ns += s->render_ns;
        st->upload_ms += s->upload_ms;
        st->device_ms += s->device_ms;
        st->upload_bytes += s->upload_bytes;
        st->mesh_bytes += s->mesh_bytes;
        st->atlas_bytes += s->atlas_bytes;
        st->tex_bytes += s->tex_bytes;
        st->quad_bytes += s->quad_bytes;
        st->fresh_meshes += s->fresh_meshes;
        st->mesh_resets += s->mesh_resets;
        st->trips += s->trips;
        st->trip_overs += s->trip_overs;
        st->stream_idle_ns += s->idle_ns;
        st->mesher_ns += s->mesh_ns;
        st->dmesh_requests += s->dmesh_requests;
        st->dmesh_checked += s->dmesh_checked;
        st->dmesh_diffs += s->dmesh_diffs;
        st->dmesh_upload_bytes += s->dmesh_upload_bytes;
        st->dmesh_band_bytes += s->dmesh_band_bytes;
        st->dmesh_chunk_bytes += s->dmesh_chunk_bytes;
        st->dmesh_list_bytes += s->dmesh_list_bytes;
        st->dmesh_req_bytes += s->dmesh_req_bytes;
        st->dmesh_ctl_bytes += s->dmesh_ctl_bytes;
        st->dmesh_band_run_bytes += s->dmesh_band_run_bytes;
        st->dmesh_ms += s->dmesh_ms;
        st->frames_failed += s->frames_failed;
    }
    pthread_mutex_unlock(&p->mu);
}

void pipe_stats_reset(struct pipe *p)
{
    struct pool_stats ps;
    pool_stats(p->pool, &ps);
    struct view_times vt = {0};
    if (p->views) view_times(p->views, &vt);
    pthread_mutex_lock(&p->mu);
    p->steps = 0;
    p->poll_wait_ns = 0;
    memset(p->lat, 0, sizeof p->lat);
    /* an env's first step after this is not timed from a poll before it */
    for (int i = 0; i < p->n; ++i) p->e[i].t_polled = 0;
    p->base_pool_ns = ps.ns;
    p->base_ins = ps.instructions;
    p->base_cyc = ps.cycles;
    p->base_view = vt;
    uint64_t lf;
    light_sums(p, &p->base_light_syncs, &p->base_light_ops, &lf);
    for (int i = 0; p->ld && i < p->n; ++i)
    {
        p->base_light_cause[i][0] = p->ld[i]->st.syncs[LD_CAUSE_WRITE];
        p->base_light_cause[i][1] = p->ld[i]->st.syncs[LD_CAUSE_READ];
        p->base_light_cause[i][2] = p->ld[i]->st.syncs[LD_CAUSE_FRAME];
    }
    for (int i = 0; i < p->ns; ++i)
    {
        struct pstream *s = &p->s[i];
        s->render_ns = s->idle_ns = s->renders = s->frames = s->upload_bytes = s->mesh_ns = 0;
        s->mesh_bytes = s->atlas_bytes = s->tex_bytes = s->quad_bytes = s->fresh_meshes = s->mesh_resets = s->trips = s->trip_overs = 0;
        s->upload_ms = s->device_ms = s->dmesh_ms = 0;
        s->dmesh_requests = s->dmesh_checked = s->dmesh_diffs = s->dmesh_upload_bytes = s->frames_failed = 0;
        s->dmesh_band_bytes = s->dmesh_chunk_bytes = s->dmesh_list_bytes = s->dmesh_req_bytes = s->dmesh_ctl_bytes = s->dmesh_band_run_bytes = 0;
    }
    pthread_mutex_unlock(&p->mu);
}

int pipe_download(struct pipe *p, const struct pipe_obs *o, unsigned char *rgb)
{
    if (o->data == NULL) return -1;
    render_device(p->cfg.device);
    return render_dev_download(rgb, o->data, p->frame_bytes);
}

/* ------------------------------------------------------------- the sweep */

static uint64_t tune_rnd(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

/* the sweep's work: pool_bench's random walk, its look deltas symmetric
 * about 0 (pipe_bench's policy 2) */
static void tune_policy(struct pool_act *a, uint64_t *s)
{
    memset(a, 0, sizeof *a);
    a->kind = PA_AGENT;
    a->hotbar = -1;
    uint64_t r = tune_rnd(s);
    if ((r & 7) < 6) a->hold |= 1u << PK_FORWARD;
    if ((r >> 3 & 3) == 0) a->hold |= 1u << PK_SPRINT;
    if ((r >> 5 & 7) == 0) a->hold |= 1u << PK_JUMP;
    if ((r >> 8 & 3) == 0) a->hold |= 1u << PK_ATTACK;
    a->look_mode = PL_DLOOK;
    a->look[0] = ((float)(r >> 16 & 255) - 127.5F) * 0.1F;
    a->look[1] = ((float)(r >> 24 & 63) - 31.5F) * 0.05F;
}

/* every env stepped `steps` times, N/4 handed back at a time and stepped
 * again in one call; env-steps per second, -1 if an env failed */
static double tune_window(struct pipe *p, int steps, uint64_t *seed)
{
    const int n = p->n, ticks = p->cfg.pool.obs.ticks;
    int *left = proc_malloc(sizeof(int) * (size_t)n);
    int *ids = proc_malloc(sizeof(int) * (size_t)n);
    const struct pipe_result **res = proc_malloc(sizeof *res * (size_t)n);
    struct pool_act *acts = proc_malloc(sizeof *acts * (size_t)n);
    uint64_t t0 = now_ns(), total = 0;
    int live = 0, bad = 0;
    for (int i = 0; i < n; ++i)
    {
        left[i] = steps;
        ids[i] = i;
        tune_policy(&acts[i], &seed[i]);
    }
    if (pipe_step(p, ids, n, acts, ticks, 0) == 0) live = n;
    else bad = 1;
    int b = n / 4 > 0 ? n / 4 : 1;
    while (live > 0)
    {
        int got = pipe_poll(p, b, res);
        live -= got;
        int k = 0;
        for (int q = 0; q < got; ++q)
        {
            int id = res[q]->id;
            ++total;
            if (res[q]->r->flags & PF_ERROR) bad = 1;
            if (--left[id] <= 0 || (res[q]->r->flags & PF_ERROR)) continue;
            tune_policy(&acts[k], &seed[id]);
            if (res[q]->r->flags & PF_DEAD) { acts[k].nops = 1; acts[k].ops[0].kind = PG_RESPAWN; }
            ids[k++] = id;
        }
        if (k && pipe_step(p, ids, k, acts, ticks, 0) == 0) live += k;
        else if (k) bad = 1;
    }
    double s = (double)(now_ns() - t0) / 1e9;
    proc_free(left);
    proc_free(ids);
    proc_free(res);
    proc_free(acts);
    return bad ? -1.0 : s > 0 ? (double)total / s : 0.0;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int pipe_tune(struct pipe *p, struct pool_start *start, int steps, FILE *log, char *err, size_t n)
{
    if (start == NULL) { seterr(err, n, "the sweep needs a start", NULL); return -1; }
    int *ids = proc_malloc(sizeof(int) * (size_t)p->n);
    for (int i = 0; i < p->n; ++i) ids[i] = i;
    uint64_t *seed = proc_malloc(sizeof(uint64_t) * (size_t)p->n);
    /* streams 1 to 16, each rendering whatever is queued (B 1) or waiting
     * for all its envs (B its envs) */
    struct pipe_params cand[16];
    int nc = 0;
    const int sv[5] = {1, 2, 4, 8, 16};
    for (int a = 0; a < 5; ++a)
    {
        if (sv[a] > p->n) continue;
        int per = (p->n + sv[a] - 1) / sv[a];
        int bv[2] = {1, per};
        for (int b = 0; b < 2; ++b)
        {
            if (b > 0 && bv[b] == bv[b - 1]) continue;
            cand[nc++] = (struct pipe_params){0, sv[a], bv[b], p->par.depth};
        }
    }
    /* every window the same work: each candidate's streams (and, when the
     * pipeline chose them, its workers) made, every env reset to START, the
     * same seeds, two steps not measured (the first frames after a reset
     * mesh every section and send every render state), then STEPS measured;
     * the rounds interleave the candidates, so a drift in the machine (its
     * clocks, the co-tenants) reaches each of them */
    enum { ROUNDS = 3 };
    double rate[16][ROUNDS];
    int rc = 0;
    for (int r = 0; r < ROUNDS && rc == 0; ++r)
        for (int c = 0; c < nc && rc == 0; ++c)
        {
            if (pipe_params_set(p, &cand[c], err, n) != 0 || pipe_reset(p, ids, p->n, start, err, n) != 0) { rc = -1; break; }
            for (int i = 0; i < p->n; ++i) seed[i] = 0x2545f4914f6cdd1dull * (uint64_t)(i + 1);
            if (tune_window(p, 2, seed) < 0 || (rate[c][r] = tune_window(p, steps, seed)) < 0)
            {
                seterr(err, n, "an env failed during the sweep", NULL);
                rc = -1;
            }
        }
    int best = 0;
    double bestv = -1;
    for (int c = 0; c < nc && rc == 0; ++c)
    {
        double v[ROUNDS];
        memcpy(v, rate[c], sizeof v);
        qsort(v, ROUNDS, sizeof v[0], cmp_double);
        if (log)
            fprintf(log, "   sweep: %d streams, batch %3d: median %.0f env-steps/s (%.0f %.0f %.0f)\n", cand[c].streams,
                    cand[c].batch, v[ROUNDS / 2], rate[c][0], rate[c][1], rate[c][2]);
        if (v[ROUNDS / 2] > bestv) { bestv = v[ROUNDS / 2]; best = c; }
    }
    if (rc == 0) rc = pipe_params_set(p, &cand[best], err, n);
    if (log && rc == 0)
        fprintf(log, "   sweep: chose %d streams, batch %d (groups of %d, depth %d), %d workers\n", p->par.streams,
                p->par.batch, p->par.group, p->par.depth, pool_threads(p->pool));
    proc_free(ids);
    proc_free(seed);
    return rc;
}
