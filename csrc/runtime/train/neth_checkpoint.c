/* ntloop's checkpoints: net.h's blobs into the learner's layout and back, a file copied whole, the model's
 * weights and Adam state beside it (--out's model.ntw, --init's state).
 *
 * lane/audit-fixes: and the one parser every reader of a .ntw calls (ntloop's --init and Adam state, the actor's
 * neth_actor_load_blob, the teacher's neth_distill_widen, ntinit's --from, --stats and --migrate) and the one writer
 * (NTW2 with net.h's header: the build's variant, the run's features and its scalar count; lane/audit-feat: the
 * features are the run's, net.h neth_net_fs, set before anything reads or writes). The first part needs net.h and
 * libc alone: ntinit and actor.mk's checks compile this file with -DNETH_NTW_ONLY, ntloop whole. */
#define _GNU_SOURCE
#ifdef NETH_NTW_ONLY
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"

int neth_ntw_parse(const void *data, size_t len, const char *name, int flags, float *t[NETH_NET_TENSORS],
                   int dims[NETH_NET_TENSORS][4], char *err, size_t en);
unsigned char *neth_ntw_write(const float *const t[NETH_NET_TENSORS], size_t *len);
unsigned char *neth_ntw_migrate(const void *data, size_t len, const char *name, const char *words, size_t *olen,
                                char *err, size_t en);
long neth_ntw_info(const void *data, size_t len, const char *name, char *variant, size_t vn, char *features, size_t fn,
                   int *scalars, char *err, size_t en);
int neth_net_words(const char *words, int *mask, char *err, size_t en);
void neth_net_mask_words(int mask, char *s, size_t n);
int neth_ntw_features(const void *data, size_t len, const char *name, int *mask, char *variant, size_t vn, char *err,
                      size_t en);
#else
#include "neth_pipe_internal.h"
#endif

/* ---- the header (net.h's NTW2) ---- */

static const struct
{
    const char *word;
    int bit, n;
} FEAT[] = {
#define NETH_NET_F(w, bit, n) {w, bit, n},
    NETH_NET_FEATURES(NETH_NET_F)
#undef NETH_NET_F
};
#define NFEAT ((int)(sizeof FEAT / sizeof *FEAT))
#define NETH_NET_FW(w, bit, n) +(n)
#define NETH_NET_FM(w, bit, n) | (bit)
_Static_assert(NETH_NET_NS_MAX == NETH_NET_BASE_NS NETH_NET_FEATURES(NETH_NET_FW) &&
                   NETH_NET_FALL == (0 NETH_NET_FEATURES(NETH_NET_FM)),
               "net.h's NETH_NET_FEATURES are its scalars, NETH_NET_NS_OF's");
#undef NETH_NET_FW
#undef NETH_NET_FM

/* the run's features as a mask (bit k NETH_NET_FEATURES' k-th; lane/audit-feat: net.h's neth_net_fs, the build's
 * default until ntloop or ntinit sets it) */
static int build_mask(void)
{
    return NETH_NET_FEATMASK;
}

/* a mask's scalars, and where feature k's sit in a row of them */
static int mask_ns(int m)
{
    int n = NETH_NET_BASE_NS;
    for (int k = 0; k < NFEAT; ++k)
        if (m >> k & 1) n += FEAT[k].n;
    return n;
}
static int mask_off(int m, int k)
{
    int o = NETH_NET_BASE_NS;
    for (int j = 0; j < k; ++j)
        if (m >> j & 1) o += FEAT[j].n;
    return o;
}

/* a mask's words in net.h's order, or none */
static void mask_words(int m, char *s, size_t n)
{
    size_t at = 0;
    s[0] = 0;
    for (int k = 0; k < NFEAT; ++k)
        if (m >> k & 1 && at < n) at += (size_t)snprintf(s + at, n - at, "%s%s", at ? " " : "", FEAT[k].word);
    if (!m) snprintf(s, n, "none");
}

/* words (separated by spaces, commas or dots; none alone for no feature) into a mask: 0, or -1 with err */
static int words_mask(const char *s, int *mask, char *err, size_t en)
{
    int m = 0, none = 0, words = 0;
    for (const char *p = s; *p;)
    {
        if (*p == ' ' || *p == ',' || *p == '.')
        {
            ++p;
            continue;
        }
        size_t l = strcspn(p, " ,.");
        int k = 0;
        while (k < NFEAT && !(strlen(FEAT[k].word) == l && !memcmp(FEAT[k].word, p, l))) ++k;
        if (l == 4 && !memcmp(p, "none", 4)) none = 1;
        else if (k == NFEAT)
            return snprintf(err, en, "the feature %.*s is not one of net.h's (ls hs dk or gv tk, or none)", (int)l, p), -1;
        else if (m >> k & 1)
            return snprintf(err, en, "the feature %s twice", FEAT[k].word), -1;
        else
            m |= 1 << k;
        ++words;
        p += l;
    }
    if (!words || (none && words > 1))
        return snprintf(err, en, "features \"%s\": feature words (ls hs dk or gv tk), or none alone", s), -1;
    *mask = m;
    return 0;
}

/* the header for variant and mask at d (NULL: its size alone): "NTW2", H, the text padded to a multiple of 4 */
static size_t head_write(unsigned char *d, const char *variant, int mask)
{
    char text[512], fw[64];
    mask_words(mask, fw, sizeof fw);
    int tl = snprintf(text, sizeof text, "variant %s\nfeatures %s\nscalars %d\n", variant, fw, mask_ns(mask));
    size_t h = ((size_t)tl + 3) & ~(size_t)3;
    if (d)
    {
        int32_t hh = (int32_t)h;
        memcpy(d, "NTW2", 4);
        memcpy(d + 4, &hh, 4);
        memset(d + 8, 0, h);
        memcpy(d + 8, text, (size_t)tl);
    }
    return 8 + h;
}

struct head
{
    int ver, mask, scalars;
    char variant[128];
};

/* a file's header: the offset of its tensor count, or -1 with err. NTW1's: version 1, no variant, mask 0, scalars -1
 * (it does not say) */
static long head_read(const unsigned char *b, size_t len, const char *name, struct head *h, char *err, size_t en)
{
    memset(h, 0, sizeof *h);
    h->scalars = -1;
    if (len >= 12 && !memcmp(b, "NTW1\0\0\0\0", 8)) return h->ver = 1, 8;
    if (len < 12 || memcmp(b, "NTW2", 4)) return snprintf(err, en, "%s: not a .ntw checkpoint (neither NTW2 nor NTW1)", name), -1;
    int32_t H;
    memcpy(&H, b + 4, 4);
    if (H <= 0 || H % 4 || (size_t)H > len - 12) return snprintf(err, en, "%s: NTW2's header length %d is broken", name, (int)H), -1;
    const char *t = (const char *)b + 8;
    size_t tl = 0;
    while (tl < (size_t)H && t[tl]) ++tl;
    for (size_t k = tl; k < (size_t)H; ++k)
        if (t[k]) return snprintf(err, en, "%s: NTW2's header has bytes after its NUL padding starts", name), -1;
    if (!tl || t[tl - 1] != '\n') return snprintf(err, en, "%s: NTW2's header does not end its last line", name), -1;
    int got = 0;
    for (size_t s = 0; s < tl;)
    {
        size_t e = s;
        while (t[e] != '\n') ++e;
        const char *line = t + s, *sp = memchr(line, ' ', e - s);
        size_t ll = e - s;
        s = e + 1;
        char key[16], val[256];
        if (!sp || (size_t)(sp - line) >= sizeof key || ll - (size_t)(sp - line) > sizeof val)
            return snprintf(err, en, "%s: NTW2's header line \"%.*s\" is not KEY VALUE", name, (int)ll, line), -1;
        memcpy(key, line, (size_t)(sp - line));
        key[sp - line] = 0;
        size_t vl = ll - (size_t)(sp - line) - 1;
        memcpy(val, sp + 1, vl);
        val[vl] = 0;
        int bit = !strcmp(key, "variant") ? 1 : !strcmp(key, "features") ? 2 : !strcmp(key, "scalars") ? 4 : 0;
        if (!bit) return snprintf(err, en, "%s: NTW2's header key %s is not variant, features or scalars", name, key), -1;
        if (got & bit) return snprintf(err, en, "%s: NTW2's header says %s twice", name, key), -1;
        got |= bit;
        if (bit == 1)
        {
            if (!vl || vl >= sizeof h->variant) return snprintf(err, en, "%s: NTW2's variant \"%s\"", name, val), -1;
            memcpy(h->variant, val, vl + 1);
        }
        else if (bit == 2)
        {
            char e2[200];
            if (words_mask(val, &h->mask, e2, sizeof e2)) return snprintf(err, en, "%s: NTW2's header: %s", name, e2), -1;
        }
        else
        {
            char *end;
            long n = strtol(val, &end, 10);
            if (!vl || *end || n <= 0 || n > 1 << 20) return snprintf(err, en, "%s: NTW2's scalars \"%s\"", name, val), -1;
            h->scalars = (int)n;
        }
    }
    if (got != 7) return snprintf(err, en, "%s: NTW2's header lacks %s", name, !(got & 1) ? "variant" : !(got & 2) ? "features" : "scalars"), -1;
    if (h->scalars != mask_ns(h->mask))
    {
        char fw[64];
        mask_words(h->mask, fw, sizeof fw);
        return snprintf(err, en, "%s: NTW2's header says %d scalars, but 155 and its features' (%s) are %d", name, h->scalars,
                        fw, mask_ns(h->mask)), -1;
    }
    h->ver = 2;
    return 8 + H;
}

/* ---- the parser ---- */

/* the parser's own flag (past net.h's): any features, the build's not consulted (ntinit --migrate's check) */
#define NTW_ANYFEAT 4

static int parse(const unsigned char *b, size_t len, const char *name, int flags, float *t[NETH_NET_TENSORS],
                 int dims[NETH_NET_TENSORS][4], char *err, size_t en)
{
    static const int rank[NETH_NET_TENSORS] = {
#define NETH_NET_R(n, r, a, b, c, d) r,
        NETH_NET_ORDER(NETH_NET_R)
#undef NETH_NET_R
    };
    const int bd[NETH_NET_TENSORS][4] = {   /* lane/audit-feat: fc1's the run's */
#define NETH_NET_D4(n, r, a, b, c, d) {a, b, c, d},
        NETH_NET_ORDER(NETH_NET_D4)
#undef NETH_NET_D4
    };
    const int fc1 = 2 * NETH_NET_STEM + 6, c3w = fc1 - 2;
    for (int i = 0; i < NETH_NET_TENSORS; ++i)
    {
        if (t) t[i] = NULL;
        if (dims) memcpy(dims[i], bd[i], sizeof bd[i]);
    }
    struct head h;
    long o0 = head_read(b, len, name, &h, err, en);
    if (o0 < 0) return -1;
    size_t off = (size_t)o0;
    int32_t cnt;
    memcpy(&cnt, b + off, 4);   /* head_read leaves at least 4 bytes */
    off += 4;
    if (!(cnt == NETH_NET_TENSORS || ((flags & NETH_NTW_FEWER) && cnt > fc1 && cnt < NETH_NET_TENSORS)))
        return snprintf(err, en, "%s: %d tensors, not this build's %d (another stem, hotbar or recurrent layer)", name,
                        (int)cnt, NETH_NET_TENSORS), -1;
    int fd[NETH_NET_TENSORS][4], cw = 0;
    size_t at[NETH_NET_TENSORS];
    for (int i = 0; i < cnt; ++i)
    {
        int32_t r;
        if (off + 4 > len) return snprintf(err, en, "%s: truncated", name), -1;
        memcpy(&r, b + off, 4);
        off += 4;
        if (r != rank[i]) return snprintf(err, en, "%s: tensor %d's rank is %d, net.h's %d", name, i, (int)r, rank[i]), -1;
        size_t n = 1;
        for (int k = 0; k < 4; ++k) fd[i][k] = bd[i][k];
        for (int k = 0; k < r; ++k, off += 4)
        {
            int32_t d;
            if (off + 4 > len) return snprintf(err, en, "%s: truncated", name), -1;
            memcpy(&d, b + off, 4);
            int ok = d == bd[i][k] || ((flags & NETH_NTW_NARROW) && k < 2 && d > 0 && d < bd[i][k]);
            if (i == fc1 && k == 1) ok = d >= cw + NETH_NET_BASE_NS;   /* the scalars: below */
            if (!ok)
                return snprintf(err, en, "%s: tensor %d's dim %d is %d, net.h's %d (another variant's net)", name, i, k,
                                (int)d, bd[i][k]), -1;
            fd[i][k] = d;
            n *= (size_t)d;
        }
        if (i == c3w) cw = fd[i][0] * NETH_NET_O3 * NETH_NET_O3;   /* fc1's columns before the scalars */
        if (n > (len - off) / 4) return snprintf(err, en, "%s: truncated", name), -1;
        at[i] = off;
        off += 4 * n;
    }
    if (off != len) return snprintf(err, en, "%s: trailing bytes", name), -1;
    /* fc1's scalars: NTW1 only at the base 155, NTW2 at its header's, every feature it names in the build */
    int ns = fd[fc1][1] - cw, bm = build_mask();
    char bw[64], fw[64];
    mask_words(bm, bw, sizeof bw);
    mask_words(h.mask, fw, sizeof fw);
    if (h.ver == 1 && ns != NETH_NET_BASE_NS)
    {
        const char *nm = NETH_NET_NAME;
        int named = strcmp(nm, "unnamed") != 0;
        return snprintf(err, en,
                        "%s: an NTW1 checkpoint (it does not say which inputs it reads) with %d scalars, 155 and %d of "
                        "features it does not name; this run (build %s) reads %d scalars, its features %s. Only an NTW1 file "
                        "of the base 155 scalars loads: state the file's features and migrate it to NTW2 with "
                        "out/runtime/train/%s%sntinit --migrate %s OUT.ntw --features WORDS",
                        name, ns, ns - NETH_NET_BASE_NS, nm, NETH_NET_NS, bw, named ? nm : "", named ? "/" : "", name), -1;
    }
    if (h.ver == 2 && !(flags & NTW_ANYFEAT) && (h.mask & ~bm))
    {
        char miss[64];
        mask_words(h.mask & ~bm, miss, sizeof miss);
        return snprintf(err, en,
                        "%s: built for the features %s (variant %s, %d scalars); this run (build %s) has %s, not %s: "
                        "refused",
                        name, fw, h.variant, h.scalars, NETH_NET_NAME, bw, miss), -1;
    }
    if (h.ver == 2 && ns != h.scalars)
        return snprintf(err, en, "%s: its features (%s) make %d scalars, but its first dense layer reads %d", name, fw,
                        h.scalars, ns), -1;
    if (!t) return cnt;
    /* the tensors at the build's shapes (fc1 the run's scalars; NETH_NTW_NARROW: the file's first two dims) */
    for (int i = 0; i < cnt; ++i)
    {
        int od[4];
        size_t n = 1;
        memcpy(od, fd[i], sizeof od);
        if (i == fc1) od[1] = cw + NETH_NET_NS;
        for (int k = 0; k < rank[i]; ++k) n *= (size_t)od[k];
        t[i] = calloc(n, sizeof(float));
        if (dims) memcpy(dims[i], od, sizeof od);
        if (i != fc1)
        {
            memcpy(t[i], b + at[i], 4 * n);
            continue;
        }
        /* each row: the conv columns and the base 155 as they are, each feature the file has at the run's place */
        for (int row = 0; row < od[0]; ++row)
        {
            const unsigned char *src = b + at[i] + 4 * (size_t)row * (size_t)fd[i][1];
            float *dst = t[i] + (size_t)row * (size_t)od[1];
            memcpy(dst, src, 4 * (size_t)(cw + NETH_NET_BASE_NS));
            for (int k = 0; k < NFEAT; ++k)
                if (h.mask >> k & 1)
                    memcpy(dst + cw + mask_off(bm, k), src + 4 * (size_t)(cw + mask_off(h.mask, k)), 4 * (size_t)FEAT[k].n);
        }
    }
    return cnt;
}

/* every reader's: the blob's tensors in net.h's order at the build's shapes, malloc'd (t[i] NULL past the count),
 * fc1's scalar columns where the build reads them and zero where the file has none; dims (or NULL) each one's. The
 * tensor count, or -1 with err (name, a path or what the blob is, starts each message) */
int neth_ntw_parse(const void *data, size_t len, const char *name, int flags, float *t[NETH_NET_TENSORS],
                   int dims[NETH_NET_TENSORS][4], char *err, size_t en)
{
    return parse(data, len, name, flags & (NETH_NTW_FEWER | NETH_NTW_NARROW), t, dims, err, en);
}

/* the one writer: the build's tensors (net.h's shapes, fc1 the run's scalars) as NTW2 with the build's variant and
 * the run's features; malloc'd */
unsigned char *neth_ntw_write(const float *const t[NETH_NET_TENSORS], size_t *len)
{
    static const int rank[NETH_NET_TENSORS] = {
#define NETH_NET_R(n, r, a, b, c, d) r,
        NETH_NET_ORDER(NETH_NET_R)
#undef NETH_NET_R
    };
    const int dims[NETH_NET_TENSORS][4] = {    /* lane/audit-feat: fc1's the run's */
#define NETH_NET_D4(n, r, a, b, c, d) {a, b, c, d},
        NETH_NET_ORDER(NETH_NET_D4)
#undef NETH_NET_D4
    };
    size_t h = head_write(NULL, NETH_NET_NAME, build_mask()), sz = h + 4, cnt[NETH_NET_TENSORS];
    for (int i = 0; i < NETH_NET_TENSORS; ++i)
    {
        cnt[i] = 1;
        for (int k = 0; k < rank[i]; ++k) cnt[i] *= (size_t)dims[i][k];
        sz += 4 + 4 * (size_t)rank[i] + 4 * cnt[i];
    }
    unsigned char *b = malloc(sz), *p = b + h;
    head_write(b, NETH_NET_NAME, build_mask());
    int n = NETH_NET_TENSORS;
    memcpy(p, &n, 4);
    p += 4;
    for (int i = 0; i < NETH_NET_TENSORS; ++i)
    {
        memcpy(p, &rank[i], 4);
        memcpy(p + 4, dims[i], 4 * (size_t)rank[i]);
        p += 4 + 4 * (size_t)rank[i];
        memcpy(p, t[i], 4 * cnt[i]);
        p += 4 * cnt[i];
    }
    *len = sz;
    return b;
}

/* ntinit --migrate: an NTW1 file whose features the caller states (words) as NTW2, its tensors' bytes as they are,
 * held to the parser (the build's tensors; fc1 as wide as 155 and those features). The header names this build's word
 * whatever the set, as a model a run saves does (lane/audit-feat: one build runs any set; until then the word was
 * "unnamed" when the set was not the build's own). Malloc'd, or NULL with err */
unsigned char *neth_ntw_migrate(const void *data, size_t len, const char *name, const char *words, size_t *olen,
                                char *err, size_t en)
{
    struct head h;
    if (head_read(data, len, name, &h, err, en) < 0) return NULL;
    if (h.ver != 1) return snprintf(err, en, "%s: NTW2 already (variant %s)", name, h.variant), NULL;
    int m;
    char e2[200];
    if (words_mask(words, &m, e2, sizeof e2)) return snprintf(err, en, "--features: %s", e2), NULL;
    const char *variant = NETH_NET_NAME;   /* lane/audit-feat: as neth_ntw_write names it, whatever the set */
    size_t hl = head_write(NULL, variant, m), n = hl + len - 8;
    unsigned char *o = malloc(n);
    head_write(o, variant, m);
    memcpy(o + hl, (const unsigned char *)data + 8, len - 8);
    if (parse(o, n, name, NTW_ANYFEAT, NULL, NULL, err, en) < 0) return free(o), NULL;
    *olen = n;
    return o;
}

/* lane/audit-feat: the run's set from words (ntloop and ntinit's --features, the run file's net.features): 0, or -1
 * with err; and a mask's words (none for no feature) */
int neth_net_words(const char *words, int *mask, char *err, size_t en)
{
    return words_mask(words, mask, err, en);
}
void neth_net_mask_words(int mask, char *s, size_t n)
{
    mask_words(mask, s, n);
}

/* lane/audit-feat: a file's version (1 or 2) and, NTW2, the features its header names (*mask; 0 for NTW1), or -1 with
 * err; the header alone is read (the first bytes of the file do) */
int neth_ntw_features(const void *data, size_t len, const char *name, int *mask, char *variant, size_t vn, char *err,
                      size_t en)
{
    struct head h;
    if (head_read(data, len, name, &h, err, en) < 0) return -1;
    *mask = h.mask;
    if (variant) snprintf(variant, vn, "%s", h.variant);
    return h.ver;
}

/* ntinit --stats: the header's variant, features and scalars (NTW1: empty, empty, -1) and the offset of the tensor
 * count, or -1 with err */
long neth_ntw_info(const void *data, size_t len, const char *name, char *variant, size_t vn, char *features, size_t fn,
                   int *scalars, char *err, size_t en)
{
    struct head h;
    long o = head_read(data, len, name, &h, err, en);
    if (o < 0) return -1;
    snprintf(variant, vn, "%s", h.variant);
    if (h.ver == 2) mask_words(h.mask, features, fn);
    else if (fn) features[0] = 0;
    *scalars = h.scalars;
    return o;
}

#ifndef NETH_NTW_ONLY

/* a blob's weights into the learner's layout (host, NETH_LEARN_NPARAM): 0, or -1 with err (neth_ntw_parse's rules) */
static int flat_of(const unsigned char *b, size_t len, const char *name, float *flat, char *err, size_t en)
{
    float *t[NETH_NET_TENSORS];
    if (neth_ntw_parse(b, len, name, 0, t, NULL, err, en) < 0) return -1;
    neth_learn_from_ntw((const float *const *)t, flat);
    for (int i = 0; i < NETH_NET_TENSORS; ++i) free(t[i]);
    return 0;
}

int neth_ntw_flat(const unsigned char *b, size_t len, float *flat, char *err, size_t en)
{
    return flat_of(b, len, "the blob", flat, err, en);
}

int neth_ntw_load(const char *path, float *flat, char *err, size_t en)
{
    size_t len;
    unsigned char *b = slurp(path, &len);
    if (!b) return snprintf(err, en, "%s: cannot read", path), -1;
    int r = flat_of(b, len, path, flat, err, en);
    free(b);
    return r;
}

int neth_ntw_save(const char *path, const float *flat)
{
    size_t len;
    unsigned char *b = neth_ntw_blob(flat, &len);
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(b, 1, len, f) == len;
    if (f) ok &= fclose(f) == 0;
    free(b);
    return ok ? 0 : -1;
}
int neth_file_copy(const char *src, const char *dst)
{
    size_t len;
    unsigned char *b = slurp(src, &len);
    if (!b) return -1;
    char tmp[4300];
    snprintf(tmp, sizeof tmp, "%s.tmp", dst);
    FILE *f = fopen(tmp, "wb");
    int bad = !f || fwrite(b, 1, len, f) != len;
    if (f && fclose(f)) bad = 1;
    free(b);
    return bad || rename(tmp, dst) ? -1 : 0;
}
/* the learner's weights, Adam's moments and its step beside them (--init reads them back) */
int neth_checkpoint_save(struct neth_pipe *L, const char *dir, double wall)
{
    struct neth_learner *R = &L->lr;
    float *h = malloc(NETH_LEARN_NPARAM * sizeof(float));
    const float *src[3] = {R->w, R->m, R->v};
    static const char *const name[3] = {"model.ntw", "model.adam_m.ntw", "model.adam_v.ntw"};
    char p[4096];
    int bad = 0;
    for (int i = 0; i < 3 && !bad; ++i)
    {
        CK(cudaMemcpy(h, src[i], NETH_LEARN_NPARAM * sizeof(float), cudaMemcpyDeviceToHost));
        snprintf(p, sizeof p, "%s/%s", dir, name[i]);
        bad = neth_ntw_save(p, h);
    }
    free(h);
    snprintf(p, sizeof p, "%s/model.json", dir);
    FILE *f = bad ? NULL : fopen(p, "w");
    if (!f) return -1;
    fprintf(f, "{\"adam_step\": %d, \"steps\": %lld, \"wall\": ", R->step, (long long)L->env_steps);
    jnum(f, wall);
    fputs(", \"net\": \"small\"}\n", f);
    return fclose(f);
}

/* --init's Adam state beside it (model.ntw: model.adam_m.ntw, model.adam_v.ntw, model.json's adam_step): 1 taken, 0
 * none there, -1 a broken one */
int neth_checkpoint_load_opt(struct neth_learner *R, const char *init, char *err, size_t en)
{
    char base[4096], p[4200];
    snprintf(base, sizeof base, "%s", init);
    size_t n = strlen(base);
    if (n < 4 || strcmp(base + n - 4, ".ntw")) return 0;
    base[n - 4] = 0;
    snprintf(p, sizeof p, "%s.json", base);
    size_t len;
    char *j = slurp(p, &len);
    if (!j) return 0;
    const char *s = strstr(j, "\"adam_step\":");
    int step = s ? atoi(s + 12) : -1;
    free(j);
    if (step < 0) return snprintf(err, en, "%.900s: no adam_step", p), -1;
    float *h = malloc(NETH_LEARN_NPARAM * sizeof(float));
    float *dst[2] = {R->m, R->v};
    static const char *const suf[2] = {"adam_m", "adam_v"};
    for (int i = 0; i < 2; ++i)
    {
        snprintf(p, sizeof p, "%s.%s.ntw", base, suf[i]);
        if (neth_ntw_load(p, h, err, en)) return free(h), -1;
        CK(cudaMemcpy(dst[i], h, NETH_LEARN_NPARAM * sizeof(float), cudaMemcpyHostToDevice));
    }
    free(h);
    R->step = step;
    return 1;
}

#endif
