/* ntloop's pipe: the hot path. A group from recv (its books: the digest, the clock wrap, the records, the ladder, the
 * reward's running return), staged on its stream into its segment, the policy launched, the actions copied back and
 * the group sent once its event has fired; --serve's casts of the groups received together. loop.c's header has the
 * contract. */
#define _GNU_SOURCE
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include "../../mods/macros/pearl.h"   /* neth_vec_pearl_census */
#include "neth_pipe_internal.h"
#include "neth_runconf.h"
#include "curric.h"

/* castev[g] of a group serve_cast cast alone on its own stream (lane/cuda-graph): no event between the two */
#define CAST_OWN (-2)

/* vec_bench's stand-in on the host (--ab's A: vec_bench's own loop) */
static int coin(uint64_t *s, int p) { return (int)(xs(s) & 1023) < p; }

static void draw(uint64_t *s, int32_t *a)
{
    for (int j = 0; j < 2; ++j) a[j] = coin(s, 512) ? 20 : 16 + (int)(xs(s) % 9);
    static const int p[14] = {700, 40, 60, 60, 200, 100, 20, 500, 80, 20, 10, 100, 0, 0};
    for (int j = 0; j < 14; ++j) a[2 + j] = coin(s, p[j]);
}
void neth_pipe_lap(struct neth_pipe *L, int k)
{
    double t = now_s();
    L->tm[k] += t - L->tick;
    L->tick = t;
}
/* the oldest launched group waited for (or not: wait 0) and sent */
static void send_at(struct neth_pipe *L, int q)
{
    int g = L->pq[q];
    L->t_rt += now_s() - L->t_lch[g];
    memmove(L->pq + q, L->pq + q + 1, (size_t)(L->np - q - 1) * sizeof *L->pq);
    --L->np;
    if (neth_vec_send(L->v, g))
    {
        printf("FAIL N %d: send %d refused\n", L->n, g);
        L->fail = 1;
    }
    ++L->inflight;
    L->env_steps += L->gs;
    ++L->gsteps;
}

/* lane/cuda-graph: group g's last launch done on the device: its flag (--poll-flag, --graph: one load, no CUDA call),
 * else its event */
static inline int group_done(struct neth_pipe *L, int g)
{
    if (L->poll) return (int32_t)(__atomic_load_n(&L->done_h[g], __ATOMIC_ACQUIRE) - L->done_seq[g]) >= 0;
    return cudaEventQuery(L->ev[g]) == cudaSuccess;
}

static void send_oldest(struct neth_pipe *L)
{
    int g = L->pq[0];
    if (L->poll)
        for (unsigned k = 1; !group_done(L, g); ++k)
        {
            /* a device fault never sets the flag: the stream asked now and then */
            if (!(k & 0xffff))
            {
                cudaError_t e = cudaStreamQuery(L->st[g]);
                if (e != cudaErrorNotReady) CK(e);
            }
#if defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        }
    else
        CK(cudaEventSynchronize(L->ev[g]));
    neth_pipe_lap(L, T_SYNC);
    send_at(L, 0);
    neth_pipe_lap(L, T_SEND);
}

/* every launched group sent */
static void serve_cast(struct neth_pipe *L, int g);
void neth_pipe_launch(struct neth_pipe *L, int g);

void neth_pipe_drain(struct neth_pipe *L)
{
    /* --serve: the received groups launched first */
    while (L->nrq > 0 && !L->fail)
    {
        int g = L->rq[0];
        memmove(L->rq, L->rq + 1, (size_t)--L->nrq * sizeof *L->rq);
        serve_cast(L, g);
        if (!L->fail) neth_pipe_launch(L, g);
    }
    while (L->np > 0 && !L->fail) send_oldest(L);
}

static int took(struct neth_pipe *L, int g, int books);

/* every group the device finished goes back to the env, and the learner's queue served */
static void serve_device(struct neth_pipe *L)
{
    neth_pipe_lap(L, T_TAIL);
    for (int q = 0; q < L->np && !L->fail;)
        if (group_done(L, L->pq[q])) send_at(L, q);
        else ++q;
    neth_pipe_lap(L, T_SEND);
    if (L->learn)
    {
        neth_train_poll(L, 0);
        neth_pipe_lap(L, T_LAUNCH);
    }
}

/* the next group from the env, its books kept (books 0: vec_bench's loop, the digest alone): the group, -1 failed, -2
 * its quota reached (not to be sent again) */
static int take(struct neth_pipe *L, int books)
{
    serve_device(L);
    /* recv needs one in flight */
    while (L->inflight == 0 && L->np > 0 && !L->fail) send_oldest(L);
    if (L->inflight == 0 || L->fail) return -1;
    int g = neth_vec_recv(L->v, L->err, sizeof L->err);
    neth_pipe_lap(L, T_RECV);
    if (g < 0)
    {
        printf("FAIL N %d: %s\n", L->n, L->err);
        L->fail = 1;
        return -1;
    }
    return took(L, g, books);
}

/* group g from recv: its books (take's) */
static int took(struct neth_pipe *L, int g, int books)
{
    --L->inflight;
    const struct neth_vec_bufs *b = &L->b;
    int lo = g * L->gs, hi = lo + L->gs, was = L->seen[lo];
    for (int i = lo; i < hi; ++i)
    {
        if (L->seen[i] && L->digest)
        {
            uint64_t h = L->hash[i];
            h = fnv(h, &b->rewards[i], sizeof b->rewards[i]);
            h = fnv(h, &b->driver[i], 1);
            h = fnv(h, &b->kind[i], 1);
            h = fnv(h, &b->chosen[i], 1);
            /* the 155 base scalars (a .ls build's late ones not hashed, so its digest is the base build's
             * when it plays the same trajectory) */
            L->hash[i] = fnv(h, b->scalars + (size_t)i * NETH_NET_NS, NETH_VEC_NSCALAR * sizeof(float));
        }
        L->seen[i] = 1;
    }
    if (L->clock_wrap)
        for (int i = lo; i < hi; ++i)
        {
            float *c = b->scalars + (size_t)i * NETH_NET_NS + 7;
            *c = *c - floorf(*c);   /* torch.remainder(x, 1.0): exact for x >= 0 */
        }
    if (L->first)
        for (int i = lo; i < hi; ++i)
            for (int t = 0; t < SC_TIMED; ++t)
                if (L->first[i * SC_TIMED + t] < 0 && b->scalars[(size_t)i * NETH_NET_NS + neth_timed_col[t]] > 0)
                    L->first[i * SC_TIMED + t] = L->gcount[g];
    if (L->dist && was) neth_distill_took(L->dist, b, lo, L->gs);  /* the step's labels */
    if (L->lab && was)
    {
        /* each env's level from its driver, drawn for the next episode where this step ended one */
        const uint8_t *dr = neth_distill_drive(L->dist);
        for (int i = lo; i < hi; ++i)
            if (b->truncations[i]) neth_vec_env_level(L->v, i, dr[i] == 2);
    }
    if (!was && L->rows_path && L->rows_env >= lo && L->rows_env < hi &&
        neth_rl_rows(neth_vec_env(L->v), L->rows_env, L->rows_path, 0))
    {
        printf("FAIL --rec-rows %d:%s\n", L->rows_env, L->rows_path);
        L->fail = 1;
    }
    if (!was && L->tape_path && L->tape_env >= lo && L->tape_env < hi &&
        neth_vec_tape(L->v, L->tape_env, L->tape_path))
    {
        printf("FAIL --tape %d:%s\n", L->tape_env, L->tape_path);
        L->fail = 1;
    }
    if (L->lad) neth_ladder_ends(L, g, was);
    if (L->rec || L->lad)
    {
        /* the episodes this step ended: each env's worker recorded its own as its last step finished, so by this
         * recv the group's are in the vec env's ring, and maybe other groups' still in flight (kept pending until
         * their own recv). The ladder takes them here too, each with the rung its episode ran */
        int k = 0;
        if (was && memchr(b->truncations + lo, 1, (size_t)L->gs))
        {
            int got = neth_vec_episodes(L->v, L->pend + L->npend, 2 * L->n - L->npend);
            L->npend += got;
            int keep = 0;
            for (int q = 0; q < L->npend; ++q)
                if (L->pend[q].env >= lo && L->pend[q].env < hi) L->eptmp[k++] = L->pend[q];
                else L->pend[keep++] = L->pend[q];
            L->npend = keep;
            /* log_rows' copy (only with --out: nothing else reads them), in the group's order: a record's rung is
             * known at its own group's recv */
            if (L->stash)
            {
                int over = L->nstash + k - L->stash_cap;
                if (over > 0)
                {
                    memmove(L->stash, L->stash + over, (size_t)(L->nstash - over) * sizeof *L->stash);
                    memmove(L->stash_rung, L->stash_rung + over, (size_t)(L->nstash - over) * sizeof *L->stash_rung);
                    L->nstash -= over;
                    L->stash_drop += over;
                }
                memcpy(L->stash + L->nstash, L->eptmp, (size_t)k * sizeof *L->stash);
            }
        }
        for (int q = 0; q < k; ++q)
        {
            int r = L->lad ? L->lad->ended[L->eptmp[q].env] : -1;
            L->eprung[q] = r;
            L->epfid[q] = r >= 0 ? L->lad->code[r] : L->sim_code;
            if (L->lad) neth_ladder_book(L, &L->eptmp[q], r);
            if (L->stash) L->stash_rung[L->nstash + q] = r;
        }
        if (L->stash) L->nstash += k;
        if (L->rec) rec_group(L->rec, g, lo, L->gs, was, b, L->eptmp, k, L->eprung, L->epfid);
    }
    if (L->lad)
    {
        neth_ladder_assign(L, g);
        /* --tape writes env E's next episode, the one its first reset puts on its rung: vanilla's alone replay on
         * the Java client */
        if (!was && L->tape_path && L->tape_env >= lo && L->tape_env < hi && !L->lad->vanilla[L->lad->pend[L->tape_env]])
        {
            printf("FAIL --tape %d: refused: the ladder puts env %d's next episode on rung %d (%s), not vanilla: the "
                   "Java client runs vanilla 1.7.10\n", L->tape_env, L->tape_env, L->lad->pend[L->tape_env],
                   L->lad->name[L->lad->pend[L->tape_env]]);
            L->fail = 1;
        }
    }
    if (was && books)
    {
        /* the last action's return, its spread on every gs/8-th env; at the episodes' end the returns zeroed and the
         * reset step's arrival counted (its reward 0) */
        int any_trunc = 0;
        for (int i = lo; i < hi; ++i)
        {
            L->ret_run[i] = L->ret_run[i] * L->gamma + (double)b->rewards[i];
            any_trunc |= b->truncations[i] != 0;
        }
        /* the envs whose episode ended (a group's envs end apart with starts.episode_seconds; without
         * it all of them at once, the books as before: every sampled env, then every sampled env's zero) */
        for (int pass = 0; pass < 1 + any_trunc; ++pass)
        {
            if (pass)
                for (int i = lo; i < hi; ++i)
                    if (b->truncations[i]) L->ret_run[i] = 0;
            for (int i = lo; i < hi; i += L->wstride)
            {
                if (pass && !b->truncations[i]) continue;
                double x = L->ret_run[i], d = x - L->w_mean;
                L->w_n += 1;
                L->w_mean += d / L->w_n;
                L->w_m2 += d * (x - L->w_mean);
            }
        }
        /* each env's running return into its stage's spread (every env; curric.h cur_ret) */
        if (L->crc)
            for (int i = lo; i < hi; ++i)
                cur_ret(L->crc, cur_stage_of(L->crc, neth_vec_env_start(L->v, i)), L->ret_run[i]);
    }
    if (was && L->sscale)
        /* ppo.stage_scale: the rewards the learner reads over their stages' scales (after the books, the digest and
         * the records, which keep the env's own) */
        for (int i = lo; i < hi; ++i)
            b->rewards[i] = (float)((double)b->rewards[i] /
                                    cur_scale(L->crc, cur_stage_of(L->crc, neth_vec_env_start(L->v, i))));
    if (L->received) L->received(L, g);     /* --score-books */
    if (L->quota && ++L->gcount[g] >= L->quota)
    {
        /* never sent again: its step's buffers stay as they are */
        if (L->rec) rec_group_done(L->rec, g, b);
        --L->live;
        neth_pipe_lap(L, T_HOST);
        return -2;
    }
    neth_pipe_lap(L, T_HOST);
    return g;
}

/* --frame-digest: what the policy and the learner read, from the segment (a check: the stream waited for): the frames,
 * the scalars and the last action's reward, end and driver (rw NULL: none) */
static void frame_hash(struct neth_pipe *L, int lo, const uint8_t *obs, const neth_sc_t *sc, const float *rw,
                       const uint8_t *dn, const uint8_t *dv, cudaStream_t s)
{
    int gs = L->gs;
    size_t so = (size_t)gs * L->oe, ss = (size_t)gs * NETH_NET_NS * sizeof(neth_sc_t);
    CK(cudaMemcpyAsync(L->fhost, obs, so, cudaMemcpyDeviceToHost, s));
    CK(cudaMemcpyAsync(L->fhost + so, sc, ss, cudaMemcpyDeviceToHost, s));
    if (rw)
    {
        CK(cudaMemcpyAsync(L->fhost + so + ss, rw, gs * sizeof(float), cudaMemcpyDeviceToHost, s));
        CK(cudaMemcpyAsync(L->fhost + so + ss + gs * sizeof(float), dn, gs, cudaMemcpyDeviceToHost, s));
        CK(cudaMemcpyAsync(L->fhost + so + ss + gs * sizeof(float) + gs, dv, gs, cudaMemcpyDeviceToHost, s));
    }
    CK(cudaStreamSynchronize(s));
    for (int i = 0; i < gs; ++i)
    {
        uint64_t h = fnv(L->fhash[lo + i], L->fhost + (size_t)i * L->oe, L->oe);
        h = fnv(h, L->fhost + so + (size_t)i * NETH_NET_NS * sizeof(neth_sc_t), NETH_NET_NS * sizeof(neth_sc_t));
        if (rw)
        {
            h = fnv(h, L->fhost + so + ss + (size_t)i * sizeof(float), sizeof(float));
            h = fnv(h, L->fhost + so + ss + gs * sizeof(float) + i, 1);
            h = fnv(h, L->fhost + so + ss + gs * sizeof(float) + gs + i, 1);
        }
        L->fhash[lo + i] = h;
    }
}

/* --frame-digest's companion (lane/cuda-graph): the policy's outputs as the segment holds them, the step's actions,
 * logp and value and, at a finished segment, its bootstrap; after the launch (the stream waited for) */
static void policy_hash(struct neth_pipe *L, int lo, const int32_t *act, const float *logp, const float *val,
                        const struct neth_seg *fin, cudaStream_t s)
{
    int gs = L->gs;
    size_t sa = (size_t)gs * NETH_NET_ACTION * sizeof(int32_t), sf = (size_t)gs * sizeof(float);
    uint8_t *p = L->fhost;  /* the frame's copy is done with by now (fhost holds a frame and more) */
    CK(cudaMemcpyAsync(p, act, sa, cudaMemcpyDeviceToHost, s));
    CK(cudaMemcpyAsync(p + sa, logp, sf, cudaMemcpyDeviceToHost, s));
    CK(cudaMemcpyAsync(p + sa + sf, val, sf, cudaMemcpyDeviceToHost, s));
    if (fin) CK(cudaMemcpyAsync(p + sa + 2 * sf, fin->boot, sf, cudaMemcpyDeviceToHost, s));
    CK(cudaStreamSynchronize(s));
    for (int i = 0; i < gs; ++i)
    {
        uint64_t h = fnv(L->phash[lo + i], p + (size_t)i * NETH_NET_ACTION * sizeof(int32_t), NETH_NET_ACTION * sizeof(int32_t));
        h = fnv(h, p + sa + (size_t)i * sizeof(float), sizeof(float));
        h = fnv(h, p + sa + sf + (size_t)i * sizeof(float), sizeof(float));
        if (fin) h = fnv(h, p + sa + 2 * sf + (size_t)i * sizeof(float), sizeof(float));
        L->phash[lo + i] = h;
    }
}

/* --graph: group g's step captured on its stream for actor image img and critic image (key = img * 2 + critic's):
 * two copy nodes side by side (the staging block, the scalars), the wait for the cast (an event wait node on gcev[g],
 * the cast on its side stream: the copies run beside it), the stage kernel, the actor, the critic, the commit kernel;
 * every place fixed, the varying ones in the staging block; instantiated into gx[g][key]. Measured on the RTX PRO
 * 6000 (nsys, node trace): the cast's end to the stage kernel 2.1 us (the cast on the group's own stream ahead of the
 * launch: 21 us, the graph's start waiting on the stream), the stage kernel to the commit's end 27.6 us (the event
 * path's neth_stage_k to neth_commit_k: 47.3 us) */
static void graph_capture(struct neth_pipe *L, int g, int img, int key)
{
    cudaStream_t s = L->st[g];
    int gs = L->gs, lo = g * gs;
    struct neth_gseg sg;
    for (int h = 0; h < 2; ++h)
    {
        const struct neth_seg *S = &L->sg[2 * g + h];
        sg.obs[h] = S->obs, sg.sc[h] = S->sc, sg.act[h] = S->act, sg.logp[h] = S->logp, sg.val[h] = S->val;
        sg.rew[h] = S->rew, sg.boot[h] = S->boot, sg.done[h] = S->done, sg.drv[h] = S->drv;
    }
    const uint8_t *frames = (const uint8_t *)L->b.obs + (size_t)lo * L->oe;
    neth_sc_t *gsc = L->g_sc + (size_t)lo * NETH_NET_NS;
    struct neth_io io = {g, lo, gs, frames, gsc, L->g_act + (size_t)lo * NETH_NET_ACTION, L->g_logp + lo, L->g_val + lo,
                         NULL, NULL};
    uint8_t *stg = L->gstg_d + (size_t)g * L->gstg_b;
    float *scf = L->g_scf + (size_t)lo * NETH_NET_NS;
#if NETH_NET_RNN
    /* the group's state (fixed), an episode starting where the staging block's done says (zero bytes before the
     * group's first outcome: the launch without the graph passes NULL there) */
    io.h = L->hst + (size_t)lo * NETH_NET_D;
    io.first = stg + sizeof(struct neth_gslot) + (size_t)gs * sizeof(float);
#endif
    cudaGraph_t gr;
    CK(cudaStreamBeginCapture(s, cudaStreamCaptureModeThreadLocal));
    /* two copy nodes side by side: the staging block, and the scalars from the env's buffer */
    CK(cudaEventRecord(L->gfork, s));
    CK(cudaStreamWaitEvent(L->gaux, L->gfork, 0));
    CK(cudaMemcpyAsync(scf, L->b.scalars + (size_t)lo * NETH_NET_NS, (size_t)gs * NETH_NET_NS * sizeof(float),
                       cudaMemcpyHostToDevice, L->gaux));
    CK(cudaEventRecord(L->gjoin, L->gaux));
    CK(cudaMemcpyAsync(stg, L->gstg_h + (size_t)g * L->gstg_b, L->gstg_b, cudaMemcpyHostToDevice, s));
    CK(cudaStreamWaitEvent(s, L->gjoin, 0));
    if (L->gcst) CK(cudaStreamWaitEvent(s, L->gcev[g], cudaEventWaitExternal));    /* the cast (not --cast-hold's) */
    neth_gstage(stg, scf, &sg, frames, gs, L->oe, gsc, s);
#if NETH_NET_RNN
    neth_grnn_h0(stg, io.h, gs, L->sg[2 * g].h0, L->sg[2 * g + 1].h0, s);  /* before the step moves h */
#endif
    int bad = neth_policy_actor_step(L->pol, img, &io, s);
    if (L->lr.crit) neth_critic_act(L->lr.crit, gs, gsc, io.value, s);
    neth_gcommit(stg, &sg, gs, L->T, io.actions, io.logp, io.value, L->dact + (size_t)lo * NETH_NET_ACTION,
                 L->done_d + g, s);
    CK(cudaStreamEndCapture(s, &gr));
    if (bad)
    {
        printf("FAIL N %d: --graph: the actor's step on group %d did not capture\n", L->n, g);
        L->fail = 1;
        CK(cudaGraphDestroy(gr));
        return;
    }
    CK(cudaGraphInstantiateWithFlags(&L->gx[g * 4 + key], gr, 0));
    CK(cudaGraphDestroy(gr));
}

/* --graph: neth_pipe_launch's work as one replay. The host's part: the segment's bookkeeping (as below), the learner's
 * claim on a fresh segment, the cast's event, the actor's workspace on the current image, the slot; then one
 * cudaGraphLaunch (the finished segment's ready event after it). Group g is never launched again before its flag says
 * this replay is done (it is sent only then, and launched only once received back), so the slot is never rewritten
 * while a replay reads it */
static void launch_graph(struct neth_pipe *L, int g)
{
    const struct neth_vec_bufs *b = &L->b;
    cudaStream_t s = L->st[g];
    int gs = L->gs, lo = g * gs, t0 = L->tg[g], t = t0, ph = L->cur[g];
    struct neth_seg *fin = NULL;
    if (t == L->T)
    {
        ++L->segments;
        if (L->learning) fin = &L->sg[2 * g + L->cur[g]];
        L->cur[g] ^= 1;
        if (L->learn) neth_train_claim(L, &L->sg[2 * g + L->cur[g]], s);
        t = 0;
    }
    if (L->staged) L->staged(L, g);     /* --obs-dump, --view-books: before --zero-sc */
    if (L->zsc_n)
        for (int i = 0; i < gs; ++i)
            memset(b->scalars + (size_t)(lo + i) * NETH_NET_NS + L->zsc_lo, 0, (size_t)L->zsc_n * sizeof(float));
    if (L->serve)
    {
        if (L->castev[g] >= 0) CK(cudaStreamWaitEvent(s, L->cev[L->castev[g]], 0));
        L->castev[g] = -1;
    }
    else if (neth_vec_cast(L->v, g, L->gcst ? L->gcst[g] : s))
    {
        printf("FAIL N %d: the cast of group %d\n", L->n, g);
        L->fail = 1;
        return;
    }
    else if (L->gcst)
        CK(cudaEventRecord(L->gcev[g], L->gcst[g]));
    int img = neth_policy_actor_prep(L->pol, g, s);
    if (img < 0)
    {
        printf("FAIL N %d: --graph: the actor's workspace on group %d\n", L->n, g);
        L->fail = 1;
        return;
    }
    int key = img * 2 + (L->lr.crit ? neth_critic_cur(L->lr.crit) : 0);
    /* the staging block: the slot, then the last action's outcome (the graph's first node copies it at its turn; the
     * host writes it again only once the flag says the replay is done) */
    uint8_t *hb = L->gstg_h + (size_t)g * L->gstg_b;
    struct neth_gslot *sl = (struct neth_gslot *)hb;
    sl->half = L->cur[g], sl->t = t, sl->phalf = ph, sl->pt = t0 - 1, sl->has_prev = t0 > 0, sl->boot = fin != NULL;
    sl->seq = ++L->done_seq[g];
    if (t0 > 0)
    {
        uint8_t *o = hb + sizeof *sl;
        memcpy(o, b->rewards + lo, (size_t)gs * sizeof(float));
        memcpy(o + (size_t)gs * sizeof(float), b->truncations + lo, (size_t)gs);
        memcpy(o + (size_t)gs * (sizeof(float) + 1), b->driver + lo, (size_t)gs);
    }
    if (!L->gx[g * 4 + key]) graph_capture(L, g, img, key);
    if (L->fail) return;
    CK(cudaGraphLaunch(L->gx[g * 4 + key], s));
    if (L->learn) CK(cudaEventRecord(L->ev[g], s));
    if (fin) CK(cudaEventRecord(fin->ready, s));
    if (L->fhash || L->phash)
    {
        /* the checks read what the replay wrote: the segment's step t, the last action's outcome at its step */
        struct neth_seg *S = &L->sg[2 * g + L->cur[g]], *P = &L->sg[2 * g + ph];
        size_t pa = t0 > 0 ? (size_t)(t0 - 1) * gs : 0;
        if (L->fhash)
            frame_hash(L, lo, S->obs + (size_t)t * gs * L->oe, S->sc + (size_t)t * gs * NETH_NET_NS, t0 > 0 ? P->rew + pa : NULL,
                       P->done + pa, P->drv + pa, s);
        if (L->phash)
            policy_hash(L, lo, S->act + (size_t)t * gs * NETH_NET_ACTION, S->logp + (size_t)t * gs, S->val + (size_t)t * gs,
                        fin, s);
    }
    L->tg[g] = t + 1;
    L->t_lch[g] = now_s();
    L->pq[L->np++] = g;
    if (fin)
    {
        fin->scale = L->sscale ? 1.0f : (float)neth_train_reward_scale(L);
        fin->job = 1;
        L->lr.q[L->lr.nq++] = fin;
        neth_train_poll(L, 0);
    }
    if (L->rec) rec_group_done(L->rec, g, b);
    neth_pipe_lap(L, T_LAUNCH);
    while (L->np > L->depth && !L->fail) send_oldest(L);
}

/* group g staged on its stream: the last action's outcome into its segment, the step's observation and scalars, the
 * policy, the actions back to the env's buffer, its event; a full segment to the learner; then at most depth left
 * unsent */
void neth_pipe_launch(struct neth_pipe *L, int g)
{
    if (L->graph)
    {
        launch_graph(L, g);
        return;
    }
    const struct neth_vec_bufs *b = &L->b;
    cudaStream_t s = L->st[g];
    int gs = L->gs, lo = g * gs, t = L->tg[g];
    struct neth_seg *S = &L->sg[2 * g + L->cur[g]], *fin = NULL;
    float *rw = NULL;   /* the last action's outcome's place in the segment (none at a group's first step) */
    uint8_t *dn = NULL, *dv = NULL;
    if (t > 0) rw = S->rew + (size_t)(t - 1) * gs, dn = S->done + (size_t)(t - 1) * gs, dv = S->drv + (size_t)(t - 1) * gs;
    if (t > 0 && !L->stage)
    {
        CK(cudaMemcpyAsync(rw, b->rewards + lo, gs * sizeof(float), cudaMemcpyHostToDevice, s));
        CK(cudaMemcpyAsync(dn, b->truncations + lo, gs, cudaMemcpyHostToDevice, s));
        CK(cudaMemcpyAsync(dv, b->driver + lo, gs, cudaMemcpyHostToDevice, s));
    }
    if (t > 0 && L->dist) neth_distill_stage_step(L->dist, (int)(S - L->sg), t - 1, lo, gs, s);  /* beside its reward */
    if (t == L->T)
    {
        /* the segment is the learner's; the group's other one is filled next, once the learner has read it */
        ++L->segments;
        if (L->learning) fin = S;
        L->cur[g] ^= 1;
        S = &L->sg[2 * g + L->cur[g]];
        if (L->learn) neth_train_claim(L, S, s);
        t = 0;
    }
    uint8_t *obs = S->obs + (size_t)t * gs * L->oe;
    neth_sc_t *sc = S->sc + (size_t)t * gs * NETH_NET_NS;
    const uint8_t *src = (const uint8_t *)b->obs + (size_t)lo * L->oe;
    if (L->staged) L->staged(L, g);     /* --obs-dump, --view-books: before --zero-sc */
    if (L->psch)
    {
        /* the critic's scalars, before --zero-sc */
        const float *sraw = b->scalars + (size_t)lo * NETH_NET_NS;
        neth_sc_t *pdst = L->psch + (size_t)lo * NETH_NET_NS;
        size_t nsc = (size_t)gs * NETH_NET_NS;
#if NETH_SC_BF16
        for (size_t k = 0; k < nsc; ++k) pdst[k] = neth_f32_to_bf16_host(sraw[k]);
#else
        memcpy(pdst, sraw, nsc * sizeof(float));
#endif
        CK(cudaMemcpyAsync(S->psc + (size_t)t * gs * NETH_NET_NS, pdst,
                           nsc * sizeof(neth_sc_t), cudaMemcpyHostToDevice, s));
    }
    if (L->zsc_n)
        for (int i = 0; i < gs; ++i)
            memset(b->scalars + (size_t)(lo + i) * NETH_NET_NS + L->zsc_lo, 0, (size_t)L->zsc_n * sizeof(float));
    if (b->obs_device)
    {
        if (L->serve)
        {
            /* cast by serve_cast's launch: the stream follows it (CAST_OWN: on this stream, already in order) */
            if (L->castev[g] >= 0) CK(cudaStreamWaitEvent(s, L->cev[L->castev[g]], 0));
            L->castev[g] = -1;
        }
        else if (neth_vec_cast(L->v, g, s))
        {
            printf("FAIL N %d: the cast of group %d\n", L->n, g);
            L->fail = 1;
            return;
        }
        CK(cudaMemcpyAsync(obs, src, gs * L->oe, cudaMemcpyDeviceToDevice, s));
    }
    else if (!L->stage)
        CK(cudaMemcpyAsync(obs, src, gs * L->oe, cudaMemcpyHostToDevice, s));
    if (L->stage)
        neth_stage(L->drew, L->ddone, L->ddrv, L->dsc, b->obs_device ? NULL : L->dobs, lo, gs, L->oe, rw, dn, dv, sc,
                 b->obs_device ? NULL : obs, s);
    else
    {
#if NETH_SC_BF16
        const float *sraw = b->scalars + (size_t)lo * NETH_NET_NS;
        neth_sc_t *sdst = L->sch + (size_t)lo * NETH_NET_NS;
        size_t nsc = (size_t)gs * NETH_NET_NS;
        for (size_t k = 0; k < nsc; ++k) sdst[k] = neth_f32_to_bf16_host(sraw[k]);
        CK(cudaMemcpyAsync(sc, sdst, nsc * sizeof(neth_sc_t), cudaMemcpyHostToDevice, s));
#else
        CK(cudaMemcpyAsync(sc, b->scalars + (size_t)lo * NETH_NET_NS, (size_t)gs * NETH_NET_NS * sizeof(float), cudaMemcpyHostToDevice, s));
#endif
    }
    if (L->tone) neth_tone(obs, gs, L->oe, L->tone, L->tlut, s);     /* --obs-tone */
    if (L->fhash) frame_hash(L, lo, obs, sc, rw, dn, dv, s);
    struct neth_io io = {g, lo, gs, obs, sc, S->act + (size_t)t * gs * NETH_NET_ACTION, S->logp + (size_t)t * gs,
                       S->val + (size_t)t * gs, NULL, NULL};
#if NETH_NET_RNN
    /* the group's state, an episode starting where the last action ended one (dn: none at the group's
     * first step, its state zero); at a segment's start the state as the learner's scan takes it */
    io.h = L->hst + (size_t)lo * NETH_NET_D;
    io.first = dn;
    if (t == 0) neth_rnn_h0(io.h, dn, gs, S->h0, s);
#endif
    if (L->PS.launch ? neth_distill_launch(L->dist, L->pol, &L->PS, &io, s) : L->pol->launch(L->pol->ctx, &io, s))
    {
        printf("FAIL N %d: the policy's launch on group %d: %s\n", L->n, g, cudaGetErrorString(cudaGetLastError()));
        L->fail = 1;
        return;
    }
    /* the privileged critic's value added to the actor's (the segment's val and the bootstrap read it) */
    if (L->lr.crit) neth_critic_act(L->lr.crit, gs, S->psc + (size_t)t * gs * NETH_NET_NS, io.value, s);
    if (fin)
    {
        /* the bootstrap before the event the learner waits on (fastppo: after it, the learner read it unmade) */
        neth_boot(io.value, fin->done + (size_t)(L->T - 1) * gs, gs, fin->boot, s);
        CK(cudaEventRecord(fin->ready, s));
    }
    if (L->poll)
    {
        /* lane/cuda-graph --poll-flag: the actions to the env's mapped buffer and the group's flag, one launch; the
         * event only for the learner's refresh (it waits for every group's launches so far) */
        neth_commit(io.actions, gs * NETH_NET_ACTION, L->dact + (size_t)lo * NETH_NET_ACTION, L->done_d + g,
                    ++L->done_seq[g], s);
        if (L->learn) CK(cudaEventRecord(L->ev[g], s));
    }
    else
    {
        CK(cudaMemcpyAsync(b->actions + (size_t)lo * NETH_NET_ACTION, io.actions, (size_t)gs * NETH_NET_ACTION * sizeof(int32_t),
                           cudaMemcpyDeviceToHost, s));
        CK(cudaEventRecord(L->ev[g], s));
    }
    if (L->phash) policy_hash(L, lo, io.actions, io.logp, io.value, fin, s);
    L->tg[g] = t + 1;
    L->t_lch[g] = now_s();
    L->pq[L->np++] = g;
    if (fin)
    {
        fin->scale = L->sscale ? 1.0f : (float)neth_train_reward_scale(L);
        fin->job = 1;
        L->lr.q[L->lr.nq++] = fin;
        neth_train_poll(L, 0);
    }
    /* the rest of the step's buffers to the recorder while the device runs the policy */
    if (L->rec) rec_group_done(L->rec, g, b);
    neth_pipe_lap(L, T_LAUNCH);
    while (L->np > L->depth && !L->fail) send_oldest(L);
}

/* --serve: group g can be launched now (cast already, or no held cast running) */
static int serve_ready(struct neth_pipe *L, int g)
{
    if (!L->cast_hold || !L->b.obs_device || L->castev[g] != -1 || L->last_ce < 0) return 1;
    if (cudaEventQuery(L->cev[L->last_ce]) != cudaSuccess) return 0;
    L->last_ce = -1;
    return 1;
}

/* --serve's take: the device's finished groups sent and every group the env has finished taken, without
 * waiting on either while the other may have work; it waits in recv only with nothing launched. The next group to
 * neth_pipe_launch (rq's first), -1 when nothing is left, -2 its quota reached */
static int serve_take(struct neth_pipe *L)
{
    while (!L->fail)
    {
        serve_device(L);
        int g = -2;
        if (L->inflight > 0)
        {
            g = L->np > 0 || L->nrq > 0 ? neth_vec_try_recv(L->v, L->err, sizeof L->err)
                                        : neth_vec_recv(L->v, L->err, sizeof L->err);
            neth_pipe_lap(L, T_RECV);
            if (g == -1)
            {
                printf("FAIL N %d: %s\n", L->n, L->err);
                L->fail = 1;
                return -1;
            }
            if (g >= 0)
            {
                g = took(L, g, 1);
                if (g == -2) return -2;
                L->rq[L->nrq++] = g;
                continue;   /* more may be done: one cast for them all */
            }
        }
        if (L->nrq > 0 && serve_ready(L, L->rq[0])) break;
        if (L->np == 0 && L->nrq == 0) return -1;   /* nothing in the env or on the device */
        if (L->idle_ns > 0)
        {
            /* --idle-us: the core the spin held, given back between looks */
            struct timespec ts = {0, L->idle_ns};
            nanosleep(&ts, NULL);
        }
#if defined(__x86_64__)
        else
            __builtin_ia32_pause();
#endif
    }
    if (L->fail) return -1;
    int g = L->rq[0];
    memmove(L->rq, L->rq + 1, (size_t)--L->nrq * sizeof *L->rq);
    return g;
}

/* --serve's cast: group g (the next to launch) and every received group not cast yet, at most cast_max, in one launch
 * on cs; an event of cev's ring each group's stream waits for */
static void serve_cast(struct neth_pipe *L, int g)
{
    if (!L->b.obs_device || L->castev[g] != -1) return;
    int gl[L->G], k = 0;
    gl[k++] = g;
    for (int q = 0; q < L->nrq && (L->cast_max == 0 || k < L->cast_max); ++q)
        if (L->castev[L->rq[q]] == -1) gl[k++] = L->rq[q];
    /* on g's own stream, so the casts of groups taken apart overlap on the device (one cast stream ran them one after
     * another: 90k env-steps/s against 187k at 128 envs in 4 groups) */
    cudaStream_t s = L->gcst && !L->cast_hold ? L->gcst[g] : L->st[g];
    double t0 = now_s();
    if (neth_vec_cast_groups(L->v, gl, k, s))
    {
        printf("FAIL N %d: the cast of %d groups from group %d\n", L->n, k, g);
        L->fail = 1;
        return;
    }
    L->t_cast += now_s() - t0;
    if (L->gcst && !L->cast_hold)
    {
        /* --graph: the cast on g's side stream; each cast group's graph waits on its own event inside */
        for (int q = 0; q < k; ++q)
        {
            CK(cudaEventRecord(L->gcev[gl[q]], s));
            L->castev[gl[q]] = CAST_OWN;
        }
        ++L->casts;
        L->cast_groups += k;
        return;
    }
    if (k == 1 && !L->cast_hold)
    {
        /* lane/cuda-graph: g alone, cast on its own stream, launched next on it: no event to record or wait for
         * (--cast-hold's serve_ready reads the last cast's event, so it keeps one) */
        L->castev[g] = CAST_OWN;
        ++L->casts;
        L->cast_groups += k;
        return;
    }
    CK(cudaEventRecord(L->cev[L->ce], s));
    for (int q = 0; q < k; ++q) L->castev[gl[q]] = L->ce;
    L->last_ce = L->ce;
    L->ce = (L->ce + 1) % L->G;
    ++L->casts;
    L->cast_groups += k;
}

/* --ab's A: vec_bench's loop, the actions drawn on the host and the group sent at once */
static void host_step(struct neth_pipe *L, int g)
{
    for (int i = g * L->gs; i < (g + 1) * L->gs; ++i) draw(&L->hrng[i], L->b.actions + (size_t)i * NETH_NET_ACTION);
    if (L->rec) rec_group_done(L->rec, g, &L->b);
    neth_pipe_lap(L, T_LAUNCH);
    L->t_lch[g] = now_s();
    L->pq[L->np++] = g;
    send_at(L, L->np - 1);
    neth_pipe_lap(L, T_SEND);
}

void neth_pipe_mark(struct neth_pipe *L, struct neth_mark *m)
{
    neth_vec_stats(L->v, m->s);
    memcpy(m->tm, L->tm, sizeof m->tm);
    m->gsteps = L->gsteps;
    m->jobs = L->lr.jobs_all;
    m->gpu_learn = L->lr.gpu_all;
    m->t_cast = L->t_cast;
    m->t_rt = L->t_rt;
    m->casts = L->casts;
    m->cast_groups = L->cast_groups;
    m->t = now_s();
}

/* vec_bench's line, then the main thread's; env-steps/s returned */
double neth_pipe_report(struct neth_pipe *L, const struct neth_mark *a, const struct neth_mark *z, const char *what, double t_make)
{
    double wall = z->t - a->t, es = z->s[0] - a->s[0], rs = z->s[1] - a->s[1];
    printf("N %d, %d groups: %.0f env-steps/s (%.0f env-steps in %.2f s); the vector env's C %.4f ms an env-step, "
           "the workers' step %.4f ms (%.1f workers stepping on average); recv waited %.0f%% of the window; %.0f resets, "
           "%.0f episodes; made in %.2f s\n",
           L->n, L->G, es / wall, es, wall, (z->s[2] - a->s[2]) * 1e-6 / (es > 0 ? es : 1),
           (z->s[7] - a->s[7]) * 1e-6 / (es + rs > 0 ? es + rs : 1), (z->s[7] - a->s[7]) * 1e-9 / wall,
           100.0 * (z->s[3] - a->s[3]) * 1e-9 / wall, rs, z->s[5] - a->s[5], t_make);
    int64_t ng = z->gsteps - a->gsteps;
    double d[T_N], busy = 0;
    for (int q = 0; q < T_N; ++q) d[q] = z->tm[q] - a->tm[q];
    for (int q = 0; q < T_N; ++q) busy += q == T_RECV || q == T_SYNC ? 0 : d[q];
    /* vec_bench's loop (--ab's A) sends each group at once: no depth */
    int64_t nc = z->casts - a->casts;
    if (L->serve && strncmp(what, "ab A", 4))
        printf("%s, %s cast, served (%.2f groups a cast, its launch %.4f ms a group step): the main thread a group step "
               "(ms):", what, L->b.obs_device ? "device" : "host", nc ? (double)(z->cast_groups - a->cast_groups) / (double)nc : 0.0,
               (z->t_cast - a->t_cast) * 1e3 / (z->gsteps > a->gsteps ? (double)(z->gsteps - a->gsteps) : 1));
    else if (strncmp(what, "ab A vec", 8))
        printf("%s, %s cast, depth %d: the main thread a group step (ms):", what, L->b.obs_device ? "device" : "host", L->depth);
    else
        printf("%s, %s cast: the main thread a group step (ms):", what, L->b.obs_device ? "device" : "host");
    for (int q = 0; q < T_N; ++q) printf(" %s %.4f", neth_t_names[q], d[q] * 1e3 / (ng > 0 ? (double)ng : 1));
    printf("; busy %.1f%% of the window (recv waiting %.1f%%, sync %.1f%%); %lld group steps, %lld segments of %d",
           100 * busy / wall, 100 * d[T_RECV] / wall, 100 * d[T_SYNC] / wall, (long long)ng, (long long)L->segments, L->T);
    /* lane/cuda-graph: a launch to its group's send, on average (the device's part of a group's round trip) */
    printf("; launch to send %.4f ms", (z->t_rt - a->t_rt) * 1e3 / (ng > 0 ? (double)ng : 1));
    int64_t nj = z->jobs - a->jobs;
    if (nj > 0)
        printf("; %lld updates, the learner's device time %.2f ms an update (%.1f%% of the window)", (long long)nj,
               (z->gpu_learn - a->gpu_learn) * 1e3 / (double)nj, 100 * (z->gpu_learn - a->gpu_learn) / wall);
    printf("\n");
    return es / wall;
}

/* one iteration of the trainer's loop (A: vec_bench's): 0, or -1 when it cannot go on */
int neth_pipe_iterate(struct neth_pipe *L, int host)
{
    if (L->serve && !host)
    {
        int g = serve_take(L);
        if (g == -2) return 0;
        if (g < 0) return -1;
        serve_cast(L, g);
        if (L->dump_at && g == 0 && ++L->dumps == L->dump_at)
        {
            if (neth_vec_raycast_dump(L->v, g, L->dump_path)) printf("FAIL --dump-cast %s\n", L->dump_path);
            else printf("--dump-cast: group 0's cast %d to %s\n", L->dump_at, L->dump_path);
        }
        if (!L->fail) neth_pipe_launch(L, g);
        return L->fail ? -1 : 0;
    }
    int g = take(L, !host);
    if (g == -2) return 0;
    if (g < 0) return -1;
    if (host) host_step(L, g);
    else neth_pipe_launch(L, g);
    return L->fail ? -1 : 0;
}

/* ---- the lifecycle ---- */

/* the observation's options: --rec-rows and --tape (env E's rows from its group's first recv, its next episode's
 * script), --obs-dump, --zero-sc, --view-books, --obs-tone; the staged hook set where one needs it. 0, or the exit
 * code */
static int pipe_options(struct neth_pipe *L, const struct neth_runconf *c)
{
    int n = L->n;
    const char *ra[2] = {c->rows_arg, c->tape_arg};
    for (int q = 0; q < 2; ++q)
    {
        if (!ra[q]) continue;
        const char *k = strchr(ra[q], ':');
        int e = atoi(ra[q]);
        if (!k || !k[1] || e < 0 || e >= n)
        {
            printf("FAIL: %s %s: E:PATH, E an env\n", q ? "--tape" : "--rec-rows", ra[q]);
            return 2;
        }
        if (q) L->tape_env = e, L->tape_path = k + 1;
        else L->rows_env = e, L->rows_path = k + 1;
    }
    if (c->obsdump_arg)
    {
        /* --obs-dump E:DIR[:MAX] (the host cast only) */
        static char od[4096];
        const char *k = strchr(c->obsdump_arg, ':');
        int e = atoi(c->obsdump_arg);
        if (!k || !k[1] || e < 0 || e >= n || L->b.obs_device)
        {
            printf("FAIL: --obs-dump %s: E:DIR[:MAX], E an env, the cast on the host\n", c->obsdump_arg);
            return 2;
        }
        snprintf(od, sizeof od, "%s", k + 1);
        char *m = strchr(od, ':');
        L->obs_max = m ? atoll(m + 1) : INT64_MAX;
        if (m) *m = 0;
        mkdir(od, 0775);
        L->obs_env = e, L->obs_dir = od, L->obs_seq = 0;
    }
    if (c->zsc_arg)
    {
        /* --zero-sc LO:N | ore */
        int lo, k;
        if (neth_zsc_parse(c->zsc_arg, &lo, &k))
        {
            printf("FAIL: --zero-sc %s: LO:N inside the build's %d scalars (or ore, a .or build)\n", c->zsc_arg, NETH_NET_NS);
            return 2;
        }
        L->zsc_lo = lo, L->zsc_n = k;
        printf("lane/rawpix: --zero-sc: scalars %d..%d zeroed before the policy\n", lo, lo + k - 1);
    }
    if (c->vbk >= 0)
    {
        /* --view-books K */
        int lo, k;
        char p[4096];
        if (c->vbk > 2 || !c->out || neth_zsc_parse("ore", &lo, &k) || k == 0)
        {
            printf("FAIL: --view-books %d: K 0, 1 or 2, with --out, in a .or build\n", c->vbk);
            return 2;
        }
        snprintf(p, sizeof p, "%s/view.jsonl", c->out);
        if (!(L->vbf = fopen(p, "a")) || !(L->vb = calloc((size_t)n, sizeof *L->vb)))
        {
            printf("FAIL: --view-books: %s\n", p);
            return 2;
        }
        for (int i = 0; i < n; ++i) L->vb[i].first = -1;
        L->vb_col = lo + 4 * c->vbk;
    }
    if (c->tone_arg)
    {
        /* --obs-tone auto|gamma:G */
        const char *ta = c->tone_arg;
        double g = !strncmp(ta, "gamma:", 6) ? atof(ta + 6) : 0.0;
        if (!strcmp(ta, "auto")) L->tone = 1;
        else if (g > 0.0 && g <= 16.0)
        {
            uint8_t lut[256];
            for (int x = 0; x < 256; ++x) lut[x] = (uint8_t)lround(255.0 * pow(x / 255.0, 1.0 / g));
            CK(cudaMalloc((void **)&L->tlut, sizeof lut));
            CK(cudaMemcpy(L->tlut, lut, sizeof lut, cudaMemcpyHostToDevice));
            memcpy(L->hlut, lut, sizeof lut);
            L->tone = 2;
        }
        else
        {
            printf("FAIL: --obs-tone %s: auto or gamma:G (0 < G <= 16)\n", ta);
            return 2;
        }
        printf("lane/rawpix: --obs-tone %s: the observation's bytes through the tone before the policy and learner\n",
               ta);
    }
    if (L->obs_dir || L->vbf) L->staged = neth_report_staged;
    return 0;
}

/* --score-books FILE (--score's): opened, its first line the drivers of the books' mb steps (index 1 + the switch's
 * bit), the received hook set. 0, or the exit code */
static int pipe_score_books(struct neth_pipe *L, const struct neth_runconf *c)
{
    if (!c->score_books) return 0;
    if (!c->score)
    {
        printf("FAIL: --score-books is --score's\n");
        return 2;
    }
    if (!(L->sbf = fopen(c->score_books, "w")))
    {
        printf("FAIL: --score-books %s: %s\n", c->score_books, strerror(errno));
        return 2;
    }
    fputs("{\"drivers\": {\"0\": \"policy\"", L->sbf);
    int32_t bit;
    for (int i = 0; neth_vec_macro_switch(i, &bit); ++i)
        if (bit > 0 && !(bit & (bit - 1)))
            fprintf(L->sbf, ", \"%d\": \"%s\"", 1 + __builtin_ctz((unsigned)bit), neth_vec_macro_switch(i, NULL));
    fputs("}}\n", L->sbf);
    L->received = neth_report_received;
    return 0;
}

/* lane/cuda-graph: --poll-flag and --graph, once everything else is made: what each needs, or the reason it is off (the
 * run goes on without it); the flags, the slots and the scratch. 0 (a CUDA failure exits) */
static int pipe_graph_setup(struct neth_pipe *L, const struct neth_runconf *c)
{
    const char *why = NULL;
    if (c->graph)
    {
        if (!L->b.obs_device) why = "the cast on the host";
        else if (!L->dsc || !L->drew || !L->ddone || !L->ddrv || !L->dact || L->oe % 16) why = "the env's buffers not mapped";
        else if (L->dist || L->PS.launch || L->pol != &L->P || c->ab) why = "--teacher, --beta or --ab";
        else if (!L->P.launch || !neth_policy_actor_graphable(&L->P)) why = "not the actor (or the check's uniforms)";
        else if (L->tone) why = "--obs-tone";
        else if (L->psch) why = "the critic's own scalars (--zero-sc, or NETH_PSC_ALIAS 0)";
        if (why) printf("--graph 1: off (%s); the launches as before\n", why);
    }
    L->graph = why ? 0 : c->graph != 0;
    L->poll = (c->poll_flag || L->graph) && L->dact;
    if ((c->poll_flag || c->graph) && !L->dact) printf("--poll-flag: the actions' buffer is not mapped; the events instead\n");
    if (!L->poll) return 0;
    CK(cudaHostAlloc((void **)&L->done_h, (size_t)L->G * sizeof *L->done_h, cudaHostAllocMapped));
    CK(cudaHostGetDevicePointer((void **)&L->done_d, L->done_h, 0));
    memset(L->done_h, 0, (size_t)L->G * sizeof *L->done_h);
    L->done_seq = calloc((size_t)L->G, sizeof *L->done_seq);
    if (!L->graph) return 0;
    L->gstg_b = neth_gstg_bytes(L->gs);
    CK(cudaHostAlloc((void **)&L->gstg_h, (size_t)L->G * L->gstg_b, cudaHostAllocDefault));
    memset(L->gstg_h, 0, (size_t)L->G * L->gstg_b);
    CK(cudaMalloc((void **)&L->gstg_d, (size_t)L->G * L->gstg_b));
    CK(cudaMalloc((void **)&L->g_scf, (size_t)L->n * NETH_NET_NS * sizeof(float)));
    CK(cudaStreamCreateWithFlags(&L->gaux, cudaStreamNonBlocking));
    CK(cudaEventCreateWithFlags(&L->gfork, cudaEventDisableTiming));
    CK(cudaEventCreateWithFlags(&L->gjoin, cudaEventDisableTiming));
    if (!L->cast_hold)
    {
        /* the casts on side streams the graphs wait on inside (--cast-hold keeps its casts on the groups' streams and
         * cev's events, which serve_ready reads: its graphs start behind them) */
        L->gcst = calloc((size_t)L->G, sizeof *L->gcst);
        L->gcev = calloc((size_t)L->G, sizeof *L->gcev);
        for (int g = 0; g < L->G; ++g)
        {
            int pr = 0;
            CK(cudaStreamGetPriority(L->st[g], &pr));
            CK(cudaStreamCreateWithPriority(&L->gcst[g], cudaStreamNonBlocking, pr));
            CK(cudaEventCreateWithFlags(&L->gcev[g], cudaEventDisableTiming));
        }
    }
    L->gx = calloc((size_t)L->G * 4, sizeof *L->gx);
    CK(cudaMalloc((void **)&L->g_sc, (size_t)L->n * NETH_NET_NS * sizeof(neth_sc_t)));
    CK(cudaMalloc((void **)&L->g_act, (size_t)L->n * NETH_NET_ACTION * sizeof(int32_t)));
    CK(cudaMalloc((void **)&L->g_logp, (size_t)L->n * sizeof(float)));
    CK(cudaMalloc((void **)&L->g_val, (size_t)L->n * sizeof(float)));
    printf("lane/cuda-graph: --graph 1: a group step one graph launch (captured per group and weight image), "
           "done by a mapped flag; the casts %s\n", L->gcst ? "on side streams, waited on inside" : "ahead on the group's stream");
    return 0;
}

int neth_pipe_create(struct neth_pipe *L, struct neth_runconf *c, const struct neth_vec_opts *o, int argc,
                     char **argv)
{
    int n = c->n, groups = c->groups, rc;
    L->stud_level = -1;
    double t0 = now_s();
    L->v = neth_vec_make(o, L->err, sizeof L->err);
    if (L->v == NULL)
    {
        printf("FAIL N %d: %s\n", n, L->err);
        return 1;
    }
    if (c->pearl_census) neth_vec_pearl_census(L->v, 1, -1, NULL);   /* the books' privileged census */
    L->t_make = now_s() - t0;
    struct neth_vec_bufs *b = &L->b;
    neth_vec_buffers(L->v, b);
    if (neth_vec_action_width(L->v) != NETH_NET_ACTION)
    {
        printf("FAIL: the vector env's action is %d wide, net.h's %d\n", neth_vec_action_width(L->v), NETH_NET_ACTION);
        return 1;
    }
    L->n = n;
    L->G = groups;
    L->gs = n / groups;
    L->T = c->T;
    L->depth = c->depth >= 0 ? c->depth : b->obs_device && groups > 1;
    /* --serve: the default with the cast on the device unless --depth is given */
    L->serve = c->serve >= 0 ? c->serve : b->obs_device && c->depth < 0;
    if (L->serve) L->depth = groups;
    L->cast_max = c->cast_max;
    L->cast_hold = c->cast_hold;
    L->last_ce = -1;
    L->idle_ns = (long)(c->idle_us * 1e3);
    L->dump_at = c->dump_path ? c->dump_at : 0;
    L->dump_path = c->dump_path;
#if defined(__linux__)
    /* a sleep of microseconds, not the default 50 us of slack */
    if (L->idle_ns > 0) prctl(PR_SET_TIMERSLACK, 1000UL, 0, 0, 0);
#endif
    L->digest = c->digest;
    /* --score: each group's first receive (the start) and its ep_len ticks' steps, the last of them ending the
     * episode (lane/ticks-per-step: ep_len is ticks, a step b->ticks of them; ep_len + 1 steps played T episodes
     * at T ticks a step and scored the first) */
    {
        const int T = b->ticks > 1 ? b->ticks : 1;
        L->quota = c->digest ? c->warm + c->steps : c->score ? (c->ep_len + T - 1) / T + 1 : 0;
    }
    if ((rc = pipe_score_books(L, c))) return rc;
    L->clock_wrap = c->clock_wrap > 0;
    if (c->score)
    {
        L->first = malloc((size_t)n * SC_TIMED * sizeof *L->first);
        for (int i = 0; i < n * SC_TIMED; ++i) L->first[i] = -1;
    }
    L->live = L->inflight = groups;
    L->gamma = c->gamma;
    L->learn = L->learning = c->learn;
    L->saving = o->save_item != 0;
    L->wstride = L->gs / 8 > 1 ? L->gs / 8 : 1;
    neth_schedule_ladder_start(L, c);
    L->oe = (size_t)b->obs_shape[1] * (size_t)b->obs_shape[2] * (size_t)b->obs_shape[3] * (size_t)b->obs_bytes;
    if (b->obs_bytes != 1 || L->oe != NETH_NET_H * NETH_NET_W * NETH_NET_C)
    {
        printf("FAIL: the observation is %lld x %lld x %lld x %d bytes, not net.h's %d x %d RGB (a raycast camera at "
               "16x16)\n", (long long)b->obs_shape[1], (long long)b->obs_shape[2], (long long)b->obs_shape[3],
               b->obs_bytes, NETH_NET_H, NETH_NET_W);
        return 1;
    }
    if ((rc = neth_train_policy(L, c)) || (rc = neth_train_setup(L, c))) return rc;
    free(L->flat0), L->flat0 = NULL;
    free(L->tflat), L->tflat = NULL;
    if ((rc = neth_seg_create(L, c)) || (rc = neth_report_open(L, c)) || (rc = neth_eval_setup(L, c, argv[0])) ||
        (rc = pipe_options(L, c)) || (rc = neth_record_setup(L, c, o, argc, argv)) || (rc = pipe_graph_setup(L, c)))
        return rc;
    L->last_log = wall_s();
    L->last_steps = 0;
    L->tick = now_s();
    return 0;
}

/* the run: warm group steps, then steps more (or until --hours or --env-steps; --digest and --score: until every
 * group's quota), a row to iters.jsonl every --log-every seconds, --save-every's and --eval-every's checkpoints; the
 * report, --score's table, the digest */
void neth_pipe_run(struct neth_pipe *L, const struct neth_runconf *c)
{
    int groups = L->G, warm = c->warm, steps = c->steps, learn = c->learn;
    struct neth_mark m0, m1;
    memset(&m0, 0, sizeof m0);
    int until = c->hours > 0 || c->max_steps > 0;
    double t_stop = c->hours > 0 ? L->t_start + c->hours * 3600 : INFINITY;
    if (c->score) warm = 0, until = 0;
    double t_loop0 = now_s();
    for (int it = 0; !L->fail && (c->digest || c->score ? L->live > 0
                                  : until ? (c->max_steps <= 0 || L->env_steps < c->max_steps) && wall_s() < t_stop
                                          : it < (warm + steps) * groups);
         ++it)
    {
        if (it == warm * groups) neth_pipe_mark(L, &m0);
        if (neth_pipe_iterate(L, 0)) break;
        if (L->rows && !c->score && wall_s() - L->last_log >= c->log_every)
        {
            neth_report_rows(L, 0);
            if (learn && c->save_every > 0 && wall_s() - L->t_start >= L->snapk * c->save_every * 60)
            {
                snprintf(L->lr.snap, sizeof L->lr.snap, "%s/model-%03d.ntw", c->out,
                         (int)lround(L->snapk * c->save_every));
                ++L->snapk;
            }
            if (L->ckev) neth_eval_tick(L, c->out);
        }
        neth_pipe_lap(L, T_TAIL);
    }
    if (until && L->gsteps < (int64_t)warm * groups) neth_pipe_mark(L, &m0);
    neth_pipe_drain(L);
    if (learn) neth_train_drain(L);
    neth_pipe_mark(L, &m1);
    if (L->fail) return;
    neth_pipe_report(L, &m0, &m1, learn ? "the trainer" : "ntloop", L->t_make);
    if (c->score) neth_report_score_run(L, c, now_s() - t_loop0);
    if (c->digest) neth_report_digest(L);
}

/* the run's end: every launched group sent, the learner's queue trained, the books, the recorder closed, --out's
 * files and the model */
void neth_pipe_finish(struct neth_pipe *L, const struct neth_runconf *c)
{
    neth_pipe_drain(L);
    if (L->sbf) fclose(L->sbf), L->sbf = NULL;
    if (c->learn) neth_train_drain(L);
    neth_schedule_report(L);
    neth_distill_report(L);
    neth_record_finish(L);
    neth_report_finish(L, c);
}

/* everything create made, in any state create left (a failed create included) */
void neth_pipe_destroy(struct neth_pipe *L)
{
    fflush(stdout);
    if (!L->v) return;
    /* the launched groups finish before their buffers go */
    CK(cudaDeviceSynchronize());
    if (L->sbf) fclose(L->sbf);
    if (L->rows) fclose(L->rows);
    if (L->eprows) fclose(L->eprows);
    if (L->dist_rows) fclose(L->dist_rows);
    if (L->vbf) fclose(L->vbf);
    if (L->ckev && L->ckev->log) fclose(L->ckev->log);
    if (L->rec) neth_record_finish(L);
    for (int g = 0; L->gx && g < 4 * L->G; ++g)
        if (L->gx[g]) cudaGraphExecDestroy(L->gx[g]);
    free(L->gx), free(L->done_seq), free(L->phash), free(L->t_lch);
    if (L->done_h) cudaFreeHost(L->done_h);
    if (L->gstg_h) cudaFreeHost(L->gstg_h);
    if (L->gaux) cudaStreamDestroy(L->gaux);
    if (L->gfork) cudaEventDestroy(L->gfork), cudaEventDestroy(L->gjoin);
    for (int g = 0; L->gcst && g < L->G; ++g) cudaStreamDestroy(L->gcst[g]), cudaEventDestroy(L->gcev[g]);
    free(L->gcst), free(L->gcev);
    cudaFree(L->gstg_d), cudaFree(L->g_scf), cudaFree(L->g_sc), cudaFree(L->g_act), cudaFree(L->g_logp), cudaFree(L->g_val);
    for (int g = 0; L->sg && g < 2 * L->G; ++g) neth_seg_free(&L->sg[g]);
    cudaFree(L->hst);
    cudaFree(L->tlut);
    if (L->psch) cudaFreeHost(L->psch);
    if (L->sch) cudaFreeHost(L->sch);
    for (int g = 0; g < L->G; ++g)
    {
        if (L->st) cudaStreamDestroy(L->st[g]);
        if (L->ev) cudaEventDestroy(L->ev[g]);
        if (L->cev) cudaEventDestroy(L->cev[g]);
    }
    if (L->P.release) L->P.release(L->P.ctx);
    if (L->PS.release) L->PS.release(L->PS.ctx);
    neth_distill_free(L->dist);
    if (L->PA.release) L->PA.release(L->PA.ctx);
    neth_train_free(&L->lr);
    const struct neth_vec_bufs *b = &L->b;
    if (L->pinned)
    {
        if (!b->obs_device) cudaHostUnregister(b->obs);
        cudaHostUnregister(b->scalars);
        cudaHostUnregister(b->actions);
        cudaHostUnregister(b->rewards);
        cudaHostUnregister(b->truncations);
        cudaHostUnregister(b->driver);
    }
    neth_vec_free(L->v);
    L->v = NULL;
    free(L->st), free(L->ev), free(L->sg), free(L->cur), free(L->tg), free(L->ret_run), free(L->hash), free(L->hrng);
    free(L->seen), free(L->gcount), free(L->pq), free(L->rq), free(L->castev), free(L->cev), free(L->eps);
    free(L->fhash), free(L->fhost), free(L->stash), free(L->eptmp), free(L->pend);
    free(L->stash_rung), free(L->eprung), free(L->epfid), free(L->first), free(L->vb), free(L->flat0), free(L->tflat);
    if (L->lad) free(L->lad->cur), free(L->lad->pend), free(L->lad->ended), free(L->lad->mring);
}
