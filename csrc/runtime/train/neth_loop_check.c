/* ntloop's gates without the env: --check-learn and --check-actor, the loop's own paths against the fixtures. */
#define _GNU_SOURCE
#include "neth_pipe_internal.h"
#include "learn_io.h"

/* ---- the gates without the env ---- */

static int cmp_bits(const char *what, const void *a, const void *b, size_t n)
{
    int d = memcmp(a, b, n);
    printf("  %-40s %s\n", what, d ? "DIFFER" : "equal, bit for bit");
    return d != 0;
}

/* max |a - ref| / (|ref| + 1e-3 max |ref|) (learn_check's measure) */
static double relerr(const float *a, const float *ref, size_t n)
{
    double mx = 0, e = 0;
    for (size_t i = 0; i < n; ++i) mx = fmax(mx, fabs((double)ref[i]));
    for (size_t i = 0; i < n; ++i) e = fmax(e, fabs((double)a[i] - ref[i]) / (fabs((double)ref[i]) + 1e-3 * mx));
    return e;
}

/* lane/audit-fixes: the GAE and the segment-end bootstrap on a hand-made segment of CT steps of CG envs, ntloop
 * --trunc-boot on (term the env's terminations) and off (term = done), each against the sums made here from the rule
 * itself: every end cuts the trace; a terminated row (done and term) bootstraps from zero, a timed-out one (done, not
 * term) from its own value; a last row open from the next segment's first value (val0). Env 0 is terminated at step 1,
 * env 1 timed out at step 1, env 2 timed out at the last step, env 3 terminated at the last step, env 4 open. The
 * returns are compared before the normalisation, the advantages after it (k_advnorm's mean and n - 1 spread) */
enum { CT = 4, CG = 5, CN = CT * CG };

static void gae_ref(const float *rew, const float *val, const float *val0, const uint8_t *done, const uint8_t *term,
                    float scale, double gamma, double lam, double *adv, double *ret)
{
    for (int e = 0; e < CG; ++e)
    {
        int l = (CT - 1) * CG + e;
        double boot = !done[l] ? val0[e] : term[l] ? 0.0 : val[l], last = 0.0;
        for (int t = CT - 1; t >= 0; --t)
        {
            int i = t * CG + e;
            double nv = !done[i] ? (t == CT - 1 ? boot : val[i + CG]) : term[i] ? 0.0 : val[i];
            last = (double)rew[i] / scale + gamma * nv - val[i] + (done[i] ? 0.0 : gamma * lam * last);
            adv[i] = last;
            ret[i] = last + val[i];
        }
    }
    double m = 0.0, q = 0.0;
    for (int i = 0; i < CN; ++i) m += adv[i];
    m /= CN;
    for (int i = 0; i < CN; ++i) q += (adv[i] - m) * (adv[i] - m);
    double sd = sqrt(q / (CN - 1));
    for (int i = 0; i < CN; ++i) adv[i] = (adv[i] - m) / (sd + 1e-8);
}

static int gae_case(neth_learn *D, const neth_learn_hyper *h0)
{
    static const uint8_t done[CN] = {0, 0, 0, 0, 0,  1, 1, 0, 0, 0,  0, 0, 0, 0, 0,  0, 0, 1, 1, 0};   /* [t][env] */
    static const uint8_t term[CN] = {0, 0, 0, 0, 0,  1, 0, 0, 0, 0,  0, 0, 0, 0, 0,  0, 0, 0, 1, 0};
    static const char *const what[CG] = {"terminated at step 1", "timed out at step 1", "timed out at the end",
                                         "terminated at the end", "open"};
    float rew[CN], val[CN], val0[CG], scale = 2.0f;
    for (int i = 0; i < CN; ++i) rew[i] = 0.25f * (float)(i % 3) - 0.125f, val[i] = 0.5f + 0.25f * (float)((i * 7) % 5);
    for (int e = 0; e < CG; ++e) val0[e] = 2.0f + 0.5f * (float)e;
    neth_learn_hyper h = *h0;
    h.gamma = 0.9, h.lam = 0.8;
    float *drew, *dval, *dval0, *dboot, *dadv, *dret, *dmask;
    uint8_t *ddone, *dterm, *ddrv;
    CK(cudaMalloc((void **)&drew, CN * 4));
    CK(cudaMalloc((void **)&dval, CN * 4));
    CK(cudaMalloc((void **)&dval0, CG * 4));
    CK(cudaMalloc((void **)&dboot, CG * 4));
    CK(cudaMalloc((void **)&dadv, CN * 4));
    CK(cudaMalloc((void **)&dret, CN * 4));
    CK(cudaMalloc((void **)&dmask, CN * 4));
    CK(cudaMalloc((void **)&ddone, CN));
    CK(cudaMalloc((void **)&dterm, CN));
    CK(cudaMalloc((void **)&ddrv, CN));
    CK(cudaMemcpy(drew, rew, CN * 4, cudaMemcpyHostToDevice));
    CK(cudaMemcpy(dval, val, CN * 4, cudaMemcpyHostToDevice));
    CK(cudaMemcpy(dval0, val0, CG * 4, cudaMemcpyHostToDevice));
    CK(cudaMemcpy(ddone, done, CN, cudaMemcpyHostToDevice));
    CK(cudaMemset(ddrv, 0, CN));
    float gadv[2][CN], gret[2][CN];
    double radv[2][CN], rret[2][CN];
    int fails = 0;
    printf("  the GAE case (lane/audit-fixes): T %d x %d envs, scale %g gamma %g lam %g; per env its steps' advantage "
           "(normalised) and return\n", CT, CG, scale, h.gamma, h.lam);
    for (int on = 1; on >= 0; --on)
    {
        const uint8_t *tm = on ? term : done;
        CK(cudaMemcpy(dterm, tm, CN, cudaMemcpyHostToDevice));
        size_t la = (size_t)(CT - 1) * CG;
        neth_boot(dval0, ddone + la, dterm + la, dval + la, CG, dboot, 0);
        CK(cudaDeviceSynchronize());
        neth_learn_gae(D, &h, CT, CG, scale, drew, ddone, dterm, ddrv, dval, dboot, dadv, dret, dmask);
        CK(cudaDeviceSynchronize());
        CK(cudaMemcpy(gadv[on], dadv, CN * 4, cudaMemcpyDeviceToHost));
        CK(cudaMemcpy(gret[on], dret, CN * 4, cudaMemcpyDeviceToHost));
        gae_ref(rew, val, val0, done, tm, scale, h.gamma, h.lam, radv[on], rret[on]);
        double ea = 0.0, er = 0.0;
        for (int i = 0; i < CN; ++i)
            ea = fmax(ea, fabs(gadv[on][i] - radv[on][i])), er = fmax(er, fabs(gret[on][i] - rret[on][i]));
        for (int e = 0; e < CG; ++e)
        {
            printf("  trunc-boot %-3s env %d (%-21s) adv", on ? "on" : "off", e, what[e]);
            for (int t = 0; t < CT; ++t) printf(" %9.5f", gadv[on][t * CG + e]);
            printf("  ret");
            for (int t = 0; t < CT; ++t) printf(" %9.5f", gret[on][t * CG + e]);
            printf("\n");
        }
        int ok = ea < 1e-4 && er < 1e-5;
        printf("  trunc-boot %-3s against the sums made here: adv within %.2g, ret within %.2g: %s\n", on ? "on" : "off",
               ea, er, ok ? "PASS" : "FAIL");
        fails += !ok;
    }
    /* on and off apart where the clock ended an episode (envs 1 and 2), the same returns bit for bit elsewhere */
    int apart = 0, same = 0;
    for (int e = 0; e < CG; ++e)
    {
        int d = memcmp(&gret[0][e], &gret[1][e], 4) != 0;
        for (int t = 1; t < CT; ++t) d |= memcmp(&gret[0][t * CG + e], &gret[1][t * CG + e], 4) != 0;
        if (e == 1 || e == 2) apart += d;
        else same += !d;
    }
    printf("  trunc-boot on against off: the returns of the 2 envs the clock ended differ in %d, the other 3 equal in "
           "%d: %s\n", apart, same, apart == 2 && same == 3 ? "PASS" : "FAIL");
    fails += !(apart == 2 && same == 3);
    cudaFree(drew), cudaFree(dval), cudaFree(dval0), cudaFree(dboot), cudaFree(dadv), cudaFree(dret), cudaFree(dmask);
    cudaFree(ddone), cudaFree(dterm), cudaFree(ddrv);
    return fails;
}

/* gate 1: learn_check's fixture segment fed through the loop's path (a group's segment as neth_pipe_launch() fills it, the
 * actions packed as the env's [16], learn_enqueue with the run file's recipe and the fixture's scale, order and Adam
 * state, the actor refreshed after it) against the learner called directly as learn_check calls it, bit for bit;
 * the run file's recipe against the fixture's; the refreshed actor's image against the one neth_actor_load_blob
 * makes of the new weights; and the weights against fastppo's (after3.ntw) as learn_check measures them */
int neth_check_learn(const char *dir, const neth_learn_hyper *h0, float ent0, int epochs0,
                       int mb0, int prec)
{
    lio_seg s = lio_segment(dir);
    size_t N = (size_t)s.N, P = NETH_LEARN_NPARAM * sizeof(float);
    int fails = 0;
    printf("check-learn %s: N %d (T %d x %d), %s; the loop's recipe: lr %g ent %g clip %g gamma %g lam %g vf %g epochs "
           "%d mb %d\n", dir, s.N, s.T, s.gs, prec == NETH_LEARN_BF16 ? NETH_NET_P16NAME : "fp32", h0->lr, ent0, h0->clip, h0->gamma,
           h0->lam, h0->vf, epochs0, mb0);
    /* the recipe: the run file's against the fixture's (fastppo's brute recipe both) */
    double fx[9] = {lio_num(s.man, "lr"), lio_num(s.man, "ent"), lio_num(s.man, "clip"), lio_num(s.man, "gamma"),
                    lio_num(s.man, "lam"), lio_num(s.man, "vf"), lio_num(s.man, "epochs"), lio_num(s.man, "mb"),
                    lio_num(s.man, "eps")};
    double ours[9] = {h0->lr, ent0, h0->clip, h0->gamma, h0->lam, h0->vf, epochs0, mb0, h0->eps};
    for (int i = 0; i < 9; ++i)
        if ((float)fx[i] != (float)ours[i])
        {
            printf("  the recipe's %d-th setting %g, the fixture's %g\n", i, ours[i], fx[i]);
            fails++;
        }
    printf("  %-40s %s\n", "the run file's recipe = the fixture's", fails ? "DIFFER" : "equal");
    float *w0 = lio_ntw(dir, "weights.ntw"), *m0 = lio_ntw(dir, "adam_m.ntw"), *v0 = lio_ntw(dir, "adam_v.ntw");

    /* the loop's side: one group of gs, its segment as neth_pipe_launch() leaves it */
    static struct neth_pipe L0;
    struct neth_pipe *L = &L0;
    L->G = 1;
    L->gs = s.gs;
    L->T = s.T;
    L->learn = L->learning = 1;
    L->ev = calloc(1, sizeof *L->ev);
    CK(cudaEventCreateWithFlags(&L->ev[0], cudaEventDisableTiming));
    struct neth_learner *R = &L->lr;
    R->h = *h0;
    R->entc = ent0;
    R->epochs = epochs0;
    R->mb = mb0;
    neth_train_init(L, s.N, prec);
    struct neth_seg S;
    neth_seg_alloc(&S, s.T, s.gs, NETH_NET_H * NETH_NET_W * NETH_NET_C);
    {
        int32_t *cam = lio_read(dir, "cam.i32", N * NETH_NET_CAM * 4);
        float *keys = lio_read(dir, "keys.f32", N * NETH_NET_KEYS * 4);
        int32_t *act = malloc(N * NETH_NET_ACTION * 4);
        for (size_t i = 0; i < N; ++i)
        {
            act[i * NETH_NET_ACTION] = cam[i * NETH_NET_CAM];
            act[i * NETH_NET_ACTION + 1] = cam[i * NETH_NET_CAM + 1];
            for (int k = 0; k < NETH_NET_KEYS; ++k) act[i * NETH_NET_ACTION + 2 + k] = (int32_t)keys[i * NETH_NET_KEYS + k];
        }
        if (NETH_NET_HOTBAR)
        {
            int32_t *hot = lio_read(dir, "hot.i32", N * 4);
            for (size_t i = 0; i < N; ++i) act[i * NETH_NET_ACTION + NETH_NET_CAM + NETH_NET_KEYS] = hot[i];
            free(hot);
        }
        CK(cudaMemcpy(S.act, act, N * NETH_NET_ACTION * 4, cudaMemcpyHostToDevice));
        free(cam), free(keys), free(act);
    }
    CK(cudaMemcpy(S.obs, s.obs, N * NETH_NET_H * NETH_NET_W * NETH_NET_C, cudaMemcpyDeviceToDevice));
    CK(cudaMemcpy(S.sc, s.sc, N * NETH_NET_NS * sizeof(neth_sc_t), cudaMemcpyDeviceToDevice));
    CK(cudaMemcpy(S.logp, s.logp, N * 4, cudaMemcpyDeviceToDevice));
    CK(cudaMemcpy(S.val, s.val, N * 4, cudaMemcpyDeviceToDevice));
    CK(cudaMemcpy(S.rew, s.rew, N * 4, cudaMemcpyDeviceToDevice));
    CK(cudaMemcpy(S.done, s.done, N, cudaMemcpyDeviceToDevice));
    CK(cudaMemcpy(S.term, s.done, N, cudaMemcpyDeviceToDevice));  /* lane/audit-fixes: the fixture's ends both masks */
    CK(cudaMemcpy(S.drv, s.drv, N, cudaMemcpyDeviceToDevice));
    CK(cudaMemcpy(S.boot, s.boot, (size_t)s.gs * 4, cudaMemcpyDeviceToDevice));
    if (NETH_NET_RNN) CK(cudaMemcpy(S.h0, s.h0, (size_t)s.gs * NETH_NET_D * 4, cudaMemcpyDeviceToDevice));
    CK(cudaEventRecord(S.ready, 0));
    CK(cudaMemcpy(R->w, w0, P, cudaMemcpyHostToDevice));
    CK(cudaMemcpy(R->m, m0, P, cudaMemcpyHostToDevice));
    CK(cudaMemcpy(R->v, v0, P, cudaMemcpyHostToDevice));
    R->step = s.adam_step;
    R->perm_fix = s.perm;
    S.scale = s.scale;
    size_t blen;
    unsigned char *blob = neth_ntw_blob(w0, &blen);
    char err[512];
    if (neth_policy_actor(&L->P, blob, blen, 1, s.gs, prec == NETH_LEARN_BF16, 1, err, sizeof err))
    {
        printf("FAIL: %s\n", err);
        return 1;
    }
    free(blob);
    L->pol = &L->P;
    S.job = 1;
    R->q[R->nq++] = &S;
    neth_train_drain(L);
    CK(cudaDeviceSynchronize());
    float *wl = malloc(P), *ml = malloc(P), *vl = malloc(P), bl[NETH_LEARN_NBOOK];
    CK(cudaMemcpy(wl, R->w, P, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(ml, R->m, P, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(vl, R->v, P, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(bl, R->acc, sizeof bl, cudaMemcpyDeviceToHost));
    int stepl = R->step;

    /* learn_check's own call: the fixture's buffers, its recipe from the manifest, a learner of its own */
    neth_learn_hyper h = neth_learn_hyper_default();
    h.gamma = lio_num(s.man, "gamma");
    h.lam = lio_num(s.man, "lam");
    h.clip = (float)lio_num(s.man, "clip");
    h.vf = (float)lio_num(s.man, "vf");
    h.lr = (float)lio_num(s.man, "lr");
    h.eps = (float)lio_num(s.man, "eps");
    h.max_norm = (float)lio_num(s.man, "max_norm");
    float entc = (float)lio_num(s.man, "ent");
    neth_learn *D = neth_learn_new(s.N, s.N, prec, NULL);
#if NETH_NET_RNN
    neth_learn_seq(D, s.T, s.done, s.h0);
#endif
    float *dw = lio_dev(w0, P), *dm = lio_dev(m0, P), *dv = lio_dev(v0, P), *dg, *dbooks;
    CK(cudaMalloc((void **)&dg, P));
    CK(cudaMalloc((void **)&dbooks, NETH_LEARN_NBOOK * 4));
    CK(cudaMemset(dbooks, 0, NETH_LEARN_NBOOK * 4));
    int step = s.adam_step;
    neth_learn_update(D, &h, entc, s.T, s.gs, s.scale, s.epochs, s.mb, s.perm, s.obs, S.sc, s.cam, s.keys, s.hot, s.logp, s.val,
               s.rew, s.done, s.done, s.drv, s.boot, dw, dg, dm, dv, &step, dbooks);
    CK(cudaDeviceSynchronize());
    float *wd = malloc(P), *md = malloc(P), *vd = malloc(P), bd[NETH_LEARN_NBOOK];
    CK(cudaMemcpy(wd, dw, P, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(md, dm, P, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(vd, dv, P, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(bd, dbooks, sizeof bd, cudaMemcpyDeviceToHost));
    fails += cmp_bits("the weights after the update", wl, wd, P);
    fails += cmp_bits("Adam's first moments", ml, md, P);
    fails += cmp_bits("Adam's second moments", vl, vd, P);
    fails += cmp_bits("the books (loss pg vl ent kl clipfrac)", bl, bd, sizeof bl);
    printf("  %-40s %d and %d\n", "Adam's steps after", stepl, step);
    fails += stepl != step;
    printf("  books over the %d Adam steps: loss %.6g pg %.6g vl %.6g ent %.6g kl %.6g clipfrac %.6g\n",
           stepl - s.adam_step, bl[0] / 3, bl[1] / 3, bl[2] / 3, bl[3] / 3, bl[4] / 3, bl[5] / 3);
    /* the refreshed actor */
    blob = neth_ntw_blob(wl, &blen);
    long dif = neth_policy_actor_diff(&L->P, blob, blen);
    free(blob);
    printf("  %-40s %ld bytes differ\n", "the actor refreshed = loaded from them", dif);
    fails += dif != 0;
    /* fastppo's own weights after the update (learn_check's gate measures each tensor; this, the worst of all) */
    float *r3 = lio_ntw(dir, "after3.ntw");
    printf("  the weights against fastppo's after the update: max relative error %.3g (learn_check gates them a "
           "tensor at a time)\n", relerr(wl, r3, NETH_LEARN_NPARAM));
    fails += gae_case(D, &h);
    printf("check-learn: %s\n", fails ? "FAIL" : "PASS");
    free(wl), free(ml), free(vl), free(wd), free(md), free(vd), free(r3), free(w0), free(m0), free(v0);
    return fails ? 1 : 0;
}

/* gate 2: actor_check's fixture batch through the loop's neth_pipe_launch() (groups of gs on their streams: the observation and
 * scalars copied from host memory into each group's segment, the actor, the actions back to the env's host buffer)
 * with the fixture's uniforms, fp32: every action equal, logp and value within actor_check's measure; then the same
 * with the actor's image refreshed from the learner's layout of the same weights (it must equal the loaded image
 * byte for byte); and bf16 measured */
int neth_check_actor(const char *dir)
{
    if (NETH_NET_RNN)
    {
        /* its batch is one step of feed-forward samples; the recurrent actor's steps and state are held
         * by actor_check on the recurrent fixture */
        printf("FAIL: --check-actor is the feed-forward net's (this build is NETH_NET_RNN: actor_check holds its actor)\n");
        return 2;
    }
    char *man = lio_read(dir, "manifest.json", 0);
    int n = (int)lio_num(man, "n"), gs = 128, G = n / gs, fails = 0;
    size_t oe = NETH_NET_H * NETH_NET_W * NETH_NET_C;
    uint8_t *obs = lio_read(dir, "obs.u8", (size_t)n * oe);
    float *sc = lio_read(dir, "sc.f32", (size_t)n * NETH_NET_NS * 4);
    int32_t *act = lio_read(dir, "act.i32", (size_t)n * NETH_NET_ACTION * 4);
    float *lp = lio_read(dir, "logp.f32", (size_t)n * 4), *val = lio_read(dir, "value.f32", (size_t)n * 4);
    float *ucam = lio_rdev(dir, "ucam.f32", (size_t)n * NETH_NET_CAM * NETH_NET_BINS * 4);
    float *ukey = lio_rdev(dir, "ukey.f32", (size_t)n * NETH_NET_KEYS * 4);
    float *uhot = NETH_NET_HOTBAR ? lio_rdev(dir, "uhot.f32", (size_t)n * NETH_NET_HOT * 4) : NULL;
    char p[4096], err[512];
    snprintf(p, sizeof p, "%s/weights.ntw", dir);
    size_t blen;
    unsigned char *blob = slurp(p, &blen);
    float *flat = malloc(NETH_LEARN_NPARAM * sizeof(float));
    if (!blob || neth_ntw_flat(blob, blen, flat, err, sizeof err))
    {
        printf("FAIL: %s: %s\n", p, blob ? err : "cannot read");
        return 1;
    }
    printf("check-actor %s: %d samples in %d groups of %d through the loop's launch\n", dir, n, G, gs);
    static struct neth_pipe L0;
    struct neth_pipe *L = &L0;
    L->n = n;
    L->G = G;
    L->gs = gs;
    L->T = 1;
    L->oe = oe;
    L->depth = G;   /* nothing sent: there is no env */
    struct neth_vec_bufs *b = &L->b;
    b->obs = obs;
    b->scalars = sc;
    b->actions = calloc((size_t)n * NETH_NET_ACTION, sizeof(int32_t));
    b->rewards = calloc((size_t)n, sizeof(float));
    b->truncations = calloc((size_t)n, 1);
    b->terminations = b->truncations;   /* lane/audit-fixes: no episode ends here */
    b->driver = calloc((size_t)n, 1);
    L->st = calloc((size_t)G, sizeof *L->st);
    L->ev = calloc((size_t)G, sizeof *L->ev);
    L->sg = calloc((size_t)G * 2, sizeof *L->sg);
    L->cur = calloc((size_t)G, sizeof *L->cur);
    L->tg = calloc((size_t)G, sizeof *L->tg);
    L->pq = calloc((size_t)G, sizeof *L->pq);
    L->t_lch = calloc((size_t)G, sizeof *L->t_lch);
    for (int g = 0; g < G; ++g)
    {
        CK(cudaStreamCreateWithFlags(&L->st[g], cudaStreamNonBlocking));
        CK(cudaEventCreateWithFlags(&L->ev[g], cudaEventDisableTiming));
        neth_seg_alloc(&L->sg[2 * g], 1, gs, oe);
    }
#if NETH_SC_BF16
    CK(cudaMallocHost((void **)&L->sch, (size_t)n * NETH_NET_NS * sizeof(neth_sc_t)));
#endif
    float *dflat;
    cudaStream_t rs;
    CK(cudaStreamCreateWithFlags(&rs, cudaStreamNonBlocking));
    CK(cudaMalloc((void **)&dflat, NETH_LEARN_NPARAM * sizeof(float)));
    CK(cudaMemcpy(dflat, flat, NETH_LEARN_NPARAM * sizeof(float), cudaMemcpyHostToDevice));
    for (int pass = 0; pass < 3; ++pass)
    {
        int bf16 = pass == 2;
        if (pass != 1 && neth_policy_actor(&L->P, blob, blen, G, gs, bf16, 1, err, sizeof err))
        {
            printf("FAIL: %s\n", err);
            return 1;
        }
        neth_policy_actor_uniforms(&L->P, ucam, ukey, uhot);
        if (pass == 1)
        {
            /* the learner's layout of the same weights, refreshed in as the trainer does after an update */
            for (int g = 0; g < G; ++g) CK(cudaStreamWaitEvent(rs, L->ev[g], 0));
            if (neth_policy_actor_refresh(&L->P, dflat, rs)) return printf("FAIL: the refresh\n"), 1;
            CK(cudaStreamSynchronize(rs));
            neth_policy_actor_switch(&L->P);
            long dif = neth_policy_actor_diff(&L->P, blob, blen);
            printf("  the refreshed image against the loaded one: %ld bytes differ\n", dif);
            fails += dif != 0;
        }
        L->pol = &L->P;
        memset(b->actions, 0, (size_t)n * NETH_NET_ACTION * sizeof(int32_t));
        for (int g = 0; g < G; ++g)
        {
            L->tg[g] = 0;
            neth_pipe_launch(L, g);
        }
        L->np = 0;
        CK(cudaDeviceSynchronize());
        int bad = 0;
        double el = 0, ev = 0;
        for (int i = 0; i < n; ++i)
        {
            bad += memcmp(b->actions + (size_t)i * NETH_NET_ACTION, act + (size_t)i * NETH_NET_ACTION, NETH_NET_ACTION * 4) != 0;
            const struct neth_seg *S = &L->sg[2 * (i / gs)];
            float l, v;
            CK(cudaMemcpy(&l, S->logp + i % gs, 4, cudaMemcpyDeviceToHost));
            CK(cudaMemcpy(&v, S->val + i % gs, 4, cudaMemcpyDeviceToHost));
            el = fmax(el, fabs((double)l - lp[i]));
            ev = fmax(ev, fabs((double)v - val[i]));
        }
        static const char *const what[3] = {"fp32, loaded", "fp32, refreshed from the learner's layout",
                                            "bf16 (measured, not gated)"};
        printf("  %-42s %d of %d actions differ; logp within %.3g, value within %.3g\n", what[pass], bad, n, el, ev);
        if (pass < 2)
            fails += bad != 0 || el > 1e-4 || ev > 1e-4;   /* actor_check: 1e-5 and 8.3e-7 measured */
        if (pass != 0) L->P.release(L->P.ctx);
    }
    printf("check-actor: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
