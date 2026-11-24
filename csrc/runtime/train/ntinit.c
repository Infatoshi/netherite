/* ntinit (lane/polish): a freshly initialised net (net.h's variant, built with its -D flags) as a .ntw blob, so the
 * native trainer starts on a fresh host without mcsr-speedrun's Python (scripts/ntref.py's fresh:wm=W[,stem=1]).
 *
 *   out/runtime/train/[V/]ntinit OUT.ntw [SEED]    the blob, SEED (default 1) the draws'
 *   ... [--features WORDS]                         lane/audit-feat: the net's scalar features (ls hs dk or gv tk, or
 *                                                  none; spaces, commas or dots between), else V's default (its words)
 *   out/runtime/train/[V/]ntinit OUT.ntw [SEED] --from BASE.ntw   BASE's tensors, the ones it lacks drawn (lane/nthotbar:
 *                                                  a hotbar net (V.hb) from a net without the head, the rest equal)
 *   out/runtime/train/[V/]ntinit --stats FILE.ntw  each tensor's count, min, max, mean and standard deviation
 *   out/runtime/train/[V/]ntinit --migrate IN.ntw OUT.ntw --features WORDS
 *                                                  lane/audit-fixes: an NTW1 file (net.h: it does not say which inputs
 *                                                  it reads) as NTW2, WORDS its features as the caller states them
 *                                                  (ls hs dk or gv tk, or none; spaces, commas or dots between), held
 *                                                  to 155 and their widths being the file's scalars; its tensors as
 *                                                  they are. The only way a feature-bearing NTW1 file loads
 *
 * lane/audit-fixes: every blob is net.h's NTW2 (neth_checkpoint.c neth_ntw_write: the build's variant, the features
 * --features names or the build's default), and --from,
 * --stats and --migrate read through neth_checkpoint.c's parser (built in with -DNETH_NTW_ONLY), NTW2 and NTW1.
 *
 * The initialisation is fastppo.py Small's (mcsr-speedrun): PyTorch's default for every Conv2d and Linear, weight and
 * bias each uniform on [-1/sqrt(fan_in), 1/sqrt(fan_in)] (kaiming_uniform_ with a = sqrt(5); fan_in the input
 * channels times the kernel's area, or the input width), then Small's calm start: the camera, keys and value heads'
 * weights times 0.01, the camera's bias -0.5 |bin - 20| for each of its two 41-bin heads (so a small turn is likelier
 * than a large one), the keys' bias forward 1, jump -2, sprint -1, mine -1, hit -3 and -4 for the others (pairs.KEYS'
 * order). The value's bias stays PyTorch's draw. NETH_NET_HOTBAR's head (lane/nthotbar): its weights times 0.01 too, its
 * bias NETH_NET_HOT_KEEP for keep and 0 for each slot. The same distribution as torch's, not its bytes: the draws are
 * splitmix64's (torch.manual_seed's stream is not reproduced), so a fresh blob here and ntref.py's differ but start
 * from the same place. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"

/* lane/audit-fixes: neth_checkpoint.c's (neth_pipe_internal.h declares them for ntloop) */
int neth_ntw_parse(const void *data, size_t len, const char *name, int flags, float *t[NETH_NET_TENSORS],
                   int dims[NETH_NET_TENSORS][4], char *err, size_t en);
unsigned char *neth_ntw_write(const float *const t[NETH_NET_TENSORS], size_t *len);
unsigned char *neth_ntw_migrate(const void *data, size_t len, const char *name, const char *words, size_t *olen,
                                char *err, size_t en);
long neth_ntw_info(const void *data, size_t len, const char *name, char *variant, size_t vn, char *features, size_t fn,
                   int *scalars, char *err, size_t en);
int neth_net_words(const char *words, int *mask, char *err, size_t en);

static uint64_t rng;

static double uniform(void)
{
    uint64_t z = (rng += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    return (double)(z >> 11) * 0x1.0p-53;
}

/* pairs.KEYS: forward back left right sprint jump sneak mine place throw drop hit swap_item paused */
static const float KEY_BIAS[NETH_NET_KEYS] = {1, -4, -4, -4, -1, -2, -4, -1, -4, -4, -4, -3, -4, -4};

struct tensor { const char *name; int rank, d[4]; };
#define NETH_NET_T(name, r, a, b, c, e) {#name, r, {a, b, c, e}},
/* lane/audit-feat: the tensors at the run's feature set (fc1's columns: net.h neth_net_fs, set before this is made) */
#define NTINIT_T const struct tensor T[NETH_NET_TENSORS] = {NETH_NET_ORDER(NETH_NET_T)}

static long count(const struct tensor *t)
{
    long n = 1;
    for (int k = 0; k < t->rank; ++k) n *= t->d[k];
    return n;
}

/* a whole file (NULL with a message) */
static unsigned char *readall(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return perror(path), NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = n > 0 ? malloc((size_t)n) : NULL;
    int ok = b && fread(b, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok) return fprintf(stderr, "ntinit: cannot read %s\n", path), free(b), NULL;
    *len = (size_t)n;
    return b;
}

static int write_file(const char *path, const unsigned char *b, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return perror(path), 1;
    int bad = fwrite(b, 1, len, f) != len;
    if (fclose(f) || bad) return fprintf(stderr, "ntinit: cannot write %s\n", path), 1;
    return 0;
}

/* --from BASE.ntw (lane/nthotbar): BASE's tensors in place of the draws, each of the build's first ones (a net without
 * the hotbar's head, which is drawn as below). lane/audit-fixes: read by neth_checkpoint.c's parser (NTW2 or NTW1,
 * net.h's rules: fc1's scalars widened by the features BASE names), the tensors it holds at the build's shapes
 * (lane/audit-feat: fc1 at the net's features, which hold BASE's) */
static int write_blob(const char *path, uint64_t seed, const char *from)
{
    NTINIT_T;
    rng = seed * 0x2545f4914f6cdd1dull + 1;
    float *base[NETH_NET_TENSORS];
    int copied = 0;
    if (from)
    {
        size_t blen;
        unsigned char *b = readall(from, &blen);
        if (!b) return 1;
        char err[1024];
        copied = neth_ntw_parse(b, blen, from, NETH_NTW_FEWER, base, NULL, err, sizeof err);
        free(b);
        if (copied < 0) return fprintf(stderr, "ntinit: --from %s\n", err), 1;
    }
    float *x[NETH_NET_TENSORS];
    double bound = 0;
    for (int i = 0; i < NETH_NET_TENSORS; ++i)
    {
        const struct tensor *t = &T[i];
        long n = count(t);
        x[i] = malloc((size_t)n * sizeof *x[i]);
        float *xi = x[i];
        /* a weight's fan_in: its dims past the first; a bias takes its weight's (the tensor before it) */
        if (t->rank > 1)
        {
            long fan = n / t->d[0];
            bound = 1.0 / sqrt((double)fan);
        }
        for (long j = 0; j < n; ++j) xi[j] = (float)((2.0 * uniform() - 1.0) * bound);
        int head_w = !strcmp(t->name, "cam_w") || !strcmp(t->name, "keys_w") || !strcmp(t->name, "v_w") ||
                     !strcmp(t->name, "hot_w");
        if (head_w)
            for (long j = 0; j < n; ++j) xi[j] *= 0.01f;
        if (!strcmp(t->name, "hot_b"))  /* NETH_NET_HOTBAR: keep favoured (net.h NETH_NET_HOT_KEEP), the slots 0 */
            for (long j = 0; j < n; ++j) xi[j] = j == 0 ? NETH_NET_HOT_KEEP : 0.0f;
        if (!strcmp(t->name, "cam_b"))
            for (long j = 0; j < n; ++j) xi[j] = -0.5f * fabsf((float)(j % NETH_NET_BINS) - (float)(NETH_NET_BINS / 2));
        if (!strcmp(t->name, "keys_b"))
            for (long j = 0; j < n; ++j) xi[j] = KEY_BIAS[j];
        /* NETH_NET_RNN (lane/ladder8): net.h's fresh start, the candidate's half (rnn_w's rows D..2D-1, rnn_b's D..2D-1)
         * zero, so h stays 0 and the net is the feed-forward one until training moves it */
        if (!strcmp(t->name, "rnn_w") || !strcmp(t->name, "rnn_b"))
            for (long j = n / 2; j < n; ++j) xi[j] = 0.0f;
        if (i < copied)   /* --from: BASE's tensor i */
        {
            memcpy(xi, base[i], (size_t)n * sizeof *xi);
            free(base[i]);
        }
    }
    size_t len;
    unsigned char *b = neth_ntw_write((const float *const *)x, &len);
    for (int i = 0; i < NETH_NET_TENSORS; ++i) free(x[i]);
    int bad = write_file(path, b, len);
    free(b);
    if (bad) return 1;
    printf("ntinit: %s, a fresh %s net, %d tensors, seed %llu", path, NETH_NET_VARIANT, NETH_NET_TENSORS, (unsigned long long)seed);
    if (from) printf(", the first %d from %s", copied, from);
    printf("\n");
    return 0;
}

/* --stats: the header (NTW2's variant, features and scalars), then each tensor's numbers */
static int stats(const char *path)
{
    NTINIT_T;
    size_t len;
    unsigned char *b = readall(path, &len);
    if (!b) return 1;
    char variant[128], features[64], err[512];
    int scalars;
    long off = neth_ntw_info(b, len, path, variant, sizeof variant, features, sizeof features, &scalars, err, sizeof err);
    if (off < 0) return fprintf(stderr, "ntinit: %s\n", err), free(b), 1;
    if (scalars < 0) printf("%s: NTW1 (no header: its features unsaid)\n", path);
    else printf("%s: NTW2, variant %s, features %s, scalars %d\n", path, variant, features, scalars);
    int32_t nt;
    memcpy(&nt, b + off, 4);
    size_t at = (size_t)off + 4;
    for (int i = 0; i < nt; ++i)
    {
        int32_t r = 0, d[8];
        long n = 1;
        if (at + 4 > len || (memcpy(&r, b + at, 4), r < 1 || r > 8) || at + 4 + 4 * (size_t)r > len)
            return fprintf(stderr, "ntinit: %s: tensor %d's header\n", path, i), free(b), 1;
        memcpy(d, b + at + 4, 4 * (size_t)r);
        at += 4 + 4 * (size_t)r;
        for (int k = 0; k < r; ++k) n *= d[k];
        if (n < 1 || (size_t)n > (len - at) / 4) return fprintf(stderr, "ntinit: %s: cut\n", path), free(b), 1;
        float *x = malloc((size_t)n * sizeof *x);
        memcpy(x, b + at, (size_t)n * sizeof *x);
        at += (size_t)n * sizeof *x;
        double lo = INFINITY, hi = -INFINITY, s = 0, s2 = 0;
        for (long j = 0; j < n; ++j) lo = fmin(lo, x[j]), hi = fmax(hi, x[j]), s += x[j], s2 += (double)x[j] * x[j];
        double m = s / (double)n;
        printf("%2d %-8s %8ld  min %+.6f  max %+.6f  mean %+.6f  sd %.6f\n", i, i < NETH_NET_TENSORS ? T[i].name : "?", n, lo,
               hi, m, sqrt(fmax(0, s2 / (double)n - m * m)));
        free(x);
    }
    free(b);
    return 0;
}

/* --migrate IN OUT --features WORDS (lane/audit-fixes): neth_checkpoint.c neth_ntw_migrate's */
static int migrate(const char *in, const char *out, const char *words)
{
    size_t len, olen;
    unsigned char *b = readall(in, &len);
    if (!b) return 1;
    char err[1024];
    unsigned char *o = neth_ntw_migrate(b, len, in, words, &olen, err, sizeof err);
    free(b);
    if (!o) return fprintf(stderr, "ntinit: --migrate %s\n", err), 1;
    int bad = write_file(out, o, olen);
    char variant[128], features[64];
    int scalars = 0;
    if (!bad) neth_ntw_info(o, olen, out, variant, sizeof variant, features, sizeof features, &scalars, err, sizeof err);
    free(o);
    if (bad) return 1;
    printf("ntinit: %s as NTW2 in %s: variant %s, features %s, scalars %d\n", in, out, variant, features, scalars);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--stats")) return stats(argv[2]);
    if (argc == 6 && !strcmp(argv[1], "--migrate") && !strcmp(argv[4], "--features")) return migrate(argv[2], argv[3], argv[5]);
    const char *from = NULL, *feat = NULL;
    /* the trailing --from BASE and (lane/audit-feat) --features WORDS, in either order */
    while (argc >= 4 && argv[1][0] != '-' && (!strcmp(argv[argc - 2], "--from") || !strcmp(argv[argc - 2], "--features")))
    {
        const char **p = !strcmp(argv[argc - 2], "--from") ? &from : &feat;
        if (*p) break;
        *p = argv[argc - 1], argc -= 2;
    }
    if (feat)
    {
        int m;
        char err[256];
        if (neth_net_words(feat, &m, err, sizeof err)) return fprintf(stderr, "ntinit: --features %s: %s\n", feat, err), 2;
        neth_net_set_features(m);
    }
    if (argc == 2 || (argc == 3 && argv[1][0] != '-'))
        return write_blob(argv[1], argc == 3 ? strtoull(argv[2], NULL, 10) : 1, from);
    fprintf(stderr,
            "usage: ntinit OUT.ntw [SEED] [--from BASE.ntw] [--features WORDS] | --stats FILE.ntw | --migrate IN.ntw OUT.ntw "
            "--features WORDS"
            "   (%s, %s)\n", NETH_NET_NAME, NETH_NET_VARIANT);
    return 2;
}

