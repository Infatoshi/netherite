/* actor_check: the actor (actor.h) against mcsr-speedrun's scripts/ntref.py fixture (the reference: fastppo.py's
 * Small in fp32 on the CPU, and its gumbel sampler on the fixture's noise).
 *
 *   actor_check [--temp T] [--greedy keys|cam|both] [FIXTURE_DIR]      (default ~/.cache/netherite-native/actor-a05-070)
 *
 * lane/audit-fixes: --temp and --greedy draw as ntloop's flags do (actor.h neth_actor_set_draw); the fixture's actions are
 * the policy's own draw's, so such a run reports the tempered or greedy actions' differences from them, as the old .tN
 * and .gk/.gc/.gkc builds' runs did
 *
 * fp32: the camera and key logits and the value within 1e-4 (max abs) of cam.f32, keys.f32 and value.f32; with
 * ucam.f32 and ukey.f32 as the noise, the actions equal act.i32 except where the reference's own margin is under
 * 1e-4 (a camera head's top two gumbel-perturbed logits, a key's |u - sigmoid(logit)|, both from the reference's
 * logits in double; such samples are counted and printed), and logp within 1e-4 of logp.f32 on every sample whose
 * action equals the reference's. The groups run at sizes 100, 28, 128, 512 and 256 to cover partial tiles.
 * The device RNG: every action in range, logp equal to the log-probability of the drawn action under the returned
 * logits (1e-4), the draws' frequencies against the logits' probabilities (|z| under 5), and one seed and counter
 * drawing the same actions twice. bf16 (NETH_NET_ACTOR_BF16; lane/ntwidth): the camera and key logits' and the value's
 * |x - ref| / |ref| over the fixture at most 1.25 times fastppo's own bf16 actor's on the same fixture (the
 * manifest's "bf16_normwise", which ntref.py actor writes; an older fixture without it is measured, not gated), its
 * max |d| and action mismatches on the fixture's noise printed. The build is one of net.h's variants (NETH_NET_WM,
 * NETH_NET_STEM); its fixture is ntref.py's for the same net (~/.cache/netherite-native/actor-fresh-V). */
#include "actor.h"
#include "learn.h"
#include "net.h"

#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NCAM (NETH_NET_CAM * NETH_NET_BINS)
#define TOL 1e-4
#define NHOT (NETH_NET_HOTBAR * NETH_NET_HOT)  /* NETH_NET_HOTBAR (lane/nthotbar): the hotbar's logits hot.f32 [n][10], its noise
                                    * uhot.f32, its draw act.i32's last column, held as a camera head is */
#define HOTA (NETH_NET_CAM + NETH_NET_KEYS)    /* the hotbar's column in the action */

static void *slurp(const char *dir, const char *name, size_t want)
{
    char p[8192];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) {
        fprintf(stderr, "actor_check: cannot open %s\n", p);
        exit(2);
    }
    void *d = malloc(want);
    size_t got = fread(d, 1, want, f);
    int extra = fgetc(f) != EOF;
    fclose(f);
    if (got != want || extra) {
        fprintf(stderr, "actor_check: %s is not %zu bytes\n", p, want);
        exit(2);
    }
    return d;
}

#define CK(x)                                                                                  \
    do {                                                                                       \
        cudaError_t e_ = (x);                                                                  \
        if (e_ != cudaSuccess) {                                                               \
            fprintf(stderr, "actor_check: %s: %s\n", #x, cudaGetErrorString(e_));               \
            exit(2);                                                                           \
        }                                                                                      \
    } while (0)

static void *dmalloc(size_t n)
{
    void *p;
    CK(cudaMalloc(&p, n));
    return p;
}

typedef struct {
    int32_t *act;
    float *logp, *value, *cam, *keys, *hot;
} outs;

static const float *duhot;  /* NETH_NET_HOTBAR: the fixture's hotbar noise on the device, used where ucam is given */
#if NETH_NET_RNN
/* the recurrent fixture (lane/ntgru, ntref.py actor on a fresh:...,rnn=1 net): rs steps of rne envs, sample t * rne +
 * e; each run starts the envs' state at h0 and steps them in order (a group never spans two steps), first [rs][rne] */
static int rs = 1, rne;
static float *rdh, *rdh0;
static uint8_t *rdfirst;
#endif

/* the whole fixture through the actor in groups of the sizes above (noise given) or of 512 (the device RNG) */
static void run(neth_actor_ws *w, int n, const uint8_t *dobs, const neth_sc_t *dsc, const float *ducam, const float *dukey,
                outs *h, cudaStream_t s)
{
    static const int sizes[] = {100, 28, 128, 512, 512, 512, 512, 512, 512, 512, 256};
    int32_t *act = dmalloc((size_t)n * NETH_NET_ACTION * 4);
    float *logp = dmalloc(n * 4), *value = dmalloc(n * 4), *cam = dmalloc((size_t)n * NCAM * 4),
          *keys = dmalloc((size_t)n * NETH_NET_KEYS * 4), *hot = NHOT ? dmalloc((size_t)n * NHOT * 4) : NULL;
    for (int b0 = 0, i = 0; b0 < n; i++) {
        int B = ducam ? sizes[i % (sizeof sizes / sizeof *sizes)] : 512;
        if (B > n - b0)
            B = n - b0;
        neth_actor_io io = {dobs + (size_t)b0 * NETH_NET_H * NETH_NET_W * NETH_NET_C, dsc + (size_t)b0 * NETH_NET_NS, act + (size_t)b0 * NETH_NET_ACTION,
                          logp + b0, value + b0, cam + (size_t)b0 * NCAM, keys + (size_t)b0 * NETH_NET_KEYS,
                          ducam ? ducam + (size_t)b0 * NCAM : NULL, dukey ? dukey + (size_t)b0 * NETH_NET_KEYS : NULL,
                          hot ? hot + (size_t)b0 * NHOT : NULL, ducam && duhot ? duhot + (size_t)b0 * NHOT : NULL,
                          NULL, NULL};
#if NETH_NET_RNN
        if (b0 == 0)
            CK(cudaMemcpyAsync(rdh, rdh0, (size_t)rne * NETH_NET_D * 4, cudaMemcpyDeviceToDevice, s));
        if (B > rne - b0 % rne)
            B = rne - b0 % rne;
        io.h = rdh + (size_t)(b0 % rne) * NETH_NET_D;
        io.first = rdfirst + b0;
#endif
        int e = neth_actor_step(w, B, &io, s);
        if (e) {
            fprintf(stderr, "actor_check: neth_actor_step: %s\n", cudaGetErrorString(e));
            exit(2);
        }
        b0 += B;
    }
    CK(cudaStreamSynchronize(s));
    CK(cudaMemcpy(h->act, act, (size_t)n * NETH_NET_ACTION * 4, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(h->logp, logp, n * 4, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(h->value, value, n * 4, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(h->cam, cam, (size_t)n * NCAM * 4, cudaMemcpyDeviceToHost));
    CK(cudaMemcpy(h->keys, keys, (size_t)n * NETH_NET_KEYS * 4, cudaMemcpyDeviceToHost));
    if (hot)
        CK(cudaMemcpy(h->hot, hot, (size_t)n * NHOT * 4, cudaMemcpyDeviceToHost));
    cudaFree(hot);
    cudaFree(act);
    cudaFree(logp);
    cudaFree(value);
    cudaFree(cam);
    cudaFree(keys);
}

static double maxdiff(const float *a, const float *b, size_t n)
{
    double m = 0;
    for (size_t i = 0; i < n; i++) {
        double d = fabs((double)a[i] - b[i]);
        if (!(d <= m))
            m = isnan(d) ? INFINITY : d;
    }
    return m;
}

/* |a - ref| / |ref| over a tensor */
static double normerr(const float *a, const float *ref, size_t n)
{
    double d = 0, r = 0;
    for (size_t i = 0; i < n; i++) {
        d += ((double)a[i] - ref[i]) * ((double)a[i] - ref[i]);
        r += (double)ref[i] * ref[i];
    }
    return r > 0 ? sqrt(d / r) : sqrt(d);
}

/* a whole text file, or NULL */
static char *slurp_text(const char *dir, const char *name)
{
    char p[8192];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    b[fread(b, 1, n, f)] = 0;
    fclose(f);
    return b;
}

/* the manifest's number OBJ.KEY ({"OBJ": {"KEY": x}}), -1 if absent */
static double manifest_num(const char *man, const char *obj, const char *key)
{
    char k[160];
    snprintf(k, sizeof k, "\"%s\":", obj);
    const char *p = strstr(man, k);
    if (!p)
        return -1;
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *e = strchr(p, '}');
    p = strstr(p, k);
    return p && (!e || p < e) ? strtod(p + strlen(k), NULL) : -1;
}

#define BF16_SLACK 1.25

static double logsig(double x) { return fmin(x, 0) - log1p(exp(-fabs(x))); }

/* a categorical's log-softmax at k over n logits, in double */
static double logsm(const float *l, int n, int k)
{
    double mx = -INFINITY, se = 0;
    for (int j = 0; j < n; j++)
        mx = fmax(mx, l[j]);
    for (int j = 0; j < n; j++)
        se += exp(l[j] - mx);
    return l[k] - mx - log(se);
}

/* the log-probability of action a under logits (cam, keys, and the hotbar's hot with NETH_NET_HOTBAR), in double */
static double logprob(const float *cam, const float *keys, const float *hot, const int32_t *a)
{
    double lp = 0;
    for (int h = 0; h < NETH_NET_CAM; h++)
        lp += logsm(cam + h * NETH_NET_BINS, NETH_NET_BINS, a[h]);
    for (int k = 0; k < NETH_NET_KEYS; k++)
        lp += a[NETH_NET_CAM + k] ? logsig(keys[k]) : logsig(-(double)keys[k]);
    if (NHOT)
        lp += logsm(hot, NETH_NET_HOT, a[HOTA]);
    return lp;
}

/* a Gumbel-max draw's margin: the top two of l - log(-log u) over n, in double */
static double gmargin(const float *l, const float *u, int n)
{
    double g1 = -INFINITY, g2 = -INFINITY;
    for (int j = 0; j < n; j++) {
        double g = l[j] - log(-log((double)u[j]));
        if (g > g1) {
            g2 = g1;
            g1 = g;
        } else if (g > g2)
            g2 = g;
    }
    return g1 - g2;
}

typedef struct {
    int mism, near, far;
    double lpmax;
} acts_cmp;

/* the actions against the reference's, the near ties (the reference's margin under TOL) apart */
static acts_cmp cmp_actions(int n, const outs *o, const int32_t *ract, const float *rlogp, const float *rcam,
                            const float *rkeys, const float *rhot, const float *ucam, const float *ukey,
                            const float *uhot, int verbose)
{
    acts_cmp c = {0, 0, 0, 0};
    for (int b = 0; b < n; b++) {
        int differs = 0, tie = 1;
        for (int h = 0; h < NETH_NET_CAM; h++) {
            if (o->act[b * NETH_NET_ACTION + h] == ract[b * NETH_NET_ACTION + h])
                continue;
            differs = 1;
            double g1 = -INFINITY, g2 = -INFINITY;
            for (int j = 0; j < NETH_NET_BINS; j++) {
                size_t e = (size_t)b * NCAM + h * NETH_NET_BINS + j;
                double g = rcam[e] - log(-log((double)ucam[e]));
                if (g > g1) {
                    g2 = g1;
                    g1 = g;
                } else if (g > g2)
                    g2 = g;
            }
            if (g1 - g2 >= TOL)
                tie = 0;
            if (verbose)
                printf("  sample %d camera %d: %d, the reference %d (its margin %.3g)\n", b, h,
                       o->act[b * NETH_NET_ACTION + h], ract[b * NETH_NET_ACTION + h], g1 - g2);
        }
        for (int k = 0; k < NETH_NET_KEYS; k++) {
            if (o->act[b * NETH_NET_ACTION + NETH_NET_CAM + k] == ract[b * NETH_NET_ACTION + NETH_NET_CAM + k])
                continue;
            differs = 1;
            double m = fabs(ukey[b * NETH_NET_KEYS + k] - 1 / (1 + exp(-(double)rkeys[b * NETH_NET_KEYS + k])));
            if (m >= TOL)
                tie = 0;
            if (verbose)
                printf("  sample %d key %d: %d, the reference %d (its margin %.3g)\n", b, k,
                       o->act[b * NETH_NET_ACTION + NETH_NET_CAM + k], ract[b * NETH_NET_ACTION + NETH_NET_CAM + k], m);
        }
        if (NHOT && o->act[b * NETH_NET_ACTION + HOTA] != ract[b * NETH_NET_ACTION + HOTA]) {
            differs = 1;
            double m = gmargin(rhot + (size_t)b * NHOT, uhot + (size_t)b * NHOT, NETH_NET_HOT);
            if (m >= TOL)
                tie = 0;
            if (verbose)
                printf("  sample %d hotbar: %d, the reference %d (its margin %.3g)\n", b, o->act[b * NETH_NET_ACTION + HOTA],
                       ract[b * NETH_NET_ACTION + HOTA], m);
        }
        if (differs) {
            c.mism++;
            if (tie)
                c.near++;
            else
                c.far++;
        } else {
            double d = fabs((double)o->logp[b] - rlogp[b]);
            if (!(d <= c.lpmax))
                c.lpmax = isnan(d) ? INFINITY : d;
        }
    }
    return c;
}

int main(int argc, char **argv)
{
    char def[4096];
    const char *croot = getenv("NETHER_CACHE");
    snprintf(def, sizeof def, "%s/native/actor-a05-070", (croot && croot[0]) ? croot : ".cache");
    const char *dir = def;
    /* lane/audit-fixes: --temp T and --greedy keys|cam|both, the draw (actor.h neth_actor_set_draw) as ntloop's flags
     * set it, so the old .tN and .gk/.gc/.gkc builds' output can be reproduced on the default build */
    float itemp = 1.0f;
    int greedy = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--temp") && i + 1 < argc) {
            char *end = NULL;
            float t = strtof(argv[++i], &end);
            if (!argv[i][0] || *end || !(t > 0.0f) || isinf(t)) {
                fprintf(stderr, "actor_check: --temp T, a number over 0 (%s)\n", argv[i]);
                return 2;
            }
            itemp = neth_actor_itemp(t);
        } else if (!strcmp(argv[i], "--greedy") && i + 1 < argc) {
            const char *g = argv[++i];
            greedy = !strcmp(g, "keys") ? 1 : !strcmp(g, "cam") ? 2 : !strcmp(g, "both") ? 3 : -1;
            if (greedy < 0) {
                fprintf(stderr, "actor_check: --greedy keys|cam|both (%s)\n", g);
                return 2;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: actor_check [--temp T] [--greedy keys|cam|both] [FIXTURE_DIR]\n");
            return 2;
        } else
            dir = argv[i];
    }
    char p[8192];
    snprintf(p, sizeof p, "%s/obs.u8", dir);
    FILE *f = fopen(p, "rb");
    if (!f) {
        fprintf(stderr, "actor_check: no fixture at %s\n", dir);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    int n = (int)(ftell(f) / (NETH_NET_H * NETH_NET_W * NETH_NET_C));
    fclose(f);
    uint8_t *obs = slurp(dir, "obs.u8", (size_t)n * NETH_NET_H * NETH_NET_W * NETH_NET_C);
    float *sc = slurp(dir, "sc.f32", (size_t)n * NETH_NET_NS * 4), *rcam = slurp(dir, "cam.f32", (size_t)n * NCAM * 4),
          *rkeys = slurp(dir, "keys.f32", (size_t)n * NETH_NET_KEYS * 4), *rvalue = slurp(dir, "value.f32", n * 4),
          *ucam = slurp(dir, "ucam.f32", (size_t)n * NCAM * 4), *ukey = slurp(dir, "ukey.f32", (size_t)n * NETH_NET_KEYS * 4),
          *rlogp = slurp(dir, "logp.f32", n * 4);
    int32_t *ract = slurp(dir, "act.i32", (size_t)n * NETH_NET_ACTION * 4);
    float *rhot = NHOT ? slurp(dir, "hot.f32", (size_t)n * NHOT * 4) : NULL,
          *uhot = NHOT ? slurp(dir, "uhot.f32", (size_t)n * NHOT * 4) : NULL;
    snprintf(p, sizeof p, "%s/weights.ntw", dir);
    const char *err = NULL;
    neth_actor *a = neth_actor_load(p, &err);
    if (!a) {
        fprintf(stderr, "actor_check: %s: %s\n", p, err);
        return 2;
    }
    neth_actor_set_draw(a, itemp, greedy);
    printf("actor_check %s: %s, %d samples\n", NETH_NET_VARIANT, dir, n);
    neth_actor_ws *w = neth_actor_ws_new(a, 512, 1);
    cudaStream_t s;
    CK(cudaStreamCreateWithFlags(&s, cudaStreamNonBlocking));
    uint8_t *dobs = dmalloc((size_t)n * NETH_NET_H * NETH_NET_W * NETH_NET_C);
    neth_sc_t *dsc = dmalloc((size_t)n * NETH_NET_NS * sizeof(neth_sc_t));
    float *ducam = dmalloc((size_t)n * NCAM * 4), *dukey = dmalloc((size_t)n * NETH_NET_KEYS * 4);
    CK(cudaMemcpy(dobs, obs, (size_t)n * NETH_NET_H * NETH_NET_W * NETH_NET_C, cudaMemcpyHostToDevice));
#if NETH_SC_BF16
    {
        size_t nsc = (size_t)n * NETH_NET_NS;
        neth_sc_t *hbf = malloc(nsc * sizeof(neth_sc_t));
        for (size_t i = 0; i < nsc; i++) hbf[i] = neth_f32_to_bf16_host(sc[i]);
        CK(cudaMemcpy(dsc, hbf, nsc * sizeof(neth_sc_t), cudaMemcpyHostToDevice));
        free(hbf);
    }
#else
    CK(cudaMemcpy(dsc, sc, (size_t)n * NETH_NET_NS * 4, cudaMemcpyHostToDevice));
#endif
    CK(cudaMemcpy(ducam, ucam, (size_t)n * NCAM * 4, cudaMemcpyHostToDevice));
    CK(cudaMemcpy(dukey, ukey, (size_t)n * NETH_NET_KEYS * 4, cudaMemcpyHostToDevice));
    if (NHOT) {
        float *d = dmalloc((size_t)n * NHOT * 4);
        CK(cudaMemcpy(d, uhot, (size_t)n * NHOT * 4, cudaMemcpyHostToDevice));
        duhot = d;
    }
    outs o = {malloc((size_t)n * NETH_NET_ACTION * 4), malloc(n * 4), malloc(n * 4), malloc((size_t)n * NCAM * 4),
              malloc((size_t)n * NETH_NET_KEYS * 4), malloc((size_t)n * (NHOT ? NHOT : 1) * 4)};
    int fail = 0;
#if NETH_NET_RNN
    {
        char *mt = slurp_text(dir, "manifest.json"), *q = mt ? strstr(mt, "\"steps\":") : NULL;
        if (!q) {
            fprintf(stderr, "actor_check: %s is not a recurrent fixture (no steps in its manifest)\n", dir);
            return 2;
        }
        rs = atoi(q + 8);
        rne = n / rs;
        free(mt);
    }
    float *hN = slurp(dir, "hN.f32", (size_t)rne * NETH_NET_D * 4), *hh = malloc((size_t)rne * NETH_NET_D * 4);
    float *h0 = slurp(dir, "h0.f32", (size_t)rne * NETH_NET_D * 4);
    uint8_t *fst = slurp(dir, "first.u8", (size_t)n);
    rdh = dmalloc((size_t)rne * NETH_NET_D * 4), rdh0 = dmalloc((size_t)rne * NETH_NET_D * 4), rdfirst = dmalloc((size_t)n);
    CK(cudaMemcpy(rdh0, h0, (size_t)rne * NETH_NET_D * 4, cudaMemcpyHostToDevice));
    CK(cudaMemcpy(rdfirst, fst, (size_t)n, cudaMemcpyHostToDevice));
    int starts = 0;
    for (int i = 0; i < n; i++)
        starts += fst[i] != 0;
    printf("recurrent: %d steps of %d envs, the state %d wide from h0.f32, %d episode starts\n", rs, rne, NETH_NET_D, starts);
#endif

    /* fp32 on the fixture's noise */
    run(w, n, dobs, dsc, ducam, dukey, &o, s);
#if NETH_NET_RNN
    {
        CK(cudaMemcpy(hh, rdh, (size_t)rne * NETH_NET_D * 4, cudaMemcpyDeviceToHost));
        double dh = maxdiff(hh, hN, (size_t)rne * NETH_NET_D);
        int okh = dh <= TOL;
        printf("fp32 the state after %d steps: max|d| %.3g: %s\n", rs, dh, okh ? "PASS" : "FAIL");
        fail |= !okh;
    }
#endif
    double dc = maxdiff(o.cam, rcam, (size_t)n * NCAM), dk = maxdiff(o.keys, rkeys, (size_t)n * NETH_NET_KEYS),
           dv = maxdiff(o.value, rvalue, n), dh = NHOT ? maxdiff(o.hot, rhot, (size_t)n * NHOT) : 0;
    acts_cmp c = cmp_actions(n, &o, ract, rlogp, rcam, rkeys, rhot, ucam, ukey, uhot, 1);
    int ok = dc <= TOL && dk <= TOL && dv <= TOL && dh <= TOL && c.far == 0 && c.lpmax <= TOL;
    printf("fp32 %d samples: logits max|d| camera %.3g keys %.3g", n, dc, dk);
    if (NHOT)
        printf(" hotbar %.3g", dh);
    printf(", value %.3g; actions %d of %d differ (%d near ties under %.0e, %d beyond); logp max|d| %.3g on the equal "
           "ones: %s\n", dv, c.mism, n, c.near, TOL, c.far, c.lpmax, ok ? "PASS" : "FAIL");
    fail |= !ok;

    /* fp32 on the device RNG */
    CK(neth_actor_ws_seed(w, 12345, 0, s));
    run(w, n, dobs, dsc, NULL, NULL, &o, s);
    double lpd = 0, kobs = 0, kexp = 0, kvar = 0, cobs = 0, cexp = 0, cvar = 0, hobs = 0, hexp = 0, hvar = 0;
    int range = 0;
    for (int b = 0; b < n; b++) {
        const int32_t *ab = o.act + (size_t)b * NETH_NET_ACTION;
        for (int h = 0; h < NETH_NET_CAM; h++)
            range |= ab[h] < 0 || ab[h] >= NETH_NET_BINS;
        for (int k = 0; k < NETH_NET_KEYS; k++)
            range |= ab[NETH_NET_CAM + k] != 0 && ab[NETH_NET_CAM + k] != 1;
        if (NHOT)
            range |= ab[HOTA] < 0 || ab[HOTA] >= NETH_NET_HOT;
        if (range)
            break;
        double d = fabs(o.logp[b] - logprob(o.cam + (size_t)b * NCAM, o.keys + (size_t)b * NETH_NET_KEYS,
                                            o.hot + (size_t)b * NHOT, ab));
        if (NHOT) {   /* the hotbar's draw against its expectation, as a camera bin's */
            const float *l = o.hot + (size_t)b * NHOT;
            double m1 = 0, m2 = 0;
            for (int j = 0; j < NETH_NET_HOT; j++) {
                double q = exp(logsm(l, NETH_NET_HOT, j));
                m1 += q * j;
                m2 += q * j * j;
            }
            hobs += ab[HOTA];
            hexp += m1;
            hvar += m2 - m1 * m1;
        }
        if (!(d <= lpd))
            lpd = isnan(d) ? INFINITY : d;
        for (int k = 0; k < NETH_NET_KEYS; k++) {
            double q = 1 / (1 + exp(-(double)o.keys[b * NETH_NET_KEYS + k]));
            kobs += ab[NETH_NET_CAM + k];
            kexp += q;
            kvar += q * (1 - q);
        }
        for (int h = 0; h < NETH_NET_CAM; h++) {   /* the drawn bin's index against its expectation */
            const float *l = o.cam + (size_t)b * NCAM + h * NETH_NET_BINS;
            double mx = -INFINITY, se = 0, m1 = 0, m2 = 0;
            for (int j = 0; j < NETH_NET_BINS; j++)
                mx = fmax(mx, l[j]);
            for (int j = 0; j < NETH_NET_BINS; j++)
                se += exp(l[j] - mx);
            for (int j = 0; j < NETH_NET_BINS; j++) {
                double q = exp(l[j] - mx) / se;
                m1 += q * j;
                m2 += q * j * j;
            }
            cobs += ab[h];
            cexp += m1;
            cvar += m2 - m1 * m1;
        }
    }
    double kz = (kobs - kexp) / sqrt(kvar), cz = (cobs - cexp) / sqrt(cvar),
           hz = NHOT && hvar > 0 ? (hobs - hexp) / sqrt(hvar) : 0;
    int32_t *first = malloc((size_t)n * NETH_NET_ACTION * 4);
    memcpy(first, o.act, (size_t)n * NETH_NET_ACTION * 4);
    CK(neth_actor_ws_seed(w, 12345, 0, s));
    run(w, n, dobs, dsc, NULL, NULL, &o, s);
    int same = !memcmp(first, o.act, (size_t)n * NETH_NET_ACTION * 4);
    run(w, n, dobs, dsc, NULL, NULL, &o, s);
    int fresh = memcmp(first, o.act, (size_t)n * NETH_NET_ACTION * 4) != 0;
    ok = !range && lpd <= TOL && fabs(kz) < 5 && fabs(cz) < 5 && fabs(hz) < 5 && same && fresh;
    printf("fp32 device RNG: actions in range %s, logp max|d| %.3g against the drawn actions' log-probability, keys "
           "held %.0f against %.1f expected (z %.2f), camera bins' mean z %.2f", range ? "no" : "yes", lpd, kobs, kexp,
           kz, cz);
    if (NHOT)
        printf(", the hotbar's mean z %.2f", hz);
    printf(", a seed repeats %s, the next call draws anew %s: %s\n", same ? "yes" : "no", fresh ? "yes" : "no",
           ok ? "PASS" : "FAIL");
    fail |= !ok;

    /* bf16 on the device RNG (lane/ntfc: actorfused.cuh keeps the counter itself): a seed repeats, the next call
     * draws anew, and the seed's draws are fp32's but where bf16's logits moved a near tie (a counter a call off would
     * make most samples differ) */
    neth_actor_set_mode(a, NETH_NET_ACTOR_BF16);
    CK(neth_actor_ws_seed(w, 12345, 0, s));
    run(w, n, dobs, dsc, NULL, NULL, &o, s);
    int32_t *bfirst = malloc((size_t)n * NETH_NET_ACTION * 4);
    memcpy(bfirst, o.act, (size_t)n * NETH_NET_ACTION * 4);
    int agree = 0;
    for (int b = 0; b < n; b++)
        agree += !memcmp(first + (size_t)b * NETH_NET_ACTION, bfirst + (size_t)b * NETH_NET_ACTION, NETH_NET_ACTION * 4);
    CK(neth_actor_ws_seed(w, 12345, 0, s));
    run(w, n, dobs, dsc, NULL, NULL, &o, s);
    same = !memcmp(bfirst, o.act, (size_t)n * NETH_NET_ACTION * 4);
    run(w, n, dobs, dsc, NULL, NULL, &o, s);
    fresh = memcmp(bfirst, o.act, (size_t)n * NETH_NET_ACTION * 4) != 0;
    ok = same && fresh && agree >= n * 0.95;
    printf("bf16 device RNG: a seed repeats %s, the next call draws anew %s, %d of %d samples draw fp32's action: %s\n",
           same ? "yes" : "no", fresh ? "yes" : "no", agree, n, ok ? "PASS" : "FAIL");
    fail |= !ok;
    neth_actor_set_mode(a, NETH_NET_ACTOR_FP32);
    free(bfirst);
    free(first);

    /* bf16 on the fixture's noise: each output's |x - ref| / |ref| over the fixture at most BF16_SLACK times fastppo's
     * own bf16 actor's (the manifest's "bf16_normwise", ntref.py actor); a fixture without it is measured only */
    neth_actor_set_mode(a, NETH_NET_ACTOR_BF16);
    run(w, n, dobs, dsc, ducam, dukey, &o, s);
    dc = maxdiff(o.cam, rcam, (size_t)n * NCAM);
    dk = maxdiff(o.keys, rkeys, (size_t)n * NETH_NET_KEYS);
    dv = maxdiff(o.value, rvalue, n);
    c = cmp_actions(n, &o, ract, rlogp, rcam, rkeys, rhot, ucam, ukey, uhot, 0);
    printf("bf16: logits max|d| camera %.3g keys %.3g, value %.3g; actions %d of %d samples differ (%.2f%%); logp "
           "max|d| %.3g on the equal ones\n",
           dc, dk, dv, c.mism, n, 100.0 * c.mism / n, c.lpmax);
    char *man = slurp_text(dir, "manifest.json");
    const char *what[4] = {"cam", "keys", "value", "hot"};
    const float *ours[4] = {o.cam, o.keys, o.value, o.hot}, *ref[4] = {rcam, rkeys, rvalue, rhot};
    size_t len[4] = {(size_t)n * NCAM, (size_t)n * NETH_NET_KEYS, (size_t)n, (size_t)n * NHOT};
    for (int i = 0; i < 3 + NETH_NET_HOTBAR; i++) {
        double ne = normerr(ours[i], ref[i], len[i]), tb = man ? manifest_num(man, "bf16_normwise", what[i]) : -1;
        if (tb < 0) {
            printf("bf16 %-5s normwise %.3e (the fixture has no reference bf16 error: not gated)\n", what[i], ne);
            continue;
        }
        ok = ne <= BF16_SLACK * tb + 1e-7;
        printf("bf16 %-5s normwise %.3e, fastppo's own bf16 %.3e (at most %.2fx): %s\n", what[i], ne, tb, BF16_SLACK,
               ok ? "PASS" : "FAIL");
        fail |= !ok;
    }
#if NETH_NET_RNN
    {
        CK(cudaMemcpy(hh, rdh, (size_t)rne * NETH_NET_D * 4, cudaMemcpyDeviceToHost));
        double ne = normerr(hh, hN, (size_t)rne * NETH_NET_D), tb = man ? manifest_num(man, "bf16_normwise", "h") : -1;
        ok = tb >= 0 && ne <= BF16_SLACK * tb + 1e-7;
        printf("bf16 h     normwise %.3e, fastppo's own bf16 %.3e (at most %.2fx): %s\n", ne, tb, BF16_SLACK,
               ok ? "PASS" : "FAIL");
        fail |= !ok;
    }
#endif
    free(man);
    neth_actor_set_mode(a, NETH_NET_ACTOR_FP32);

    /* neth_actor_reload: an actor loaded from the negated weights, reloaded on the device from the fixture's weights in
     * the learner's layout (learn.h), is byte for byte the fixture's own load */
    {
        snprintf(p, sizeof p, "%s/weights.ntw", dir);
        f = fopen(p, "rb");
        fseek(f, 0, SEEK_END);
        size_t blen = (size_t)ftell(f);
        fclose(f);
        unsigned char *blob = slurp(dir, "weights.ntw", blen), *neg = malloc(blen);
        memcpy(neg, blob, blen);
        float *flat = malloc((size_t)NETH_LEARN_NPARAM * 4);
        const float *t[NETH_NET_TENSORS];
        size_t off = 12;
        for (int i = 0; i < NETH_NET_TENSORS; i++) {   /* neth_actor_load_blob checked the shapes */
            int32_t r, d;
            size_t k = 1;
            memcpy(&r, blob + off, 4);
            off += 4;
            for (int j = 0; j < r; j++, off += 4) {
                memcpy(&d, blob + off, 4);
                k *= (size_t)d;
            }
            t[i] = (const float *)(blob + off);
            for (size_t j = 0; j < k; j++)
                ((float *)(neg + off))[j] = -t[i][j];
            off += 4 * k;
        }
#define NETH_NET_FL(name, i, k) memcpy(flat + NETH_LEARN_OFF(name), t[i], 4 * (size_t)(k));
        NETH_LEARN_LAYOUT(NETH_NET_FL)
#undef NETH_NET_FL
        neth_actor *b = neth_actor_load_blob(neg, blen, &err);
        float *dflat = dmalloc((size_t)NETH_LEARN_NPARAM * 4);
        CK(cudaMemcpy(dflat, flat, (size_t)NETH_LEARN_NPARAM * 4, cudaMemcpyHostToDevice));
        long before = b ? neth_actor_diff(a, b) : -1;
        int rc = b ? neth_actor_reload(b, dflat, s) : -1;
        CK(cudaStreamSynchronize(s));
        long after = b ? neth_actor_diff(a, b) : -1;
        ok = b && rc == 0 && before > 0 && after == 0;
        printf("reload: the negated weights' image differs in %ld bytes, after neth_actor_reload from the learner's "
               "layout %ld: %s\n", before, after, ok ? "PASS" : "FAIL");
        fail |= !ok;
        neth_actor_free(b);
        cudaFree(dflat);
        free(flat);
        free(neg);
        free(blob);
    }

    printf("actor-check: %s\n", fail ? "FAIL" : "PASS");
    neth_actor_ws_free(w);
    neth_actor_free(a);
    return fail;
}
