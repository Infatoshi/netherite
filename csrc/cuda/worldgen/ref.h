/* The generation checks' stored C side (worldgen_check, worldgen_dim_check;
 * lane/cudasplit). C's result for every stage of every chunk of a check is
 * kept as a 64-bit hash of its values, in a file named by the C build (KEY:
 * the engine library and the check's source, from make) and the request
 * list, so a later run of the same check hashes the device's stages and
 * compares, and runs C for a chunk only where a hash differs, to name the
 * first differing cell as before. The seed-world spawn lists (the chunks a
 * seed-world build loads, 5 s of C each) are kept the same way. Without
 * --store every chunk runs C, as before. */
#ifndef NETHERITE_WORLDGEN_REF_H
#define NETHERITE_WORLDGEN_REF_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* a hash over values, not bytes: a device stage's uint8 cells and C's
 * uint16 ones hash the same when their values do */
static inline uint64_t gr_mix(uint64_t h, uint64_t v)
{
    h = (h ^ v) * 0x9e3779b97f4a7c15ull;
    return h ^ (h >> 32);
}

static inline uint64_t gr_u8(uint64_t h, const uint8_t *a, size_t n)
{
    for (size_t i = 0; i < n; ++i) h = gr_mix(h, a[i]);
    return gr_mix(h, n);
}

static inline uint64_t gr_u16(uint64_t h, const uint16_t *a, size_t n)
{
    for (size_t i = 0; i < n; ++i) h = gr_mix(h, a[i]);
    return gr_mix(h, n);
}

static inline uint64_t gr_i32(uint64_t h, const int32_t *a, size_t n)
{
    for (size_t i = 0; i < n; ++i) h = gr_mix(h, (uint64_t)(int64_t)a[i]);
    return gr_mix(h, n);
}

/* doubles by their bits */
static inline uint64_t gr_f64(uint64_t h, const double *a, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        uint64_t b;

        memcpy(&b, &a[i], sizeof b);
        h = gr_mix(h, b);
    }
    return gr_mix(h, n);
}

#define GR_MAGIC 0x4652474eu   /* "NGRF" */

/* one check's references: n chunks of nst hashes, in request order */
struct gr_store {
    char path[4096];
    int on, loaded;
    size_t n, nst;
    uint64_t *have, *made;
    unsigned char *done;
};

/* the store file of this check (what: its kind and flags) for the request
 * list reqs (n entries of rb bytes); loads it when it is there */
static inline void gr_open(struct gr_store *s, const char *dir, const char *key, const char *what, const void *reqs,
                           size_t rb, size_t n, size_t nst)
{
    memset(s, 0, sizeof *s);
    if (dir == NULL || key == NULL) return;
    s->on = 1;
    s->n = n;
    s->nst = nst;
    s->have = calloc(n * nst + 1, sizeof *s->have);
    s->made = calloc(n * nst + 1, sizeof *s->made);
    s->done = calloc(n + 1, 1);
    if (!s->have || !s->made || !s->done)
    {
        s->on = 0;
        return;
    }
    snprintf(s->path, sizeof s->path, "%s/%s-%s-%016llx.ref", dir, key, what,
             (unsigned long long)gr_u8(0, (const uint8_t *)reqs, rb * n));

    FILE *f = fopen(s->path, "rb");
    uint32_t head[4];

    if (f == NULL) return;
    if (fread(head, sizeof head, 1, f) == 1 && head[0] == GR_MAGIC && head[1] == 1 && head[2] == (uint32_t)nst &&
        fread(s->have, sizeof *s->have, n * nst, f) == n * nst)
        s->loaded = 1;
    fclose(f);
}

/* chunk i's hashes: whether C's stored ones are the device's (h) */
static inline int gr_match(const struct gr_store *s, size_t i, const uint64_t *h)
{
    return s->loaded && memcmp(s->have + i * s->nst, h, s->nst * sizeof *h) == 0;
}

/* C's hashes of chunk i, computed this run */
static inline void gr_made(struct gr_store *s, size_t i, const uint64_t *h)
{
    if (!s->on) return;
    memcpy(s->made + i * s->nst, h, s->nst * sizeof *h);
    s->done[i] = 1;
}

/* the file, when this run computed C for every chunk and none was stored */
static inline void gr_close(struct gr_store *s)
{
    if (s->on && !s->loaded)
    {
        size_t i = 0;

        while (i < s->n && s->done[i]) ++i;
        if (i == s->n)
        {
            char tmp[4200];
            uint32_t head[4] = {GR_MAGIC, 1, (uint32_t)s->nst, 0};
            FILE *f;

            snprintf(tmp, sizeof tmp, "%s.%ld.tmp", s->path, (long)getpid());
            if ((f = fopen(tmp, "wb")) != NULL)
            {
                int ok = fwrite(head, sizeof head, 1, f) == 1 &&
                         fwrite(s->made, sizeof *s->made, s->n * s->nst, f) == s->n * s->nst;

                if (fclose(f) == 0 && ok) rename(tmp, s->path);
                else remove(tmp);
            }
        }
    }
    free(s->have);
    free(s->made);
    free(s->done);
}

/* a seed world's spawn list (the chunk keys a build loads, in order),
 * stored under the same key: n, or 0 when it is not there */
static inline size_t gr_spawn_load(const char *dir, const char *key, int64_t seed, int dim, uint64_t **keys)
{
    char path[4096];
    FILE *f;
    uint64_t n = 0;

    *keys = NULL;
    if (dir == NULL || key == NULL) return 0;
    snprintf(path, sizeof path, "%s/%s-spawn-%lld-%d.list", dir, key, (long long)seed, dim);
    if ((f = fopen(path, "rb")) == NULL) return 0;
    if (fread(&n, sizeof n, 1, f) != 1 || n == 0 || n > ((uint64_t)1 << 24) ||
        (*keys = malloc(n * sizeof **keys)) == NULL || fread(*keys, sizeof **keys, n, f) != n)
    {
        free(*keys);
        *keys = NULL;
        n = 0;
    }
    fclose(f);
    return (size_t)n;
}

static inline void gr_spawn_save(const char *dir, const char *key, int64_t seed, int dim, const uint64_t *keys, size_t n)
{
    char path[4096], tmp[4200];
    FILE *f;
    uint64_t n64 = n;

    if (dir == NULL || key == NULL || n == 0) return;
    snprintf(path, sizeof path, "%s/%s-spawn-%lld-%d.list", dir, key, (long long)seed, dim);
    snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, (long)getpid());
    if ((f = fopen(tmp, "wb")) == NULL) return;

    int ok = fwrite(&n64, sizeof n64, 1, f) == 1 && fwrite(keys, sizeof *keys, n, f) == n;

    if (fclose(f) == 0 && ok) rename(tmp, path);
    else remove(tmp);
}

#endif
