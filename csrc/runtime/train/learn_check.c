/* learn_check: the learner's kernels (learn.h) against fastppo.py's Learner on scripts/ntref.py's learner fixture.
 *
 *   learn_check [--prec fp32|tf32|bf16] [--dump FILE] [DIR]      DIR ~/.cache/netherite-native/learn-a05-070 by default;
 *                  --dump: the first step's gradient written to FILE (raw fp32, learn.h's layout) for a closer look
 *
 * The gate is fp32 (the default). bf16 is held instead to fastppo's own --amp bf16 learner: each gradient's, step's,
 * moment's and weight's error against fp32, |x - ref| / |ref| over the tensor, the worst of a class at most 1.25x the
 * reference's bf16 run's worst in that class and each tensor at most 2x its own (the fixture's "bf16_normwise";
 * BF16_SLACK's comment has why); GAE (fp32 in both) to fp32's tolerance; the forward and the books printed. tf32
 * runs the fp32 comparisons to state its error; it is not expected to pass.
 *
 * In order, each on the kernels' own results before it (errors carry, as in a run): GAE's advantages, returns and
 * mask; the forward over the segment (the camera's and keys' logits, the value); the first minibatch's books (loss
 * pg vl ent kl clipfrac), every parameter's gradient and the clip's norm; the weights, Adam's moments and the step
 * itself (the weights after less before) after its Adam step; and the weights and the steps after the whole update
 * (3 epochs from the same start). A tensor's error is max |x - ref| / (|ref| + 1e-3 max |ref|): relative, with a
 * floor for entries near zero (1e-2 for the steps, FLOOR_STEP). The tolerances, a class each, are 5x the worst the reference itself moved between its
 * own GPU and CPU runs (the fixture's manifest "spread", fp32 both; learn_check prints both numbers), rounded up:
 * fp32 here, never bit-identical (cuBLAS's sums are not cuDNN's). Exit 0 when every tensor is within. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "learn_io.h"

static int fails;

#define FLOOR 1e-3
#define FLOOR_STEP 1e-2     /* an Adam step's: the weights after it are fp32, a step 1e-3 of the largest resolved to 1% */

static double relerr_f(const float *a, const float *ref, size_t n, double floor) {
    double mx = 0.0, e = 0.0;
    for (size_t i = 0; i < n; i++) mx = fmax(mx, fabs((double)ref[i]));
    double fl = floor * (mx > 0 ? mx : 1e-30);
    for (size_t i = 0; i < n; i++) e = fmax(e, fabs((double)a[i] - ref[i]) / (fabs((double)ref[i]) + fl));
    return e;
}

static double relerr(const float *a, const float *ref, size_t n) { return relerr_f(a, ref, n, FLOOR); }

/* the reference's own spread for KEY: the larger of its second GPU run's and its CPU run's (the manifest's "spread":
 * {"KEY": {"gpu2": x, "cpu": y}}) */
static double spread(const char *man, const char *key) {
    char k[160];
    snprintf(k, sizeof k, "\"%s\": {", key);
    const char *p = strstr(man, "\"spread\"");
    if (!p || !(p = strstr(p, k))) {
        fprintf(stderr, "manifest: no spread for %s\n", key);
        exit(2);
    }
    const char *g = strstr(p, "\"gpu2\":"), *c = strstr(p, "\"cpu\":");
    return fmax(strtod(g + 7, NULL), strtod(c + 6, NULL));
}

/* a class's tolerance: 5x the worst spread of its keys, rounded up to one significant digit, at least lo (fp32's own
 * rounding over these sums, for a class the reference reproduced exactly) */
static double class_tol(double worst, double lo) {
    double t = 5.0 * worst;
    if (t <= lo) return lo;
    double p = pow(10.0, floor(log10(t)));
    return ceil(t / p * (1 - 1e-12)) * p;
}

/* |x - ref| / |ref| over the whole tensor (printed; the measure for tf32 and bf16, whose entries near zero move more) */
static double normerr(const float *a, const float *ref, size_t n) {
    double d = 0, r = 0;
    for (size_t i = 0; i < n; i++) {
        d += ((double)a[i] - ref[i]) * ((double)a[i] - ref[i]);
        r += (double)ref[i] * ref[i];
    }
    return r > 0 ? sqrt(d / r) : sqrt(d);
}

static double worst_norm[2], worst_torch[2];    /* the gradients', the steps': ours, and fastppo's own bf16's */

/* --prec bf16 (lane/ntwidth): each class's (the gradients, Adam's first moment, its steps d1 and d3, the weights after
 * one step and after the update) worst normwise error against fp32 within BF16_SLACK of fastppo's own --amp bf16
 * learner's worst in that class (the fixture's "bf16_normwise"), and every tensor within BF16_BOUND of fastppo's on
 * that tensor; a tensor over BF16_SLACK is printed "over". Adam's second moment v1 = 0.999 v0 + 0.001 g^2 (v0 exact)
 * is held to BF16_BOUND alone: its error is the gradient's, weighted to its largest entries, and the gradient and the
 * step v1 makes are gated by class. Per tensor at 1.25x (lane/ntlearn's bar) the fresh fixtures fail 0 to 5 of 113 to
 * 127 tensors, all v1 or a first layer's bias of 16 to 32 entries, and which ones moves with the fixture's seed
 * (out/report.md); our per-layer errors equal torch's (input gradients 7.5 to 9.7%, relu masks flipped 6e-4 to
 * 1.3e-3 at w1). */
#define BF16_SLACK 1.25
#define BF16_BOUND 2.0
static int bf16_gate;

/* fastppo's own --amp bf16 learner's normwise error for KEY (the manifest's "bf16_normwise"), -1 if absent */
static double torch_bf16(const char *man, const char *key) {
    char k[160];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(man, "\"bf16_normwise\"");
    if (!p || !(p = strstr(p, k))) return -1;
    return strtod(p + strlen(k), NULL);
}

/* fp32 (lane/ntwidth): every tensor's normwise error also at most FP32_NORM. The class tolerance above is 5x the
 * reference's own GPU-against-CPU spread, and on the fresh fixtures that spread reaches 0.4 (a first layer's entries
 * near zero after cancellation: tolerances to 3); our worst normwise is 7.1e-4 (s1), bf16's 1e-2 to 1e-1 */
#define FP32_NORM 2e-3
static int fp32_gate;

static void report_n(const char *man, const char *what, const char *key, double err, double tol, double ne) {
    int ok = err <= tol && !(fp32_gate && ne > FP32_NORM);
    fails += !ok;
    printf("%-4s %-22s err %.3e  tol %.0e  reference's spread %.2e", ok ? "ok" : "FAIL", what, err, tol,
           spread(man, key));
    if (ne >= 0) printf("  normwise %.2e (fastppo's bf16 %.2e)", ne, torch_bf16(man, key));
    printf("\n");
}

static void report(const char *man, const char *what, const char *key, double err, double tol) {
    if (bf16_gate && strcmp(key, "adv") && strcmp(key, "ret") && strcmp(key, "mask")) {   /* GAE is fp32 at any */
        printf("info %-22s err %.3e  (fp32's tol %.0e)\n", what, err, tol);
        return;
    }
    report_n(man, what, key, err, tol, -1);
}

static void *dget(const void *d, size_t n) {
    void *h = malloc(n);
    if (cudaMemcpy(h, d, n, cudaMemcpyDeviceToHost) != cudaSuccess) {
        fprintf(stderr, "dget\n");
        exit(2);
    }
    return h;
}

static const char *const tname[NETH_NET_TENSORS] = {
#if NETH_NET_STEM
                                              "stem.0.weight", "stem.0.bias",
#endif
                                              "conv.0.weight", "conv.0.bias", "conv.2.weight", "conv.2.bias",
                                              "conv.4.weight", "conv.4.bias", "fc.0.weight",   "fc.0.bias",
                                              "fc.2.weight",   "fc.2.bias",   "cam.weight",    "cam.bias",
                                              "keys.weight",   "keys.bias",   "v.weight",      "v.bias",
#if NETH_NET_HOTBAR
                                              "hot.weight",    "hot.bias",
#endif
#if NETH_NET_RNN
                                              "rnn.weight",    "rnn.bias",
#endif
};
static const struct {
    int off, n, ntw;
} lay[NETH_NET_TENSORS] = {
#define LC(n, i, k) {NETH_LEARN_OFF(n), (k), (i)},
    NETH_LEARN_LAYOUT(LC)
#undef LC
};

/* every tensor of a flat buffer against the reference's, the key PRE:name */
static void tensors(const char *man, const char *pre, const float *a, const float *ref, double lo) {
    double fl = pre[0] == 'd' && pre[1] != 0 && pre[1] <= '9' ? FLOOR_STEP : FLOOR, worst = 0;
    char key[NETH_NET_TENSORS][96];
    for (int t = 0; t < NETH_NET_TENSORS; t++) {
        snprintf(key[t], sizeof key[t], "%s:%s", pre, tname[lay[t].ntw]);
        worst = fmax(worst, spread(man, key[t]));
    }
    double tol = class_tol(worst, lo);
    double cw[2] = {0, 0};   /* bf16: the class's worst, ours and fastppo's */
    for (int t = 0; t < NETH_NET_TENSORS; t++) {
        double ne = normerr(a + lay[t].off, ref + lay[t].off, lay[t].n);
        if (!strcmp(pre, "grad")) {
            worst_norm[0] = fmax(worst_norm[0], ne);
            worst_torch[0] = fmax(worst_torch[0], torch_bf16(man, key[t]));
        }
        if (pre[0] == 'd') {
            worst_norm[1] = fmax(worst_norm[1], ne);
            worst_torch[1] = fmax(worst_torch[1], torch_bf16(man, key[t]));
        }
        if (bf16_gate) {    /* the tensor within BF16_BOUND of fastppo's own bf16 learner; the class below */
            double tb = torch_bf16(man, key[t]);
            /* fastppo's bf16 bit for bit its fp32 (tb 0: lane/ntgru's s1.r1 fixture, v.bias after Adam's steps, each
             * +-lr whatever the gradient's rounding): held to the fp32 gate's own normwise bound instead */
            int ok = ne <= (tb > 0 ? BF16_BOUND * tb + 1e-7 : FP32_NORM), over = ne > BF16_SLACK * tb + 1e-7;
            fails += !ok;
            cw[0] = fmax(cw[0], ne), cw[1] = fmax(cw[1], tb);
            printf("%-4s %-22s normwise %.3e  fastppo's own bf16 %.3e (%.2fx)\n", !ok ? "FAIL" : over ? "over" : "ok",
                   key[t], ne, tb, tb > 0 ? ne / tb : 0.0);
            continue;
        }
        report_n(man, key[t], key[t], relerr_f(a + lay[t].off, ref + lay[t].off, lay[t].n, fl), tol, ne);
    }
    if (bf16_gate && strcmp(pre, "v1")) {
        int ok = cw[0] <= BF16_SLACK * cw[1] + 1e-7;
        fails += !ok;
        printf("%-4s class %-16s worst normwise %.3e, fastppo's own bf16's worst %.3e (%.2fx)\n", ok ? "ok" : "FAIL",
               pre, cw[0], cw[1], cw[1] > 0 ? cw[0] / cw[1] : 0.0);
    }
}

static float *diff(const float *a, const float *b) {
    float *d = (float *)malloc(NETH_LEARN_NPARAM * sizeof(float));
    for (int i = 0; i < NETH_LEARN_NPARAM; i++) d[i] = a[i] - b[i];
    return d;
}

int main(int argc, char **argv) {
    char def[4096];
    const char *croot = getenv("NETHER_CACHE");
    snprintf(def, sizeof def, "%s/native/learn-a05-070", (croot && croot[0]) ? croot : ".cache");
    const char *dir = def, *dump = NULL;
    int prec = NETH_LEARN_FP32;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--prec") && i + 1 < argc) {
            const char *p = argv[++i];
            prec = !strcmp(p, "bf16") ? NETH_LEARN_BF16 : !strcmp(p, "tf32") ? NETH_LEARN_TF32 : NETH_LEARN_FP32;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump = argv[++i];
        else
            dir = argv[i];
    }
    lio_seg s = lio_segment(dir);
    const char *man = s.man;
    size_t N = s.N, P = NETH_LEARN_NPARAM * sizeof(float);
    neth_learn_hyper h = neth_learn_hyper_default();
    h.gamma = lio_num(man, "gamma");
    h.lam = lio_num(man, "lam");
    h.clip = (float)lio_num(man, "clip");
    h.vf = (float)lio_num(man, "vf");
    h.lr = (float)lio_num(man, "lr");
    h.eps = (float)lio_num(man, "eps");
    h.max_norm = (float)lio_num(man, "max_norm");
    float entc = (float)lio_num(man, "ent");
    bf16_gate = prec == NETH_LEARN_BF16;
    fp32_gate = prec == NETH_LEARN_FP32;
    neth_learn *L = neth_learn_new(s.N, s.N, prec, NULL);
    if (!L) return 2;
#if NETH_NET_RNN
    neth_learn_seq(L, s.T, s.done, s.h0);  /* the recurrent net's segment: its start states and ends (lane/ntgru) */
#endif
    float *w0 = lio_ntw(dir, "weights.ntw"), *m0 = lio_ntw(dir, "adam_m.ntw"), *v0 = lio_ntw(dir, "adam_v.ntw");
    float *dw = (float *)lio_dev(w0, P), *dm = (float *)lio_dev(m0, P), *dv = (float *)lio_dev(v0, P);
    float *dg, *dadv, *dret, *dmask, *dbooks, *dgn;
    cudaMalloc((void **)&dg, P);
    cudaMalloc((void **)&dadv, N * 4);
    cudaMalloc((void **)&dret, N * 4);
    cudaMalloc((void **)&dmask, N * 4);
    cudaMalloc((void **)&dbooks, NETH_LEARN_NBOOK * 4);
    cudaMalloc((void **)&dgn, 4);
    printf("learn_check %s %s: N %d, Adam step %d before, scale %g\n", NETH_NET_VARIANT, dir, s.N, s.adam_step, s.scale);

    /* GAE */
    neth_learn_gae(L, &h, s.T, s.gs, s.scale, s.rew, s.done, s.drv, s.val, s.boot, dadv, dret, dmask);
    const char *gk[3] = {"adv", "ret", "mask"};
    float *gd[3] = {dadv, dret, dmask};
    double gtol = class_tol(fmax(spread(man, "adv"), fmax(spread(man, "ret"), spread(man, "mask"))), 1e-5);
    for (int i = 0; i < 3; i++) {
        char f[32];
        snprintf(f, sizeof f, "%s.f32", gk[i]);
        float *a = (float *)dget(gd[i], N * 4), *r = (float *)lio_read(dir, f, N * 4);
        report(man, gk[i], gk[i], relerr(a, r, N), gtol);
        free(a);
        free(r);
    }

    /* the forward over the segment in its own order */
    {
        float *hd = (float *)dget(neth_learn_forward(L, dw, s.N, NULL, s.obs, s.sc), N * NETH_LEARN_HSTRIDE * 4);
        float *rc = (float *)lio_read(dir, "fcam.f32", N * NETH_NET_CAM * NETH_NET_BINS * 4);
        float *rk = (float *)lio_read(dir, "fkeys.f32", N * NETH_NET_KEYS * 4), *rv = (float *)lio_read(dir, "fval.f32", N * 4);
        float *c = (float *)malloc(N * NETH_NET_CAM * NETH_NET_BINS * 4), *k = (float *)malloc(N * NETH_NET_KEYS * 4), *v = (float *)malloc(N * 4);
        for (size_t i = 0; i < N; i++) {
            memcpy(c + i * NETH_NET_CAM * NETH_NET_BINS, hd + i * NETH_LEARN_HSTRIDE, NETH_NET_CAM * NETH_NET_BINS * 4);
            memcpy(k + i * NETH_NET_KEYS, hd + i * NETH_LEARN_HSTRIDE + NETH_NET_CAM * NETH_NET_BINS, NETH_NET_KEYS * 4);
            v[i] = hd[i * NETH_LEARN_HSTRIDE + NETH_LEARN_HEADS - 1];
        }
        double ft = class_tol(fmax(spread(man, "fcam"), fmax(spread(man, "fkeys"), spread(man, "fval"))), 1e-5);
        report(man, "forward cam", "fcam", relerr(c, rc, N * NETH_NET_CAM * NETH_NET_BINS), ft);
        report(man, "forward keys", "fkeys", relerr(k, rk, N * NETH_NET_KEYS), ft);
        report(man, "forward value", "fval", relerr(v, rv, N), ft);
#if NETH_NET_HOTBAR
        {
            float *rh = (float *)lio_read(dir, "fhot.f32", N * NETH_NET_HOT * 4), *hh = (float *)malloc(N * NETH_NET_HOT * 4);
            for (size_t i = 0; i < N; i++) memcpy(hh + i * NETH_NET_HOT, hd + i * NETH_LEARN_HSTRIDE + NETH_LEARN_HOFF, NETH_NET_HOT * 4);
            report(man, "forward hotbar", "fhot", relerr(hh, rh, N * NETH_NET_HOT), class_tol(spread(man, "fhot"), ft));
            free(rh), free(hh);
        }
#endif
        free(hd), free(rc), free(rk), free(rv), free(c), free(k), free(v);
    }

    /* lane/ntppo: a fixture made with fastppo's --ref-kl (out/fix_refkl.py: the manifest's "ref_kl", the reference
     * policy's weights ref.ntw): the reference's forward over the segment, then the books (ref_kl's too), gradients
     * and steps with the KL's term in them */
    int nbook = 6;
    if (strstr(man, "\"ref_kl\":")) {
        h.ref_kl = (float)lio_num(man, "ref_kl");
        float *wr = lio_ntw(dir, "ref.ntw");
        if (neth_learn_set_ref(L, (const float *)lio_dev(wr, P))) return 2;
        free(wr);
        neth_learn_ref_forward(L, 0, s.N, s.obs, s.sc);
        nbook = 7;
        printf("     the reference policy ref.ntw, ref_kl %g\n", h.ref_kl);
    }
    /* the first minibatch: books, gradients, the clip's norm */
    neth_learn_grad(L, &h, entc, dw, s.N, s.perm, s.obs, s.sc, s.cam, s.keys, s.hot, s.logp, dadv, dret, dmask, dg, dbooks);
    {
        float *b = (float *)dget(dbooks, NETH_LEARN_NBOOK * 4), *rb = (float *)lio_read(dir, "loss.f32", NETH_LEARN_NBOOK * 4);
        const char *bn[7] = {"loss", "pg", "vl", "ent", "kl", "clipfrac", "ref_kl"};
        /* pg is a sum of terms of both signs that cancels to a few 1e-4 of their absolute mass (sum |term| / nm,
         * about 0.7 on the fresh fixtures): its floor is FLOOR of that mass, the tensors' measure (lane/ntgru: the
         * recurrent fixture's fp32 forward, 1.5e-7 off in its logits, moved pg 7e-8, 1.04e-4 of pg itself; the
         * reference's own logp with 2e-7 relative noise moves it 9.5e-8) */
        double mass = 0;
        {
            float *flp = (float *)lio_read(dir, "flp.f32", N * 4), *lp0 = (float *)dget(s.logp, N * 4);
            float *ad = (float *)lio_read(dir, "adv.f32", N * 4), *mk = (float *)lio_read(dir, "mask.f32", N * 4);
            double nm = 0;
            for (size_t i = 0; i < N; i++) {
                double r = exp((double)flp[i] - lp0[i]), rc = fmin(fmax(r, 1.0 - h.clip), 1.0 + h.clip);
                mass += fabs(fmin(r * ad[i], rc * ad[i])) * mk[i];
                nm += mk[i];
            }
            mass /= fmax(nm, 1.0);
            free(flp), free(lp0), free(ad), free(mk);
        }
        double e = 0;
        for (int i = 0; i < nbook; i++) {   /* lane/ntppo: nbook 7 with the ref_kl book, else 6 */
            double ei = fabs((double)b[i] - rb[i]) / (fabs((double)rb[i]) + (i == 1 ? FLOOR * mass : 1e-6));
            printf("     book %-8s %.7g  reference %.7g\n", bn[i], b[i], rb[i]);
            e = fmax(e, ei);
        }
        /* each book on its own, relative (the spread's measure floors the stack at 1e-3 of the entropy): a fixed 1e-4,
         * fp32 sums over the minibatch */
        report(man, "books", "loss", e, 1e-4);
        free(b), free(rb);
    }
    float *g = (float *)dget(dg, P), *rg = lio_ntw(dir, "grad.ntw");
    if (dump) {     /* --dump FILE: the first step's gradient, the learner's layout (learn.h), raw fp32 */
        FILE *f = fopen(dump, "wb");
        if (!f || fwrite(g, 1, P, f) != P || fclose(f)) fprintf(stderr, "learn_check: cannot write %s\n", dump);
    }
    tensors(man, "grad", g, rg, 1e-4);
    neth_learn_adam(L, &h, s.adam_step + 1, dw, dg, dm, dv, dgn);
    {
        float gn, rgn = (float)lio_num(man, "gnorm");
        cudaMemcpy(&gn, dgn, 4, cudaMemcpyDeviceToHost);
        printf("     clip norm %.7g  reference %.7g\n", gn, rgn);
        report(man, "clip norm", "gnorm", fabs((double)gn - rgn) / rgn, class_tol(spread(man, "gnorm"), 1e-6));
    }
    float *w1 = (float *)dget(dw, P), *m1 = (float *)dget(dm, P), *v1 = (float *)dget(dv, P);
    float *rw1 = lio_ntw(dir, "after1.ntw"), *rm1 = lio_ntw(dir, "m1.ntw"), *rv1 = lio_ntw(dir, "v1.ntw");
    tensors(man, "after1", w1, rw1, 1e-6);
    tensors(man, "d1", diff(w1, w0), diff(rw1, w0), 1e-4);
    tensors(man, "m1", m1, rm1, 1e-4);
    tensors(man, "v1", v1, rv1, 1e-5);

    /* the whole update from the same start */
    cudaMemcpy(dw, w0, P, cudaMemcpyHostToDevice);
    cudaMemcpy(dm, m0, P, cudaMemcpyHostToDevice);
    cudaMemcpy(dv, v0, P, cudaMemcpyHostToDevice);
    int step = s.adam_step;
    neth_learn_update(L, &h, entc, s.T, s.gs, s.scale, s.epochs, s.mb, s.perm, s.obs, s.sc, s.cam, s.keys, s.hot, s.logp, s.val,
               s.rew, s.done, s.drv, s.boot, dw, dg, dm, dv, &step, NULL);
    float *w3 = (float *)dget(dw, P), *rw3 = lio_ntw(dir, "after3.ntw");
    tensors(man, "after3", w3, rw3, 1e-6);
    tensors(man, "d3", diff(w3, w0), diff(rw3, w0), 1e-4);

    /* lane/ntppo: the value warm-up (h.vonly). The value head's gradient is the value term's alone in fastppo's loss
     * too (pg, the entropy and the KL do not reach v.weight or v.bias), so the fixture's grad for those two holds it;
     * every other gradient is 0, and a whole update moves v.weight and v.bias alone: every other weight and both
     * moments bit for bit as they were */
    {
        neth_learn_hyper hv = h;
        hv.vonly = 1;
        cudaMemcpy(dw, w0, P, cudaMemcpyHostToDevice);
        cudaMemcpy(dm, m0, P, cudaMemcpyHostToDevice);
        cudaMemcpy(dv, v0, P, cudaMemcpyHostToDevice);
        neth_learn_grad(L, &hv, entc, dw, s.N, s.perm, s.obs, s.sc, s.cam, s.keys, s.hot, s.logp, dadv, dret, dmask, dg,
                 dbooks);
        float *gv = (float *)dget(dg, P);
        const int vo[2] = {NETH_LEARN_OFF(v_w), NETH_LEARN_OFF(v_b)}, vn[2] = {NETH_LEARN_N_v_w, NETH_LEARN_N_v_b};
        double ev = 0, tv = 1e-4;   /* bf16: normwise, within BF16_BOUND of fastppo's own bf16 on v.weight */
        for (int j = 0; j < 2; j++)
            ev = fmax(ev, bf16_gate ? normerr(gv + vo[j], rg + vo[j], vn[j]) : relerr(gv + vo[j], rg + vo[j], vn[j]));
        if (bf16_gate) tv = BF16_BOUND * fmax(torch_bf16(man, "grad:v.weight"), torch_bf16(man, "grad:v.bias")) + 1e-7;
        long nz = 0;
        for (int t = 0; t < NETH_NET_TENSORS; t++)
            if (lay[t].off == NETH_LEARN_OFF(cam_w) || lay[t].off == NETH_LEARN_OFF(keys_w) || lay[t].off == NETH_LEARN_OFF(cam_b) ||
                lay[t].off == NETH_LEARN_OFF(keys_b))
                for (int i = 0; i < lay[t].n; i++) nz += gv[lay[t].off + i] != 0.0f;
        int ok = ev <= tv && nz == 0;
        fails += !ok;
        printf("%-4s value warm-up's gradient: the value head's err %.3e (tol %.0e) against the fixture's; %ld nonzero "
               "in the policy heads' (0)\n", ok ? "ok" : "FAIL", ev, tv, nz);
        int vstep = 0;
        neth_learn_update(L, &hv, entc, s.T, s.gs, s.scale, s.epochs, s.mb, s.perm, s.obs, s.sc, s.cam, s.keys, s.hot, s.logp,
                   s.val, s.rew, s.done, s.drv, s.boot, dw, dg, dm, dv, &vstep, NULL);
        float *wv = (float *)dget(dw, P), *mv = (float *)dget(dm, P), *vv = (float *)dget(dv, P);
        long moved = 0, still = 0;
        for (int i = 0; i < NETH_LEARN_NPARAM; i++) {
            int head = (i >= vo[0] && i < vo[0] + vn[0]) || i == vo[1];
            int same = !memcmp(wv + i, w0 + i, 4) && !memcmp(mv + i, m0 + i, 4) && !memcmp(vv + i, v0 + i, 4);
            if (head) still += !memcmp(wv + i, w0 + i, 4) && gv[i] != 0.0f;     /* a dead unit's gets no gradient */
            else moved += !same;
        }
        ok = moved == 0 && still == 0 && vstep == s.epochs;
        fails += !ok;
        printf("%-4s value warm-up's update: %ld entries outside the value head changed (0), %ld of the head's %d "
               "with a gradient unmoved (0), its own Adam count %d (%d)\n", ok ? "ok" : "FAIL", moved, still, NETH_LEARN_N_v_w + NETH_LEARN_N_v_b,
               vstep, s.epochs);
        free(gv), free(wv), free(mv), free(vv);
    }
    cudaError_t e = cudaDeviceSynchronize();
    if (e != cudaSuccess) {
        fprintf(stderr, "learn_check: %s\n", cudaGetErrorString(e));
        return 2;
    }
    printf("learn_check: worst normwise error: a gradient %.2e, an Adam step %.2e (fastppo's own bf16: %.2e, %.2e)\n",
           worst_norm[0], worst_norm[1], worst_torch[0], worst_torch[1]);
    printf("learn_check: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    neth_learn_free(L);
    return fails ? 1 : 0;
}
