/* Reader for the oracle's stage dumps (make -C oracle chunks): manifest.json plus
 * one gzip stream per stage, chunks in cx-major order. */
#ifndef NETHERITE_DUMP_H
#define NETHERITE_DUMP_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

struct dump {
    char dir[1024];
    int64_t seed;
    int x0, z0, x1, z1, step;
};

static long long manifest_num(const char *json, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) { fprintf(stderr, "manifest: no %s\n", key); exit(2); }
    return strtoll(p + strlen(pat), 0, 10);
}

static void dump_open(struct dump *d, const char *dir)
{
    snprintf(d->dir, sizeof d->dir, "%s", dir);
    char path[1100];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    static char json[1 << 22];
    size_t n = fread(json, 1, sizeof json - 1, f);
    json[n] = 0;
    fclose(f);
    d->seed = manifest_num(json, "seed");
    d->x0 = (int)manifest_num(json, "x0");
    d->z0 = (int)manifest_num(json, "z0");
    d->x1 = (int)manifest_num(json, "x1");
    d->z1 = (int)manifest_num(json, "z1");
    d->step = strstr(json, "\"step\":") ? (int)manifest_num(json, "step") : 1;
}

static gzFile dump_stage(const struct dump *d, const char *stage)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/%s.bin.gz", d->dir, stage);
    gzFile g = gzopen(path, "rb");
    if (!g) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    return g;
}

static void dump_read(gzFile g, void *buf, unsigned n)
{
    if (gzread(g, buf, n) != (int)n) { fprintf(stderr, "short read in dump\n"); exit(2); }
}

#endif
