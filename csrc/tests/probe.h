/* Reader for the oracle's world-core probes (make -C oracle probe), the shared
 * half of test_probe.c (kind "setblock") and test_feature.c (kind "feature"):
 * the manifest's JSON fields, the LE readers, the chunk bytes the probes hash
 * and the 3x3 FNV hash around a chunk. */
#ifndef NETHERITE_PROBE_H
#define NETHERITE_PROBE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "../engine/gunzip.h"
#include "../engine/snapshot.h"
#include "../engine/terrain.h"
#include "../engine/world.h"

/* ids uint16 x 65536, metas, sky and block light x 65536, heightMap and
 * precipitationHeightMap 256 int32 each, heightMapMinimum int32, section mask
 * uint16. */
#define CHUNK_BYTES (2 * 65536 + 3 * 65536 + 2 * 256 * 4 + 4 + 2)

#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

/* The per-case 3x3 hash is about 3 MB of FNV-1a, one serial multiply chain,
 * and it was nearly all of a long probe's time (86 s for 22,000 tick cases).
 * The case tests check it every PROBE_HASH_EVERY cases and on the last case;
 * the final region compare still covers every byte. On any failure the test
 * reruns itself with --hash-all, which hashes every case, so the report still
 * names the first case that differs. */
#define PROBE_HASH_EVERY 64

static int probe_hash_all;
static int probe_hash_never;
/* --cases N: only the first N cases, without the end-of-run compares (make
 * smoke's short runs of the long case tests) */
static int probe_cases = -1;

/* Takes leading --hash-all, --hash-never and --cases N out of argv. */
static inline void probe_args(int *argc, char **argv)
{
    for (;;)
    {
        int k = 0;
        if (*argc > 1 && !strcmp(argv[1], "--hash-all")) probe_hash_all = k = 1;
        else if (*argc > 1 && !strcmp(argv[1], "--hash-never")) probe_hash_never = k = 1;
        else if (*argc > 2 && !strcmp(argv[1], "--cases")) { probe_cases = atoi(argv[2]); k = 2; }
        if (!k) break;
        for (int i = 1; i + k <= *argc; ++i) argv[i] = argv[i + k];
        *argc -= k;
    }
}

/* the cases a test runs of the n recorded: n, or fewer under --cases */
static inline int probe_limit(int n)
{
    return probe_cases >= 0 && probe_cases < n ? probe_cases : n;
}

static int probe_hash_never;

static inline int probe_hash_due(int i, int n)
{
    return !probe_hash_never && (probe_hash_all || (i + 1) % PROBE_HASH_EVERY == 0 || i == n - 1);
}

/* After a failure with sampled hashes: run again hashing every case. */
static inline void probe_localize(const char *self, const char *dir)
{
    if (probe_hash_all) return;
    printf("     rerunning %s with every case hashed to find the first difference\n", dir);
    fflush(stdout);
    char n[16];
    snprintf(n, sizeof n, "%d", probe_cases);
    if (probe_cases >= 0) execl(self, self, "--hash-all", "--cases", n, dir, (char *)NULL);
    else execl(self, self, "--hash-all", dir, (char *)NULL);
    perror("execl");
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t le64(const unsigned char *p)
{
    uint64_t v = 0;

    for (int i = 7; i >= 0; --i) v = v << 8 | p[i];
    return v;
}

static void put_le32(unsigned char *p, int32_t v)
{
    uint32_t u = (uint32_t)v;

    for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(u >> (8 * i));
}

/* One integer value out of the manifest's single JSON line. */
static long long manifest_int(const char *json, const char *key)
{
    char pat[64];

    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);

    if (p == NULL)
    {
        fprintf(stderr, "manifest: no %s\n", key);
        exit(2);
    }

    return strtoll(p + strlen(pat), NULL, 10);
}

/* One string value out of the manifest's single JSON line, into out. */
static inline void manifest_str(const char *json, const char *key, char *out, size_t n)
{
    char pat[64];

    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(json, pat);

    if (p == NULL)
    {
        fprintf(stderr, "manifest: no %s\n", key);
        exit(2);
    }

    p += strlen(pat);
    const char *e = strchr(p, '"');

    if (e == NULL)
    {
        fprintf(stderr, "manifest: %s is not a string\n", key);
        exit(2);
    }

    size_t len = (size_t)(e - p);

    if (len >= n) len = n - 1;

    memcpy(out, p, len);
    out[len] = 0;
}

/* The chunk bytes the probes hash, in the manifest's layout. The host is
 * little-endian (x86-64, arm64), so the id array can be copied as is; the
 * int32 arrays are written out explicitly. */
static inline void chunk_bytes(const struct chunk *c, unsigned char *out)
{
    chunk_cells_out(c, (uint16_t *)(void *)out, out + 2 * CHUNK_CELLS, out + 3 * CHUNK_CELLS,
                    out + 4 * CHUNK_CELLS);                /* ids uint16 LE */

    int p = 5 * CHUNK_CELLS; /* heightMap then precipitationHeightMap */

    for (int i = 0; i < 256; ++i)
    {
        put_le32(out + p, c->height[i]);
        put_le32(out + p + 1024, c->precip[i]);
        p += 4;
    }

    put_le32(out + p + 1024, c->height_min);
    out[p + 1028] = (unsigned char)(c->mask & 255);
    out[p + 1029] = (unsigned char)(c->mask >> 8);
}

/* FNV-1a 64 of the 3x3 chunks around (chx, chz); a chunk that is not loaded
 * contributes one zero byte, like the probe's chunkExists branch. At the
 * region's rim this also generates the chunks the *native* side is missing, so
 * the chunk load order has to be the probe's. */
static inline uint64_t hash_around(struct world *w, int chx, int chz, unsigned char *buf)
{
    /* snapshot.c's chain over each chunk's bands: the same value as
     * snapshot_fnv over chunk_bytes, without writing the bytes out */
    (void)buf;
    return snapshot_chunk_hash_around(w, chx, chz);
}

/* The manifest's "loaded":[ [cx,cz], ... ] list, in the oracle's load order.
 * *n gets the number of chunks; the caller frees the returned arrays. */
static inline int *probe_loaded(const char *dir, const char *manifest, int *n, int **lcz)
{
    char *p = (char *)strstr(manifest, "\"loaded\":[");

    if (p == NULL)
    {
        fprintf(stderr, "%s: no loaded list in the manifest\n", dir);
        exit(2);
    }

    p += strlen("\"loaded\":[");
    int cap = 1024, count = 0;
    int *lcx = malloc((size_t)cap * sizeof *lcx);

    *lcz = malloc((size_t)cap * sizeof **lcz);

    while (*p == '[')
    {
        if (count == cap)
        {
            cap *= 2;
            lcx = realloc(lcx, (size_t)cap * sizeof *lcx);
            *lcz = realloc(*lcz, (size_t)cap * sizeof **lcz);
        }

        lcx[count] = (int)strtol(p + 1, &p, 10);
        (*lcz)[count] = (int)strtol(p + 1, &p, 10);
        ++count;

        if (*p == ']') ++p;
        if (*p == ',') ++p;
    }

    if (*p != ']')
    {
        fprintf(stderr, "%s: cannot parse the manifest's loaded list\n", dir);
        exit(2);
    }

    if (count == 0)
    {
        fprintf(stderr, "%s: no loaded chunks in the manifest\n", dir);
        exit(2);
    }

    *n = count;
    return lcx;
}

/* The first field of a chunk that differs from the recorded bytes, in the
 * order the layout lists. */
static int cmp_chunk(const unsigned char *want, const struct chunk *c, char *what, size_t n)
{
    for (int i = 0; i < CHUNK_CELLS; ++i)
    {
        int id = want[2 * i] | want[2 * i + 1] << 8;

        if (chunk_cell_id(c, i) != id)
        {
            snprintf(what, n, "id (%d,%d,%d) want %d got %d", i >> 12, i & 255, (i >> 8) & 15, id, chunk_cell_id(c, i));
            return 1;
        }
    }

    for (int i = 0; i < CHUNK_CELLS; ++i)
        if (chunk_cell_meta(c, i) != want[2 * CHUNK_CELLS + i])
        {
            snprintf(what, n, "meta (%d,%d,%d) want %d got %d", i >> 12, i & 255, (i >> 8) & 15,
                     want[2 * CHUNK_CELLS + i], chunk_cell_meta(c, i));
            return 1;
        }

    for (int i = 0; i < CHUNK_CELLS; ++i)
        if (chunk_cell_sky(c, i) != want[3 * CHUNK_CELLS + i])
        {
            snprintf(what, n, "sky light (%d,%d,%d) want %d got %d", i >> 12, i & 255, (i >> 8) & 15,
                     want[3 * CHUNK_CELLS + i], chunk_cell_sky(c, i));
            return 1;
        }

    for (int i = 0; i < CHUNK_CELLS; ++i)
        if (chunk_cell_blocklight(c, i) != want[4 * CHUNK_CELLS + i])
        {
            snprintf(what, n, "block light (%d,%d,%d) want %d got %d", i >> 12, i & 255, (i >> 8) & 15,
                     want[4 * CHUNK_CELLS + i], chunk_cell_blocklight(c, i));
            return 1;
        }

    for (int i = 0; i < 256; ++i)
        if (c->height[i] != (int32_t)le32(want + 5 * CHUNK_CELLS + 4 * i))
        {
            snprintf(what, n, "heightMap (%d,%d) want %d got %d", i & 15, i >> 4,
                     (int32_t)le32(want + 5 * CHUNK_CELLS + 4 * i), c->height[i]);
            return 1;
        }

    for (int i = 0; i < 256; ++i)
        if (c->precip[i] != (int32_t)le32(want + 5 * CHUNK_CELLS + 1024 + 4 * i))
        {
            snprintf(what, n, "precipitationHeightMap (%d,%d) want %d got %d", i & 15, i >> 4,
                     (int32_t)le32(want + 5 * CHUNK_CELLS + 1024 + 4 * i), c->precip[i]);
            return 1;
        }

    int32_t hmin = (int32_t)le32(want + 5 * CHUNK_CELLS + 2048);

    if (c->height_min != hmin)
    {
        snprintf(what, n, "heightMapMinimum want %d got %d", hmin, c->height_min);
        return 1;
    }

    int mask = want[5 * CHUNK_CELLS + 2052] | want[5 * CHUNK_CELLS + 2053] << 8;

    if (c->mask != mask)
    {
        snprintf(what, n, "section mask want 0x%04x got 0x%04x", mask, c->mask);
        return 1;
    }

    return 0;
}

/* The whole region after the last case, from final.bin.gz, in load order. */
static inline int cmp_final(const char *dir, const char *path, struct world *w, const int *lcx, const int *lcz, int nchunks)
{
    struct gunzip *g = gunzip_open(path);

    if (g == NULL)
    {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }

    unsigned char head[8], final[CHUNK_BYTES], cols[256], gap;
    int fail = 0;

    for (int i = 0; i < nchunks && !fail; ++i)
    {
        if (gunzip_read(g, head, 8) != 8 || gunzip_read(g, final, CHUNK_BYTES) != CHUNK_BYTES || gunzip_read(g, cols, 256) != 256 ||
            gunzip_read(g, &gap, 1) != 1)
        {
            fprintf(stderr, "%s: final.bin.gz ends early\n", dir);
            gunzip_close(g);
            return 1;
        }

        int fcx = (int)le32(head), fcz = (int)le32(head + 4);

        if (fcx != lcx[i] || fcz != lcz[i])
        {
            fprintf(stderr, "%s: final chunk %d is (%d,%d), the manifest says (%d,%d)\n", dir, i, fcx, fcz, lcx[i],
                    lcz[i]);
            gunzip_close(g);
            return 1;
        }

        struct chunk *c = world_chunk(w, fcx, fcz);
        char what[256];

        if (c == NULL)
        {
            printf("FAIL %s final chunk (%d,%d): not loaded\n", dir, fcx, fcz);
            ++fail;
            break;
        }

        if (cmp_chunk(final, c, what, sizeof what))
        {
            printf("FAIL %s final chunk (%d,%d): %s\n", dir, fcx, fcz, what);
            ++fail;
            break;
        }

        for (int k = 0; k < 256; ++k)
            if (c->update_skylight_columns[k] != (cols[k] ? 1 : 0))
            {
                printf("FAIL %s final chunk (%d,%d): updateSkylightColumns (%d,%d) want %d got %d\n", dir, fcx, fcz,
                       k & 15, k >> 4, cols[k] ? 1 : 0, c->update_skylight_columns[k]);
                ++fail;
                break;
            }

        if (fail) break;

        if (c->gap_lighting_updated != (gap ? 1 : 0))
        {
            printf("FAIL %s final chunk (%d,%d): isGapLightingUpdated want %d got %d\n", dir, fcx, fcz, gap ? 1 : 0,
                   c->gap_lighting_updated);
            ++fail;
            break;
        }
    }

    gunzip_close(g);
    return fail;
}

static inline void *probe_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL)
    {
        perror(path);
        exit(2);
    }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);

    unsigned char *buf = malloc((size_t)n + 1);

    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
    {
        fprintf(stderr, "short read in %s\n", path);
        exit(2);
    }

    fclose(f);
    buf[n] = 0; /* the manifest is read as a string */
    *len = (size_t)n;
    return buf;
}

#endif