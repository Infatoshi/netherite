/* ntloop's learning stage: the policy (the actor from --init or the teacher, the stand-in, the no-op), the learner's
 * buffers and recipe, its queue of full segments, one update in flight on its own stream, the actor's other image
 * refreshed after each update. */
#define _GNU_SOURCE
#include "neth_pipe_internal.h"
#include "neth_runconf.h"

/* the policy launches use: the actor (--init's weights, or the teacher's widened to the build's net with the
 * learner the student), or the stand-in or the no-op; 0, or the exit code */
int neth_train_policy(struct neth_pipe *L, const struct neth_runconf *c)
{
    char err[1024];
    if (!strcmp(c->pol, "actor"))
    {
        L->flat0 = malloc(NETH_LEARN_NPARAM * sizeof(float));
        if (neth_ntw_load(c->init, L->flat0, err, sizeof err))
        {
            printf("FAIL --init %s\n", err);
            return 1;
        }
        size_t blen;
        unsigned char *blob;
        if (c->teacher)
        {
            /* the actor is the teacher (widened to the build's net), the learner the student from --init */
            size_t tl;
            unsigned char *tb = slurp(c->teacher, &tl);
            int got = tb != NULL;
            blob = got ? neth_distill_widen(tb, tl, &blen, err, sizeof err) : NULL;
            free(tb);
            if (!blob)
            {
                printf("FAIL --teacher %s: %s\n", c->teacher, got ? err : "cannot read");
                return 1;
            }
            /* --soft's copy of the teacher's weights in the learner's layout */
            L->tflat = malloc(NETH_LEARN_NPARAM * sizeof(float));
            if (neth_ntw_flat(blob, blen, L->tflat, err, sizeof err))
            {
                printf("FAIL --teacher %s: %s\n", c->teacher, err);
                free(blob);
                return 1;
            }
        }
        else
            blob = neth_ntw_blob(L->flat0, &blen);
        int r = neth_policy_actor(&L->P, blob, blen, c->groups, L->gs, c->prec == NETH_LEARN_BF16, c->seed, err, sizeof err);
        free(blob);
        if (r)
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
    }
    else if ((!strcmp(c->pol, "standin")       ? neth_policy_standin(&L->P, c->n, c->seed)
              : !strcmp(c->pol, "standin-gui") ? neth_policy_standin_gui(&L->P, c->n, c->seed)
              : !strcmp(c->pol, "noop")        ? neth_policy_noop(&L->P) : -1))
    {
        printf("FAIL: --policy %s (standin, noop or actor)\n", c->pol);
        return 2;
    }
    L->pol = &L->P;
    if (c->ab && !strcmp(c->ab_a, "loop") && neth_policy_standin(&L->PA, c->n, c->seed))
    {
        printf("FAIL: --ab-a loop's stand-in\n");
        return 1;
    }
    return 0;
}

/* the learner (with --learn): the recipe, its buffers from --init's weights, --ref-kl's frozen start, the privileged
 * critic, the actor's first image from the learner's weights, the student (--teacher), --init's Adam state; 0, or the
 * exit code */
int neth_train_setup(struct neth_pipe *L, const struct neth_runconf *c)
{
    if (!c->learn) return 0;
    struct neth_learner *R = &L->lr;
    R->h = c->hy;
    R->entc = c->ent;
    R->lr = c->lr;
    R->epochs = c->epochs;
    R->mb = c->mb;
    R->rng = (c->seed * 0x9e3779b97f4a7c15ull) ^ 0x2545f4914f6cdd1dull;
    /* ppo.mb past a segment (T x gs samples): kseg segments a batch, one update over them (lane/xhigh-patch; before
     * it the minibatch was clamped to the segment without a word) */
    int n1 = c->T * L->gs;
    R->kseg = 1;
    if (c->mb > n1)
    {
        int k = c->mb / n1;
        if (k > (int)(sizeof R->bt.scale / sizeof R->bt.scale[0])) k = (int)(sizeof R->bt.scale / sizeof R->bt.scale[0]);
        if (NETH_NET_RNN || c->teacher || R->perm_fix)
            printf("ntloop: ppo.mb %d is past a segment's %d samples (T %d x a group of %d); the %s trains a segment at "
                   "a time, so its minibatches are %d\n", c->mb, n1, c->T, L->gs,
                   NETH_NET_RNN ? "recurrent net" : c->teacher ? "distillation" : "check", n1);
        else
        {
            R->kseg = k;
            printf("ntloop: ppo.mb %d: %d segments of %d samples (T %d x a group of %d) a batch of %d, one update over "
                   "them, minibatches of %d%s\n", c->mb, k, n1, c->T, L->gs, k * n1, c->mb < k * n1 ? c->mb : k * n1,
                   c->mb % n1 ? " (ppo.mb not a multiple of the segment: the batch rounded down)" : "");
        }
    }
    neth_train_init(L, R->kseg * n1, c->prec);
    CK(cudaMemcpy(R->w, L->flat0, NETH_LEARN_NPARAM * sizeof(float), cudaMemcpyHostToDevice));
    R->lr0 = c->lr, R->refc0 = c->ref_kl, R->refc_min = c->ref_kl_min, R->ref_lin = c->ref_kl > 0 && !strcmp(c->ref_sched, "linear");
    R->anneal = c->anneal, R->vwarm = c->vwarm, R->t0 = now_s(), R->hours = c->hours, R->max_steps = c->max_steps;
    R->vlr = c->vwarm_lr;
    R->h.ref_kl = (float)c->ref_kl;
    if (c->ref_kl > 0)
    {   /* the start's weights, frozen: the reference policy */
        CK(cudaMalloc((void **)&R->wref, NETH_LEARN_NPARAM * sizeof(float)));
        CK(cudaMemcpy(R->wref, L->flat0, NETH_LEARN_NPARAM * sizeof(float), cudaMemcpyHostToDevice));
        if (neth_learn_set_ref(R->ln, R->wref)) return 1;
    }
    /* the privileged critic, --kl-stop, --vclip */
    R->h.kl_stop = (float)c->kl_stop;
    R->h.vclip = (float)c->vclip;
    if (c->critic_h)
    {
        if (c->teacher || strcmp(c->pol, "actor"))
        {
            printf("FAIL: --critic: PPO's own actor only (--policy actor, no --teacher)\n");
            return 2;
        }
        int nmax = R->N;
        R->crit = neth_critic_new(c->critic_h, R->mb < nmax ? R->mb : nmax, c->seed);
        if (!R->crit) return 1;
        neth_critic_set_mode(R->crit, c->prec == NETH_LEARN_BF16);
        if (c->critic_init && neth_critic_load(R->crit, c->critic_init))
        {
            printf("FAIL: --critic-init %s: not a critic of width %d over %d scalars\n", c->critic_init, c->critic_h,
                   NETH_NET_NS);
            return 1;
        }
#if NETH_PSC_ALIAS
        if (c->zsc_arg)
            CK(cudaMallocHost((void **)&L->psch, (size_t)c->n * NETH_NET_NS * sizeof(neth_sc_t)));
#else
        CK(cudaMallocHost((void **)&L->psch, (size_t)c->n * NETH_NET_NS * sizeof(neth_sc_t)));  /* the segments': below */
#endif
        printf("ntloop: lane/acrl: the privileged critic, %d wide over the %d scalars before --zero-sc, %d "
               "parameters%s%s\n", c->critic_h, NETH_NET_NS, neth_critic_params(R->crit), c->critic_init ? ", from " : "",
               c->critic_init ? c->critic_init : "");
    }
    if (c->kl_stop > 0 || c->vclip > 0)
        printf("ntloop: lane/acrl: kl_stop %g (an update's epochs stopped at the first minibatch past it), vclip %g\n",
               c->kl_stop, c->vclip);
    /* the actor's image from the learner's weights once, so the image in use always has its source (actor_gap) */
    if (!c->teacher && (neth_policy_actor_refresh(&L->P, R->w, R->s) || cudaStreamSynchronize(R->s) != cudaSuccess))
    {
        printf("FAIL: the actor's first refresh\n");
        return 1;
    }
    if (!c->teacher) neth_policy_actor_switch(&L->P);
    int rc = c->teacher ? neth_distill_setup(L, c) : 0;
    if (rc) return rc;
    char err[1024];
    int o2 = c->fresh_opt ? 0 : neth_checkpoint_load_opt(R, c->init, err, sizeof err);
    if (o2 < 0)
    {
        printf("FAIL --init's Adam state: %s\n", err);
        return 1;
    }
    printf("ntloop: the trainer: %s's weights%s, %s, T %d, a group of %d, %d segment%s an update (%d samples), %d epochs "
           "of minibatches of %d; lr %g ent %g clip %g gamma %g lam %g vf %g\n", c->init, o2 ? " and Adam state" : ", Adam afresh",
           c->prec == NETH_LEARN_BF16 ? NETH_NET_P16NAME : "fp32", c->T, L->gs, R->kseg, R->kseg > 1 ? "s" : "", R->N,
           c->epochs, c->mb < R->N ? c->mb : R->N, c->lr, c->ent, c->hy.clip, c->gamma, c->hy.lam, c->hy.vf);
    printf("ntloop: the reward: log cap %d, milestone weights %s, values %s, caps %s, nether %g; starts saved %s\n",
           c->log_cap, c->ms_weights ? c->ms_weights : "all 1", c->values_arg ? c->values_arg : "C's",
           c->caps_arg ? c->caps_arg : "none", c->nether_w, c->save ? c->save : "off");
    if (c->end_w != 0 || c->dragon_w != 0 || c->dragon_hp_w != 0 || c->crystal_w != 0)
        printf("ntloop: lane/dragon: the End %g, the dragon %g, dragon_hp %g, crystals %g\n",
               c->end_w, c->dragon_w, c->dragon_hp_w, c->crystal_w);
    if (c->ref_kl > 0 || c->vwarm || c->anneal)
        printf("ntloop: lane/ntppo: ref_kl %g (%s, min %g, the start's weights frozen), value warm-up %d updates (lr %g), "
               "anneal %s\n", c->ref_kl, c->ref_sched, c->ref_kl_min, c->vwarm, c->vwarm_lr > 0 ? c->vwarm_lr : c->lr,
               c->anneal ? "on (the lr to 0 over the run)" : "off");
    return 0;
}

/* fastppo's scale(): the running return's std, at least 1, 1 until 100 samples */
double neth_train_reward_scale(const struct neth_pipe *L)
{
    double sd = L->w_n > 100 ? sqrt(L->w_m2 / (L->w_n - 1 > 1 ? L->w_n - 1 : 1)) : 1.0;
    return sd > 1.0 ? sd : 1.0;
}

void neth_train_init(struct neth_pipe *L, int nmax, int prec)
{
    struct neth_learner *R = &L->lr;
    size_t P = NETH_LEARN_NPARAM * sizeof(float);
    if (R->kseg < 1) R->kseg = 1;   /* a learner made without neth_train_setup (--check-learn): a segment an update */
    /* NETH_NET_RNN: a minibatch is whole env columns, mb / T envs (fastppo's be = max(1, mb // T)) */
    if (NETH_NET_RNN) R->mb = (R->mb / L->T > 0 ? R->mb / L->T : 1) * L->T;
    CK(cudaStreamCreateWithFlags(&R->s, cudaStreamNonBlocking));
    R->ln = neth_learn_new(nmax, R->mb < nmax ? R->mb : nmax, prec, R->s);
    if (!R->ln) exit(1);
    R->N = nmax;
    CK(cudaMalloc((void **)&R->w, P));
    CK(cudaMalloc((void **)&R->grad, P));
    CK(cudaMalloc((void **)&R->m, P));
    CK(cudaMalloc((void **)&R->v, P));
    CK(cudaMalloc((void **)&R->acc, (NETH_LEARN_NBOOK + 1) * sizeof(float)));
    R->gap = R->acc + NETH_LEARN_NBOOK;
    CK(cudaMemset(R->m, 0, P));
    CK(cudaMemset(R->v, 0, P));
    CK(cudaMemset(R->acc, 0, (NETH_LEARN_NBOOK + 1) * sizeof(float)));
    CK(cudaMalloc((void **)&R->cam, (size_t)nmax * NETH_NET_CAM * sizeof(int32_t)));
    CK(cudaMalloc((void **)&R->keys, (size_t)nmax * NETH_NET_KEYS * sizeof(float)));
    if (NETH_NET_HOTBAR) CK(cudaMalloc((void **)&R->hot, (size_t)nmax * sizeof(int32_t)));
    CK(cudaMalloc((void **)&R->perm, (size_t)R->epochs * nmax * sizeof(int32_t)));
    CK(cudaMallocHost((void **)&R->hperm, (size_t)R->epochs * nmax * sizeof(int32_t)));
    CK(cudaMallocHost((void **)&R->hacc, (NETH_LEARN_NBOOK + 1) * sizeof(float)));
    CK(cudaEventCreate(&R->e0));
    CK(cudaEventCreate(&R->e1));
    R->q = calloc((size_t)2 * L->G + 1, sizeof *R->q);
    if (R->kseg > 1)
    {   /* the batch the segments are copied into ([kseg][T][gs]; --critic's scalars made at the first copy) */
        size_t K = (size_t)R->kseg, n1 = (size_t)L->T * L->gs;
        CK(cudaMalloc((void **)&R->bt.obs, K * n1 * L->oe));
        CK(cudaMalloc((void **)&R->bt.sc, K * n1 * NETH_NET_NS * sizeof(neth_sc_t)));
        CK(cudaMalloc((void **)&R->bt.logp, K * n1 * sizeof(float)));
        CK(cudaMalloc((void **)&R->bt.val, K * n1 * sizeof(float)));
        CK(cudaMalloc((void **)&R->bt.rew, K * n1 * sizeof(float)));
        CK(cudaMalloc((void **)&R->bt.done, K * n1));
        CK(cudaMalloc((void **)&R->bt.drv, K * n1));
        CK(cudaMalloc((void **)&R->bt.boot, K * (size_t)L->gs * sizeof(float)));
    }
}

void neth_train_free(struct neth_learner *R)
{
    if (!R->ln) return;
    neth_learn_free(R->ln);
    cudaFree(R->w), cudaFree(R->grad), cudaFree(R->m), cudaFree(R->v), cudaFree(R->acc), cudaFree(R->cam);
    cudaFree(R->keys), cudaFree(R->hot), cudaFree(R->perm), cudaFreeHost(R->hperm), cudaFreeHost(R->hacc);
    if (R->wref) cudaFree(R->wref);
    if (R->kseg > 1)
    {
        if (R->bt.psc != R->bt.sc) cudaFree(R->bt.psc);
        cudaFree(R->bt.obs), cudaFree(R->bt.sc), cudaFree(R->bt.logp), cudaFree(R->bt.val), cudaFree(R->bt.rew);
        cudaFree(R->bt.done), cudaFree(R->bt.drv), cudaFree(R->bt.boot);
    }
    cudaEventDestroy(R->e0), cudaEventDestroy(R->e1), cudaStreamDestroy(R->s);
    free(R->q);
}

/* kseg > 1: segment S copied into the batch's slot kfill on the learner's stream (after any update in flight there,
 * which reads the batch), its actions split into the learner's camera, keys and hotbar at that slot */
static void learn_collect(struct neth_pipe *L, struct neth_seg *S)
{
    struct neth_learner *R = &L->lr;
    double t0 = now_s();
    size_t n1 = (size_t)L->T * L->gs, o = (size_t)R->kfill * n1;
    cudaStream_t s = R->s;
    CK(cudaStreamWaitEvent(s, S->ready, 0));
    CK(cudaMemcpyAsync(R->bt.obs + o * L->oe, S->obs, n1 * L->oe, cudaMemcpyDeviceToDevice, s));
    CK(cudaMemcpyAsync(R->bt.sc + o * NETH_NET_NS, S->sc, n1 * NETH_NET_NS * sizeof(neth_sc_t), cudaMemcpyDeviceToDevice, s));
    CK(cudaMemcpyAsync(R->bt.logp + o, S->logp, n1 * sizeof(float), cudaMemcpyDeviceToDevice, s));
    CK(cudaMemcpyAsync(R->bt.val + o, S->val, n1 * sizeof(float), cudaMemcpyDeviceToDevice, s));
    CK(cudaMemcpyAsync(R->bt.rew + o, S->rew, n1 * sizeof(float), cudaMemcpyDeviceToDevice, s));
    CK(cudaMemcpyAsync(R->bt.done + o, S->done, n1, cudaMemcpyDeviceToDevice, s));
    CK(cudaMemcpyAsync(R->bt.drv + o, S->drv, n1, cudaMemcpyDeviceToDevice, s));
    CK(cudaMemcpyAsync(R->bt.boot + (size_t)R->kfill * L->gs, S->boot, (size_t)L->gs * sizeof(float),
                       cudaMemcpyDeviceToDevice, s));
    if (R->crit && S->psc)
    {
        if (S->psc == S->sc) R->bt.psc = R->bt.sc;
        else
        {
            if (R->bt.psc == NULL || R->bt.psc == R->bt.sc)
                CK(cudaMalloc((void **)&R->bt.psc, (size_t)R->kseg * n1 * NETH_NET_NS * sizeof(neth_sc_t)));
            CK(cudaMemcpyAsync(R->bt.psc + o * NETH_NET_NS, S->psc, n1 * NETH_NET_NS * sizeof(neth_sc_t),
                               cudaMemcpyDeviceToDevice, s));
        }
    }
    neth_split_actions(S->act, (int)n1, R->cam + o * NETH_NET_CAM, R->keys + o * NETH_NET_KEYS, R->hot ? R->hot + o : NULL, s);
    R->bt.scale[R->kfill++] = S->scale;
    R->t_learn += now_s() - t0;
}

/* segment S's update on the learner's stream (none in flight): GAE, the epochs in a fresh order (torch.randperm's
 * role: a seeded xorshift's Fisher-Yates), the Adam steps, the books into acc; then the actor's other image from the
 * new weights, after every group's launches so far; S freed for its group's next fill. kseg > 1: S the batch's last
 * segment, copied in first, and the update over the whole batch */
static void learn_enqueue(struct neth_pipe *L, struct neth_seg *S)
{
    struct neth_learner *R = &L->lr;
    if (R->kseg > 1) learn_collect(L, S);
    double t0 = now_s();
    int N = R->N;
    const int32_t *perm = R->perm_fix;
    if (!perm)
    {
        /* NETH_NET_RNN: the envs shuffled, not the samples, each minibatch whole env columns (fastppo's
         * Learner over a recurrent net: mb / T envs a minibatch), in neth_learn_update's column order */
        int M = NETH_NET_RNN ? L->gs : N;
        int32_t *envs = NETH_NET_RNN ? malloc((size_t)M * sizeof *envs) : NULL;
        for (int e = 0; e < R->epochs; ++e)
        {
            int32_t *p = NETH_NET_RNN ? envs : R->hperm + (size_t)e * N;
            for (int i = 0; i < M; ++i) p[i] = i;
            for (int i = M - 1; i > 0; --i)
            {
                int j = (int)(xs(&R->rng) % (uint64_t)(i + 1)), x = p[i];
                p[i] = p[j];
                p[j] = x;
            }
            if (NETH_NET_RNN) neth_learn_cols(p, L->T, L->gs, R->mb / L->T, R->hperm + (size_t)e * N);
        }
        free(envs);
        CK(cudaMemcpyAsync(R->perm, R->hperm, (size_t)R->epochs * N * sizeof(int32_t), cudaMemcpyHostToDevice, R->s));
        perm = R->perm;
    }
    if (R->kseg == 1)
    {
        CK(cudaStreamWaitEvent(R->s, S->ready, 0));
        if (!L->dist) neth_split_actions(S->act, N, R->cam, R->keys, R->hot, R->s);
    }
#if NETH_NET_RNN
    neth_learn_seq(R->ln, L->T, S->done, S->h0);  /* the scan starts at the segment's stored states */
#endif
    /* the schedules from the run's share done, the warm-up's updates first (jobs_all: updates enqueued
     * before this one, as fastppo sets its lr after each segment's update for the next) */
    int vonly = R->jobs_all < R->vwarm;
    if (R->anneal || R->ref_lin)
    {
        double fs = R->max_steps > 0 ? (double)L->env_steps / R->max_steps : 0;
        double ft = R->hours > 0 ? (now_s() - R->t0) / (R->hours * 3600) : 0;
        double rem = 1.0 - (fs > ft ? fs : ft);
        if (rem < 0) rem = 0;
        if (R->anneal) R->lr = R->lr0 * rem, R->h.lr = (float)R->lr;
        if (R->ref_lin) { double rc = R->refc0 * rem; if (rc < R->refc_min) rc = R->refc_min; R->h.ref_kl = (float)rc; }
    }
    if (R->h.vonly && !vonly && R->step == 0)
    {   /* the warm-up's end with Adam afresh: the value head's moments too, so its first joint steps are Adam's own
         * first steps (bias correction at step 1 over moments of many steps would multiply them by 10) */
        CK(cudaMemsetAsync(R->m + NETH_LEARN_OFF(v_w), 0, (NETH_LEARN_OFF(v_b) + 1 - NETH_LEARN_OFF(v_w)) * sizeof(float), R->s));
        CK(cudaMemsetAsync(R->v + NETH_LEARN_OFF(v_w), 0, (NETH_LEARN_OFF(v_b) + 1 - NETH_LEARN_OFF(v_w)) * sizeof(float), R->s));
    }
    R->h.vonly = vonly;
    float lr_now = R->h.lr;
    if (vonly && R->vlr > 0) R->h.lr = (float)R->vlr;     /* --value-warmup-lr: the head alone, its own rate */
    CK(cudaEventRecord(R->e0, R->s));
    if (L->dist)    /* the student's update on the segment's labels; the actor stays the teacher */
        neth_distill_learn(L->dist, R->ln, &R->h, R->epochs, R->mb, perm, (int)(S - L->sg), S->obs, S->sc, S->val, R->w,
                  R->grad, R->m, R->v, &R->step, R->s);
    else if (R->kseg == 1)
    {
        if (R->crit) neth_learn_set_critic(R->ln, R->crit, S->psc);
        neth_learn_update(R->ln, &R->h, R->entc, L->T, L->gs, S->scale, R->epochs, R->mb, perm, S->obs, S->sc, R->cam,
                   R->keys, R->hot, S->logp, S->val, S->rew, S->done, S->drv, S->boot, R->w, R->grad, R->m, R->v,
                   vonly ? &R->vstep : &R->step, R->acc);
    }
    else
    {
        if (R->crit) neth_learn_set_critic(R->ln, R->crit, R->bt.psc);
        neth_learn_update_k(R->ln, &R->h, R->entc, L->T, L->gs, R->kseg, R->bt.scale, R->epochs, R->mb, perm, R->bt.obs,
                   R->bt.sc, R->cam, R->keys, R->hot, R->bt.logp, R->bt.val, R->bt.rew, R->bt.done, R->bt.drv, R->bt.boot,
                   R->w, R->grad, R->m, R->v, vonly ? &R->vstep : &R->step, R->acc);
        R->kfill = 0;
    }
    R->h.lr = lr_now;
    CK(cudaEventRecord(R->e1, R->s));
    for (int g = 0; g < L->G; ++g) CK(cudaStreamWaitEvent(R->s, L->ev[g], 0));
    /* the actor that learns: the trainer's, or --beta's student (the teacher never) */
    struct neth_policy *learnt = !L->dist ? &L->P : L->PS.launch ? &L->PS : NULL;
    if (learnt && neth_policy_actor_refresh(learnt, R->w, R->s))
    {
        printf("FAIL: the actor's refresh: %s\n", cudaGetErrorString(cudaGetLastError()));
        L->fail = 1;
    }
    if (R->crit) neth_critic_refresh(R->crit, R->s);   /* the critic's actor image, switched with the actor's */
    CK(cudaEventRecord(S->freed, R->s));
    S->job = 2;
    R->busy = S;
    R->t_learn += now_s() - t0;
}

/* the update in flight is done on the device: the actor takes its weights from the next launch; --save-every's */
void neth_train_done(struct neth_pipe *L)
{
    struct neth_learner *R = &L->lr;
    float ms = 0;
    CK(cudaEventElapsedTime(&ms, R->e0, R->e1));
    R->gpu_learn += ms * 1e-3;
    R->gpu_all += ms * 1e-3;
    if (!L->dist) neth_policy_actor_switch(&L->P);
    else if (L->PS.launch) neth_policy_actor_switch(&L->PS);
    if (R->crit) neth_critic_switch(R->crit);
    ++R->refreshes;
    ++R->jobs;
    ++R->jobs_all;
    R->n += (int64_t)R->epochs * ((R->N + R->mb - 1) / R->mb);
    R->samples += R->N;
    R->busy->job = 0;
    R->busy = NULL;
    if (R->snap[0])
    {
        float *h = malloc(NETH_LEARN_NPARAM * sizeof(float));
        CK(cudaMemcpy(h, R->w, NETH_LEARN_NPARAM * sizeof(float), cudaMemcpyDeviceToHost));
        if (neth_ntw_save(R->snap, h)) printf("FAIL: --save-every %s: %s\n", R->snap, strerror(errno));
        free(h);
        R->snap[0] = 0;
    }
    if (R->esnap[0])
    {
        /* --eval-every's checkpoint, then its eval queued */
        float *h = malloc(NETH_LEARN_NPARAM * sizeof(float));
        CK(cudaMemcpy(h, R->w, NETH_LEARN_NPARAM * sizeof(float), cudaMemcpyDeviceToHost));
        if (neth_ntw_save(R->esnap, h)) printf("FAIL: --eval-every %s: %s\n", R->esnap, strerror(errno));
        else if (L->ckev) neth_eval_push(L->ckev, R->esnap, L->env_steps);
        free(h);
        R->esnap[0] = 0;
    }
}

/* the learner moved on: a finished update taken (wait: waiting for it), and the next waiting segment's enqueued */
void neth_train_poll(struct neth_pipe *L, int wait)
{
    struct neth_learner *R = &L->lr;
    if (R->busy)
    {
        cudaError_t e = wait ? cudaEventSynchronize(R->busy->freed) : cudaEventQuery(R->busy->freed);
        if (e == cudaSuccess) neth_train_done(L);
        else if (e != cudaErrorNotReady) CK(e);
    }
    while (R->nq > 0 && !L->fail)
    {
        struct neth_seg *S = R->q[0];
        /* kseg > 1: a batch's segments before its last are copied in at once (behind any update in flight on the
         * learner's stream) and freed for their group; the last waits for the learner, as a whole segment does */
        int collect = R->kseg > 1 && R->kfill < R->kseg - 1;
        if (!collect && R->busy) break;
        memmove(R->q, R->q + 1, (size_t)(R->nq - 1) * sizeof *R->q);
        --R->nq;
        if (!collect)
        {
            learn_enqueue(L, S);
            break;
        }
        learn_collect(L, S);
        CK(cudaEventRecord(S->freed, R->s));
        S->job = 0;
    }
}

/* a group's segment X about to be filled on stream s: what the learner has not read of it read first */
void neth_train_claim(struct neth_pipe *L, struct neth_seg *X, cudaStream_t s)
{
    while (X->job == 1 && !L->fail) neth_train_poll(L, 1);
    CK(cudaStreamWaitEvent(s, X->freed, 0));
}

/* every waiting segment trained (fastppo's learner thread empties its queue before the run ends) */
void neth_train_drain(struct neth_pipe *L)
{
    while ((L->lr.busy || L->lr.nq) && !L->fail) neth_train_poll(L, 1);
}
