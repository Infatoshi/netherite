/* Gate: the native world core against the oracle's setblock probe
 * (oracle/harness/netherite/oracle/Probe.java; recorded with `make -C oracle probe`).
 *
 * The probe loaded a square of raw chunks, applied OPS random World.setBlock
 * calls with flags 2, and recorded per op: the coordinates, the block, a flag
 * byte (bit 0: setBlock returned true; bit 1: the 17-block guard was false) and
 * an FNV-1a 64 hash of the 3x3 chunks around the op, then the whole region
 * after the last op.
 *
 * This replays ops.bin in the manifest's chunk load order and checks, op by op,
 * the return value, the guard, and the hash; then every field of every loaded
 * chunk against final.bin.gz. The first difference stops the run and names the
 * op or the field.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../engine/world.h"
#include "probe.h"

/* CPU clock for the replay's two halves: the chunk pipeline, then the ops
 * (nearly all of which is the 3x3 hash: 9 chunks x 329,734 bytes per op). */
static double seconds(void)
{
    return (double)clock() / CLOCKS_PER_SEC;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_probe PROBE_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1024];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    /* A feature probe has its own test (test_feature); a setblock probe with
     * no kind field is the original format. */
    char kind[64];
    manifest_str(manifest, "kind", kind, sizeof kind);

    if (strstr(manifest, "\"kind\":") != NULL && strcmp(kind, "setblock") != 0)
    {
        printf("FAIL %s: kind is %s, not a setblock probe\n", dir, kind);
        free(manifest);
        return 2;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int ops = (int)manifest_int(manifest, "ops");

    /* "loaded":[ [cx,cz], ... ] in the order the oracle loaded them */
    char *p = strstr(manifest, "\"loaded\":[");
    int nchunks = 0, cap = 1024;
    int *lcx = malloc((size_t)cap * sizeof *lcx), *lcz = malloc((size_t)cap * sizeof *lcz);

    if (p != NULL)
    {
        p += strlen("\"loaded\":[");
        while (*p == '[')
        {
            if (nchunks == cap)
            {
                cap *= 2;
                lcx = realloc(lcx, (size_t)cap * sizeof *lcx);
                lcz = realloc(lcz, (size_t)cap * sizeof *lcz);
            }

            lcx[nchunks] = (int)strtol(p + 1, &p, 10);
            lcz[nchunks] = (int)strtol(p + 1, &p, 10);
            ++nchunks;

            if (*p == ']') ++p;
            if (*p == ',') ++p;
        }

        if (*p != ']')
        {
            fprintf(stderr, "%s: cannot parse the manifest's loaded list\n", dir);
            return 2;
        }
    }

    if (nchunks == 0)
    {
        fprintf(stderr, "%s: no loaded chunks in the manifest\n", dir);
        return 2;
    }

    struct world w;

    double t0 = seconds();

    world_init(&w, seed);

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    double t1 = seconds();

    snprintf(path, sizeof path, "%s/ops.bin", dir);
    size_t olen;
    unsigned char *opbuf = probe_read_file(path, &olen);

    if (olen % 24 != 0 || (int)(olen / 24) != ops)
    {
        fprintf(stderr, "%s: ops.bin is %zu bytes, the manifest says %d ops\n", dir, olen, ops);
        return 2;
    }

    unsigned char *cb = malloc(CHUNK_BYTES);
    int fail = 0;
    int mismatches = 0;
    int changed = 0, moved = 0, light_skipped = 0;
    uint64_t prev = 0;

    for (int i = 0; i < ops; ++i)
    {
        const unsigned char *o = opbuf + 24 * i;
        int x = (int)le32(o), y = (int)le32(o + 4), z = (int)le32(o + 8);
        int id = o[12] | o[13] << 8, meta = o[14];
        int flags = o[15];
        uint64_t want = le64(o + 16);

        int lit = world_do_chunks_near_chunk_exist(&w, x, y, z, 17);
        int did = world_set_block(&w, x, y, z, id, meta, 2);
        uint64_t got = probe_hash_due(i, ops) ? hash_around(&w, x >> 4, z >> 4, cb) : want;

        /* the probe's own tallies, for the report: ops that changed a block,
         * ops whose 3x3 hash moved, ops the guard kept out of the light pass */
        if (i > 0 && got != prev) ++moved;
        prev = got;
        if (did) ++changed;
        if (!lit) ++light_skipped;

        if (did != (flags & 1))
        {
            printf("FAIL %s op %d (%d,%d,%d) id %d meta %d: setBlock returned %d, the probe recorded %d\n",
                   dir, i, x, y, z, id, meta, did, flags & 1);
            ++fail;
            break;
        }

        int want_lit = (flags & 2) == 0;

        if (lit != want_lit)
        {
            printf("FAIL %s op %d (%d,%d,%d) id %d meta %d: the 17-block guard was %s, the probe recorded %s\n",
                   dir, i, x, y, z, id, meta, lit ? "true" : "false", want_lit ? "true" : "false");
            ++fail;
            break;
        }

        if (got != want)
        {
            printf("FAIL %s op %d (%d,%d,%d) id %d meta %d: hash of the 3x3 chunks want %016llx got %016llx\n",
                   dir, i, x, y, z, id, meta, (unsigned long long)want, (unsigned long long)got);
            ++fail;
            break;
        }

        ++mismatches; /* ops that agreed */
    }

    /* the whole region after the last op */
    snprintf(path, sizeof path, "%s/final.bin.gz", dir);
    gzFile g = gzopen(path, "rb");

    if (g == NULL)
    {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }

    unsigned char head[8], final[CHUNK_BYTES], cols[256], gap;

    for (int i = 0; i < nchunks && !fail; ++i)
    {
        if (gzread(g, head, 8) != 8 || gzread(g, final, CHUNK_BYTES) != CHUNK_BYTES || gzread(g, cols, 256) != 256 ||
            gzread(g, &gap, 1) != 1)
        {
            fprintf(stderr, "%s: final.bin.gz ends early\n", dir);
            return 2;
        }

        int fcx = (int)le32(head), fcz = (int)le32(head + 4);

        if (fcx != lcx[i] || fcz != lcz[i])
        {
            fprintf(stderr, "%s: final chunk %d is (%d,%d), the manifest says (%d,%d)\n", dir, i, fcx, fcz, lcx[i],
                    lcz[i]);
            return 2;
        }

        struct chunk *c = world_chunk(&w, fcx, fcz);
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

    gzclose(g);
    printf("%s %s: %d/%d ops, %d chunks (%d generated by the light pass's chunk lookups), seed %lld, around %d,%d\n"
           "     changed %d, moved %d, lightSkipped %d, %.2fs to load %.2fs for the ops\n",
           fail ? "FAIL" : "PASS", dir, mismatches, ops, nchunks, (int)w.used - nchunks, (long long)seed, cx, cz,
           changed, moved, light_skipped, t1 - t0, seconds() - t1);

    free(cb);
    free(opbuf);
    free(lcx);
    free(lcz);
    free(manifest);
    world_free(&w);
    if (fail) probe_localize(argv[0], dir);

    return fail;
}
