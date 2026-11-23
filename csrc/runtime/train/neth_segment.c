/* ntloop's segments: a group's T steps on the device as the learner reads them, and the vector env's buffers
 * page-locked for the copies. */
#define _GNU_SOURCE
#include "neth_pipe_internal.h"
#include "neth_runconf.h"

void neth_seg_alloc(struct neth_seg *s, int T, int gs, size_t oe)
{
    CK(cudaMalloc((void **)&s->obs, (size_t)T * gs * oe));
    CK(cudaMalloc((void **)&s->sc, (size_t)T * gs * NETH_NET_NS * sizeof(neth_sc_t)));
    CK(cudaMalloc((void **)&s->act, (size_t)T * gs * NETH_NET_ACTION * sizeof(int32_t)));
    CK(cudaMalloc((void **)&s->logp, (size_t)T * gs * sizeof(float)));
    CK(cudaMalloc((void **)&s->val, (size_t)T * gs * sizeof(float)));
    CK(cudaMalloc((void **)&s->rew, (size_t)T * gs * sizeof(float)));
    CK(cudaMalloc((void **)&s->done, (size_t)T * gs));
    CK(cudaMalloc((void **)&s->drv, (size_t)T * gs));
    CK(cudaMalloc((void **)&s->boot, (size_t)gs * sizeof(float)));
    s->h0 = NULL;
    if (NETH_NET_RNN) CK(cudaMalloc((void **)&s->h0, (size_t)gs * NETH_NET_D * sizeof(float)));
    s->psc = NULL;  /* made by --critic (crit_alloc) */
    CK(cudaEventCreateWithFlags(&s->ready, cudaEventDisableTiming));
    CK(cudaEventCreateWithFlags(&s->freed, cudaEventDisableTiming));
    s->job = 0;
    /* the no-op policy's actions: the idle action (the centre bins, no key) */
    int32_t *idle = calloc((size_t)T * gs * NETH_NET_ACTION, sizeof *idle);
    for (size_t i = 0; i < (size_t)T * gs; ++i) idle[i * NETH_NET_ACTION] = idle[i * NETH_NET_ACTION + 1] = NETH_VEC_CAM / 2;
    CK(cudaMemcpy(s->act, idle, (size_t)T * gs * NETH_NET_ACTION * sizeof *idle, cudaMemcpyHostToDevice));
    free(idle);
}

void neth_seg_free(struct neth_seg *s)
{
    cudaFree(s->obs);
    cudaFree(s->sc);
    cudaFree(s->act);
    cudaFree(s->logp);
    cudaFree(s->val);
    cudaFree(s->rew);
    cudaFree(s->done);
    cudaFree(s->drv);
    cudaFree(s->boot);
    cudaFree(s->h0);
    if (s->psc && s->psc != s->sc) cudaFree(s->psc);
    cudaEventDestroy(s->ready);
    cudaEventDestroy(s->freed);
}

/* page-lock a buffer of the vector env's (already locked is fine), mapped: the copies go without staging, and
 * --stage's kernel reads it through the device pointer returned (NULL: none) */
void *neth_pin(void *p, size_t n)
{
    if (p == NULL || n == 0) return NULL;
    cudaError_t e = cudaHostRegister(p, n, cudaHostRegisterMapped);
    if (e == cudaErrorHostMemoryAlreadyRegistered) (void)cudaGetLastError();
    else CK(e);
    void *d = NULL;
    if (cudaHostGetDevicePointer(&d, p, 0) != cudaSuccess)
    {
        (void)cudaGetLastError();
        d = NULL;
    }
    return d;
}

/* the pipe's device side and books, once the env and the policy are made: the env's buffers page-locked (the copies'
 * kernel reads them mapped), each group's stream (above the learner's with --prio), event and two segments, the
 * recurrent state, and every per-group and per-env array the hot path reads; 0 (a CUDA failure exits) */
int neth_seg_create(struct neth_pipe *L, const struct neth_runconf *c)
{
    struct neth_vec_bufs *b = &L->b;
    int n = L->n, groups = L->G, T = L->T;
    L->dsc = neth_pin(b->scalars, (size_t)n * NETH_NET_NS * sizeof(float));
    L->dact = neth_pin(b->actions, (size_t)n * NETH_NET_ACTION * sizeof(int32_t));
    L->drew = neth_pin(b->rewards, (size_t)n * sizeof(float));
    L->ddone = neth_pin(b->truncations, (size_t)n);
    L->ddrv = neth_pin(b->driver, (size_t)n);
    if (!b->obs_device) L->dobs = neth_pin(b->obs, (size_t)n * L->oe);
    L->pinned = 1;
    L->stage = c->stage && L->dsc && L->drew && L->ddone && L->ddrv && (b->obs_device || (L->dobs && L->oe % 16 == 0));
    if (c->stage && !L->stage) printf("--stage: the env's buffers are not mapped; the copies instead\n");
    L->st = calloc((size_t)groups, sizeof *L->st);
    L->ev = calloc((size_t)groups, sizeof *L->ev);
    L->sg = calloc((size_t)groups * 2, sizeof *L->sg);
    /* --prio 1 (the default served): the groups' streams (the cast, the actor, the copies the env waits for) ahead of
     * the learner's at the device's block scheduler: the 64x64 trainer (s1, 512x4) 119.5k and 117.6k env-steps/s at
     * 0, 128.6k and 129.3k at 1, the learner then busy the whole window but keeping up */
    int prio = c->prio < 0 ? L->serve : c->prio;
    int prio_lo = 0, prio_hi = 0;
    CK(cudaDeviceGetStreamPriorityRange(&prio_lo, &prio_hi));
    for (int g = 0; g < groups; ++g)
    {
        CK(cudaStreamCreateWithPriority(&L->st[g], cudaStreamNonBlocking, prio ? prio_hi : prio_lo));
        CK(cudaEventCreateWithFlags(&L->ev[g], cudaEventDisableTiming));
        neth_seg_alloc(&L->sg[2 * g], T, L->gs, L->oe);
        neth_seg_alloc(&L->sg[2 * g + 1], T, L->gs, L->oe);
        for (int k = 0; L->lr.crit && k < 2; ++k)     /* --critic's scalars */
        {
            if (L->psch)
                CK(cudaMalloc((void **)&L->sg[2 * g + k].psc, (size_t)T * L->gs * NETH_NET_NS * sizeof(neth_sc_t)));
            else
                L->sg[2 * g + k].psc = L->sg[2 * g + k].sc;
        }
    }
#if NETH_SC_BF16
    CK(cudaMallocHost((void **)&L->sch, (size_t)n * NETH_NET_NS * sizeof(neth_sc_t)));
#endif
    if (NETH_NET_RNN)
    {
        /* every env's state, zero before its first step */
        CK(cudaMalloc((void **)&L->hst, (size_t)n * NETH_NET_D * sizeof(float)));
        CK(cudaMemset(L->hst, 0, (size_t)n * NETH_NET_D * sizeof(float)));
    }
    L->cur = calloc((size_t)groups, sizeof *L->cur);
    L->tg = calloc((size_t)groups, sizeof *L->tg);
    L->ret_run = calloc((size_t)n, sizeof *L->ret_run);
    L->hash = malloc((size_t)n * sizeof *L->hash);
    L->hrng = malloc((size_t)n * sizeof *L->hrng);
    L->seen = calloc((size_t)n, 1);
    if (c->fdigest)
    {
        L->fhash = malloc((size_t)n * sizeof *L->fhash);
        L->fhost = malloc((size_t)L->gs * (L->oe + NETH_NET_NS * sizeof(float) + sizeof(float) + 2));
        for (int i = 0; i < n; ++i) L->fhash[i] = 0xcbf29ce484222325ull;
        L->phash = malloc((size_t)n * sizeof *L->phash);
        for (int i = 0; i < n; ++i) L->phash[i] = 0xcbf29ce484222325ull;
    }
    for (int i = 0; i < n; ++i)
    {
        L->hash[i] = 0xcbf29ce484222325ull;
        L->hrng[i] = (c->seed * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull);
    }
    L->gcount = calloc((size_t)groups, sizeof *L->gcount);
    L->pq = calloc((size_t)groups, sizeof *L->pq);
    L->t_lch = calloc((size_t)groups, sizeof *L->t_lch);
    L->rq = calloc((size_t)groups, sizeof *L->rq);
    L->castev = malloc((size_t)groups * sizeof *L->castev);
    L->cev = calloc((size_t)groups, sizeof *L->cev);
    for (int g = 0; g < groups; ++g)
    {
        L->castev[g] = -1;
        CK(cudaEventCreateWithFlags(&L->cev[g], cudaEventDisableTiming));
    }
    return 0;
}
