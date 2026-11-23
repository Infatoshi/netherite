/* ntinit (lane/polish): a freshly initialised net (net.h's variant, built with its -D flags) as a .ntw blob, so the
 * native trainer starts on a fresh host without mcsr-speedrun's Python (scripts/ntref.py's fresh:wm=W[,stem=1]).
 *
 *   out/runtime/train/[V/]ntinit OUT.ntw [SEED]    the blob, SEED (default 1) the draws'
 *   out/runtime/train/[V/]ntinit OUT.ntw [SEED] --from BASE.ntw   BASE's tensors, the ones it lacks drawn (lane/nthotbar:
 *                                                  a hotbar net (V.hb) from a net without the head, the rest equal)
 *   out/runtime/train/[V/]ntinit --stats FILE.ntw  each tensor's count, min, max, mean and standard deviation
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
static const struct tensor T[NETH_NET_TENSORS] = {NETH_NET_ORDER(NETH_NET_T)};

static long count(const struct tensor *t)
{
    long n = 1;
    for (int k = 0; k < t->rank; ++k) n *= t->d[k];
    return n;
}

static int put32(FILE *f, int32_t v) { return fwrite(&v, 4, 1, f) == 1 ? 0 : -1; }

/* --from BASE.ntw (lane/nthotbar): BASE's tensors in place of the draws, each of the build's first ones whose rank and
 * dims BASE's same tensor has (a net without the hotbar's head, which is drawn as above); NULL with a message */
static unsigned char *from_blob(const char *path, long *len, int32_t *cnt)
{
    FILE *f = fopen(path, "rb");
    if (!f) return perror(path), NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)*len);
    int ok = *len >= 12 && fread(b, 1, (size_t)*len, f) == (size_t)*len && !memcmp(b, "NTW1", 4);
    fclose(f);
    if (!ok) return fprintf(stderr, "ntinit: %s is not a .ntw blob\n", path), free(b), NULL;
    memcpy(cnt, b + 8, 4);
    return b;
}

static int write_blob(const char *path, uint64_t seed, const char *from)
{
    rng = seed * 0x2545f4914f6cdd1dull + 1;
    long blen = 0, boff = 12;
    int32_t bcnt = 0, copied = 0;
    unsigned char *base = from ? from_blob(from, &blen, &bcnt) : NULL;
    if (from && !base) return 1;
    FILE *f = fopen(path, "wb");
    if (!f) return perror(path), 1;
    int bad = fwrite("NTW1\0\0\0\0", 1, 8, f) != 8 || put32(f, NETH_NET_TENSORS);
    double bound = 0;
    for (int i = 0; i < NETH_NET_TENSORS && !bad; ++i)
    {
        const struct tensor *t = &T[i];
        long n = count(t);
        float *x = malloc((size_t)n * sizeof *x);
        /* a weight's fan_in: its dims past the first; a bias takes its weight's (the tensor before it) */
        if (t->rank > 1)
        {
            long fan = n / t->d[0];
            bound = 1.0 / sqrt((double)fan);
        }
        for (long j = 0; j < n; ++j) x[j] = (float)((2.0 * uniform() - 1.0) * bound);
        int head_w = !strcmp(t->name, "cam_w") || !strcmp(t->name, "keys_w") || !strcmp(t->name, "v_w") ||
                     !strcmp(t->name, "hot_w");
        if (head_w)
            for (long j = 0; j < n; ++j) x[j] *= 0.01f;
        if (!strcmp(t->name, "hot_b"))  /* NETH_NET_HOTBAR: keep favoured (net.h NETH_NET_HOT_KEEP), the slots 0 */
            for (long j = 0; j < n; ++j) x[j] = j == 0 ? NETH_NET_HOT_KEEP : 0.0f;
        if (!strcmp(t->name, "cam_b"))
            for (long j = 0; j < n; ++j) x[j] = -0.5f * fabsf((float)(j % NETH_NET_BINS) - (float)(NETH_NET_BINS / 2));
        if (!strcmp(t->name, "keys_b"))
            for (long j = 0; j < n; ++j) x[j] = KEY_BIAS[j];
        /* NETH_NET_RNN (lane/ladder8): net.h's fresh start, the candidate's half (rnn_w's rows D..2D-1, rnn_b's D..2D-1)
         * zero, so h stays 0 and the net is the feed-forward one until training moves it */
        if (!strcmp(t->name, "rnn_w") || !strcmp(t->name, "rnn_b"))
            for (long j = n / 2; j < n; ++j) x[j] = 0.0f;
        if (base && i < bcnt)   /* --from: BASE's tensor i, its header the build's own (fc1_w supports scalar prefix widening) */
        {
            int32_t r = 0, d = 0, narrow = 0;
            int same = boff + 4 <= blen && (memcpy(&r, base + boff, 4), r == t->rank);
            for (int k = 0; same && k < r; ++k)
            {
                same = boff + 8 + 4 * k <= blen;
                if (!same) break;
                memcpy(&d, base + boff + 4 + 4 * k, 4);
                if (r == 2 && k == 1 && !strcmp(t->name, "fc1_w") && d < t->d[1] &&
                    d > NETH_NET_FLAT - NETH_NET_NS && NETH_NET_NS_PREFIX(d - (NETH_NET_FLAT - NETH_NET_NS)))
                    narrow = d;
                else if (d != t->d[k])
                    same = 0;
            }
            long bn = narrow ? (long)t->d[0] * (long)narrow : n;
            same = same && boff + 4 + 4 * r + 4 * bn <= blen;
            if (!same)
            {
                fprintf(stderr, "ntinit: %s's tensor %d is not %s's shape\n", from, i, t->name);
                fclose(f);
                return 1;
            }
            if (narrow)
            {
                memset(x, 0, (size_t)n * sizeof *x);
                for (int row = 0; row < t->d[0]; ++row)
                    memcpy(x + (size_t)row * (size_t)t->d[1], base + boff + 4 + 4 * r + 4 * (size_t)row * (size_t)narrow,
                           (size_t)narrow * sizeof *x);
            }
            else
                memcpy(x, base + boff + 4 + 4 * r, (size_t)n * sizeof *x);
            boff += 4 + 4 * r + 4 * bn;
            ++copied;
        }
        bad |= put32(f, t->rank);
        for (int k = 0; k < t->rank; ++k) bad |= put32(f, t->d[k]);
        bad |= fwrite(x, sizeof *x, (size_t)n, f) != (size_t)n;
        free(x);
    }
    if (fclose(f) || bad) return fprintf(stderr, "ntinit: cannot write %s\n", path), 1;
    if (base && (bcnt > NETH_NET_TENSORS || boff != blen))
        return fprintf(stderr, "ntinit: %s has %d tensors, more than the build's %d or trailing bytes\n", from, bcnt,
                       NETH_NET_TENSORS), 1;
    printf("ntinit: %s, a fresh %s net, %d tensors, seed %llu", path, NETH_NET_VARIANT, NETH_NET_TENSORS, (unsigned long long)seed);
    if (base) printf(", the first %d from %s", copied, from);
    printf("\n");
    free(base);
    return 0;
}

static int stats(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return perror(path), 1;
    char magic[8];
    int32_t nt = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "NTW1", 4) || fread(&nt, 4, 1, f) != 1)
        return fprintf(stderr, "ntinit: %s is not a .ntw blob\n", path), 1;
    for (int i = 0; i < nt; ++i)
    {
        int32_t r = 0, d[8];
        long n = 1;
        if (fread(&r, 4, 1, f) != 1 || r < 1 || r > 8 || fread(d, 4, (size_t)r, f) != (size_t)r)
            return fprintf(stderr, "ntinit: %s: tensor %d's header\n", path, i), 1;
        for (int k = 0; k < r; ++k) n *= d[k];
        float *x = malloc((size_t)n * sizeof *x);
        if (fread(x, sizeof *x, (size_t)n, f) != (size_t)n) return fprintf(stderr, "ntinit: %s: cut\n", path), 1;
        double lo = INFINITY, hi = -INFINITY, s = 0, s2 = 0;
        for (long j = 0; j < n; ++j) lo = fmin(lo, x[j]), hi = fmax(hi, x[j]), s += x[j], s2 += (double)x[j] * x[j];
        double m = s / (double)n;
        printf("%2d %-8s %8ld  min %+.6f  max %+.6f  mean %+.6f  sd %.6f\n", i, i < NETH_NET_TENSORS ? T[i].name : "?", n, lo,
               hi, m, sqrt(fmax(0, s2 / (double)n - m * m)));
        free(x);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--stats")) return stats(argv[2]);
    const char *from = NULL;
    if (argc >= 4 && !strcmp(argv[argc - 2], "--from")) from = argv[argc - 1], argc -= 2;
    if (argc == 2 || (argc == 3 && argv[1][0] != '-'))
        return write_blob(argv[1], argc == 3 ? strtoull(argv[2], NULL, 10) : 1, from);
    fprintf(stderr, "usage: ntinit OUT.ntw [SEED] [--from BASE.ntw] | --stats FILE.ntw   (%s)\n", NETH_NET_VARIANT);
    return 2;
}
