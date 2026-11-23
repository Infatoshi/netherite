/* ntloop's checkpoints: net.h's NTW1 blobs into the learner's layout and back, a file copied whole, the model's
 * weights and Adam state beside it (--out's model.ntw, --init's state). */
#define _GNU_SOURCE
#include "neth_pipe_internal.h"

/* a blob's weights into the learner's layout (host, NETH_LEARN_NPARAM): 0, or -1 with err (net.h's shapes held) */
int neth_ntw_flat(const unsigned char *b, size_t len, float *flat, char *err, size_t en)
{
    static const int rank[NETH_NET_TENSORS] = {
#define NETH_NET_R(n, r, a, b, c, d) r,
        NETH_NET_ORDER(NETH_NET_R)
#undef NETH_NET_R
    };
    static const int dims[NETH_NET_TENSORS][4] = {
#define NETH_NET_D4(n, r, a, b, c, d) {a, b, c, d},
        NETH_NET_ORDER(NETH_NET_D4)
#undef NETH_NET_D4
    };
    int cnt;
    if (len < 12 || memcmp(b, "NTW1\0\0\0\0", 8) || (memcpy(&cnt, b + 8, 4), cnt != NETH_NET_TENSORS))
        return snprintf(err, en, "not net.h's NTW1 blob of %d tensors", NETH_NET_TENSORS), -1;
    size_t off = 12;
    const float *t[NETH_NET_TENSORS];
    float *own[NETH_NET_TENSORS];
    for (int i = 0; i < NETH_NET_TENSORS; ++i)
    {
        int r, d;
        size_t n = 1;
        if (off + 4 > len || (memcpy(&r, b + off, 4), r != rank[i])) return snprintf(err, en, "tensor %d's rank", i), -1;
        off += 4;
        /* fc1 of a net without the late scalars (NETH_NET_LATE): its columns the first NETH_NET_FLAT - NETH_NET_LATE_N, the
         * late ones zero, so the wider net computes what the blob's did; the same for the hotbar's
         * tools (NETH_NET_HSEE), after the late ones: a blob's scalars are 155, 155 + the late ones' or all of the build's */
        int narrow = 0;
        for (int k = 0; k < r; ++k, off += 4)
        {
            if (off + 4 <= len) memcpy(&d, b + off, 4);
            if (r == 2 && k == 1 && dims[i][0] == NETH_NET_D && dims[i][1] == NETH_NET_FLAT && off + 4 <= len && d < NETH_NET_FLAT &&
                d > NETH_NET_FLAT - NETH_NET_NS && NETH_NET_NS_PREFIX(d - (NETH_NET_FLAT - NETH_NET_NS)))   /* and NETH_NET_DIG's */
                narrow = d, d = NETH_NET_FLAT;
            else if (off + 4 > len || d != dims[i][k])
                return snprintf(err, en, "tensor %d's shape is not net.h's", i), -1;
            n *= (size_t)d;
        }
        size_t nb = narrow ? (size_t)NETH_NET_D * (size_t)narrow : n;
        if (off + 4 * nb > len) return snprintf(err, en, "truncated"), -1;
        own[i] = calloc(n, 4);
        if (narrow)
            for (int row = 0; row < NETH_NET_D; ++row)
                memcpy(own[i] + (size_t)row * NETH_NET_FLAT, b + off + 4 * (size_t)row * (size_t)narrow, 4 * (size_t)narrow);
        else
            memcpy(own[i], b + off, 4 * n);
        t[i] = own[i];
        off += 4 * nb;
    }
    neth_learn_from_ntw(t, flat);
    for (int i = 0; i < NETH_NET_TENSORS; ++i) free(own[i]);
    return off == len ? 0 : (snprintf(err, en, "trailing bytes"), -1);
}

int neth_ntw_load(const char *path, float *flat, char *err, size_t en)
{
    size_t len;
    unsigned char *b = slurp(path, &len);
    if (!b) return snprintf(err, en, "%s: cannot read", path), -1;
    char e[256];
    int r = neth_ntw_flat(b, len, flat, e, sizeof e);
    if (r) snprintf(err, en, "%s: %s", path, e);
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
