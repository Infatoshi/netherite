/* Gate: the native random block ticks against the oracle's TickProbe
 * (oracle/harness/netherite/oracle/TickProbe.java, recorded with
 * `make -C oracle run CLASS=TickProbe`).
 *
 *   test_ticks PROBE_DIR
 *
 * DIR holds manifest.json (kind "ticks": seed, cases, opseed,
 * skylightSubtracted, the scene list), scenes.bin (per (scene, variant) the
 * setBlock(flag 2) ops that build the scene, anchor-relative), cases.bin
 * (52 bytes per case: scene, variant, ticked position, case seed, tick seed,
 * write count, 3x3 hash, World.rand's 48-bit state after), writes.bin and
 * final.bin.gz.
 *
 * The region loads in the manifest's order, every case's scene ops replay
 * through world_set_block, the tick runs through randomtick_update_tick with
 * the case's two Randoms, and the write stream, the hash and the World.rand
 * state compare against the record; final.bin.gz closes the run. The first
 * difference stops the run and names the case, the write index and both
 * values. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/features.h"
#include "../engine/randomtick.h"
#include "probe.h"

/* The oracle's case record: scene uint16, variant uint16, x/y/z int32, case
 * seed int64, tick seed int64, writes uint32, hash uint64, World.rand state
 * uint64. */
#define CASE_BYTES 52
#define OP_BYTES 7

/* The scene list the probe can hold. */
#define MAX_SCENES 32

struct writes
{
    unsigned char *base;   /* the oracle's bytes for this case */
    uint32_t want;
    uint32_t got;
    int overflow;
    int bad;
};

static struct writes cur;
static char diff_what[256];

/* world's on_block hook: compare one write against the oracle's, in order.
 * A metadata-only write reports id -1, which the record stores as 0xffff. */
static void on_write(void *ctx, int x, int y, int z, int id, int meta)
{
    struct writes *s = ctx;

    if (s->got >= s->want)
    {
        if (!s->overflow)
        {
            s->overflow = 1;
            snprintf(diff_what, sizeof diff_what, "write %u (%d,%d,%d) id %d meta %d: the oracle has only %u writes",
                     s->got + 1, x, y, z, id & 0xffff, meta, s->want);
        }

        ++s->got;
        return;
    }

    const unsigned char *o = s->base + 16 * (size_t)s->got;
    int wx = (int)le32(o), wy = (int)le32(o + 4), wz = (int)le32(o + 8);
    int wid = o[12] | o[13] << 8, wmeta = o[14];

    if (!s->bad && (x != wx || y != wy || z != wz || (id & 0xffff) != wid || meta != wmeta))
    {
        s->bad = (int)s->got + 1;
        snprintf(diff_what, sizeof diff_what, "write %u want (%d,%d,%d) id %d meta %d, got (%d,%d,%d) id %d meta %d",
                 s->got + 1, wx, wy, wz, wid, wmeta, x, y, z, id & 0xffff, meta);
    }

    ++s->got;
}

int main(int argc, char **argv)
{
    probe_args(&argc, argv);
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_ticks [--cases N] PROBE_DIR\n");
        return 2;
    }

    const char *dir = argv[1];
    char path[1024];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest = probe_read_file(path, &mlen);

    char kind[64];

    manifest_str(manifest, "kind", kind, sizeof kind);
    if (strcmp(kind, "ticks") != 0)
    {
        printf("SKIP %s: kind %s is not a tick probe\n", dir, kind);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int cases = (int)manifest_int(manifest, "cases");
    int skylight = (int)manifest_int(manifest, "skylightSubtracted");

    /* the manifest's "scenes":[...]: name, block id and variants per scene */
    int nscenes = 0;
    int scene_variants[64];
    int scene_block[64];
    char scene_names[MAX_SCENES][64];

    for (const char *p = strstr(manifest, "\"scenes\":["); p != NULL; )
    {
        const char *n = strstr(p, "\"name\":\"");
        const char *b = strstr(p, "\"block\":");
        const char *v = strstr(p, "\"variants\":");
        const char *q = n + strlen("\"name\":\"");
        const char *e = strchr(q, '"');

        if (n == NULL || b == NULL || v == NULL || e == NULL) break;

        size_t len = (size_t)(e - q);

        if (len >= sizeof scene_names[0]) len = sizeof scene_names[0] - 1;
        memcpy(scene_names[nscenes], q, len);
        scene_names[nscenes][len] = 0;
        scene_block[nscenes] = (int)strtol(b + strlen("\"block\":"), NULL, 10);
        scene_variants[nscenes] = (int)strtol(v + strlen("\"variants\":"), NULL, 10);
        ++nscenes;
        p = strchr(e, '}');
    }

    if (nscenes == 0)
    {
        fprintf(stderr, "%s: the manifest has no scenes\n", dir);
        return 2;
    }

    randomtick_set_skylight(skylight);

    int nchunks = 0, *lcz = NULL;
    int *lcx = probe_loaded(dir, manifest, &nchunks, &lcz);

    struct world w;

    world_init(&w, seed);

    for (int i = 0; i < nchunks; ++i) world_load_chunk(&w, lcx[i], lcz[i]);

    /* scenes.bin: the byte offset and the op count of every opset present.
     * A (scene, variant) the sweep never drew has no record and no case
     * needs it: every case that names an opset hit it at least once, and
     * the first such hit wrote the record. */
    snprintf(path, sizeof path, "%s/scenes.bin", dir);
    size_t slen;
    unsigned char *sbuf = probe_read_file(path, &slen);

    /* dense index by (scene, variant) */
    int opsets = 0;

    for (int s = 0; s < nscenes; ++s) opsets += scene_variants[s];

    size_t *opoff = malloc((size_t)opsets * sizeof *opoff);
    uint32_t *opn = malloc((size_t)opsets * sizeof *opn);

    for (int i = 0; i < opsets; ++i)
    {
        opoff[i] = 0;
        opn[i] = 0;
    }

    {
        size_t p = 0;

        while (p + 8 <= slen)
        {
            int si = sbuf[p] | sbuf[p + 1] << 8;
            int vi = sbuf[p + 2] | sbuf[p + 3] << 8;
            uint32_t count = le32(sbuf + p + 4);

            if (si >= nscenes || vi >= scene_variants[si])
            {
                fprintf(stderr, "%s: scenes.bin names scene %d variant %d\n", dir, si, vi);
                return 2;
            }

            int idx = 0;

            for (int s = 0; s < si; ++s) idx += scene_variants[s];

            idx += vi;
            opoff[idx] = p + 8;
            opn[idx] = count;
            p += 8 + (size_t)count * OP_BYTES;
        }

        if (p != slen)
        {
            fprintf(stderr, "%s: scenes.bin is %zu bytes, the opsets stop at %zu\n", dir, slen, p);
            return 2;
        }
    }

    snprintf(path, sizeof path, "%s/cases.bin", dir);
    size_t clen;
    unsigned char *cbuf = probe_read_file(path, &clen);

    if (clen % CASE_BYTES != 0 || (int)(clen / CASE_BYTES) != cases)
    {
        fprintf(stderr, "%s: cases.bin is %zu bytes, the manifest says %d cases\n", dir, clen, cases);
        return 2;
    }

    snprintf(path, sizeof path, "%s/writes.bin", dir);
    size_t wlen;
    unsigned char *wbuf = probe_read_file(path, &wlen);
    size_t woff = 0;

    unsigned char *hashbuf = malloc(CHUNK_BYTES);
    int fail = 0;
    long long total = 0;
    long long per_scene[MAX_SCENES];

    memset(per_scene, 0, sizeof per_scene);

    w.on_block = on_write;
    w.on_block_ctx = &cur;

    int run = probe_limit(cases);

    for (int i = 0; i < run && !fail; ++i)
    {
        const unsigned char *c = cbuf + CASE_BYTES * (size_t)i;
        int si = c[0] | c[1] << 8;
        int vi = c[2] | c[3] << 8;
        int x = (int)le32(c + 4), y = (int)le32(c + 8), z = (int)le32(c + 12);
        int64_t case_seed = (int64_t)le64(c + 16);
        int64_t tick_seed = (int64_t)le64(c + 24);
        uint32_t nw = le32(c + 32);
        uint64_t want_hash = le64(c + 36);
        uint64_t want_wr = le64(c + 44);

        if (si >= nscenes)
        {
            printf("FAIL %s case %d: scene %d, the manifest has %d\n", dir, i, si, nscenes);
            ++fail;
            break;
        }

        if (woff + (size_t)nw * 16 > wlen)
        {
            fprintf(stderr, "%s: writes.bin is short at case %d\n", dir, i);
            return 2;
        }

        cur.base = wbuf + woff;
        cur.want = nw;
        cur.got = 0;
        cur.overflow = 0;
        cur.bad = 0;

        int idx = 0;

        for (int s = 0; s < si; ++s) idx += scene_variants[s];

        idx += vi;

        /* the scene build runs with the listener off: the oracle's probe
         * records only what the tick itself writes */
        w.on_block = NULL;

        const unsigned char *ops = sbuf + opoff[idx];

        for (uint32_t k = 0; k < opn[idx]; ++k)
        {
            const unsigned char *o = ops + OP_BYTES * k;
            int dx = (signed char)o[0], dy = (signed char)o[1], dz = (signed char)o[2];
            int id = o[3] | o[4] << 8, meta = o[5];

            world_set_block(&w, x + dx, y + dy, z + dz, id, meta, 2);
        }

        jrand wr, cr;

        jr_seed(&wr, case_seed);
        jr_seed(&cr, tick_seed);

        /* the ticked block is what the scene built at the anchor; the scene
         * table's block field says the same thing (the manifest's per-scene
         * block), so dispatch by scene id */
        int ticked = scene_block[si];

        if (world_get_block(&w, x, y, z) != ticked)
            printf("WARN %s case %d: the world holds %d at the anchor, the scene table says %d\n", dir, i,
                   world_get_block(&w, x, y, z), ticked);

        w.on_block = on_write;
        w.on_block_ctx = &cur;
        randomtick_update_tick(&w, ticked, &wr, &cr, x, y, z);

        if (cur.got != nw)
        {
            printf("FAIL %s case %d scene %d variant %d pos (%d,%d,%d): %u writes, the oracle recorded %u%s\n", dir, i,
                   si, vi, x, y, z, cur.got, nw, cur.overflow ? "" : "");
            ++fail;
            break;
        }

        if (cur.bad)
        {
            printf("FAIL %s case %d scene %d variant %d pos (%d,%d,%d): %s\n", dir, i, si, vi, x, y, z, diff_what);
            ++fail;
            break;
        }

        uint64_t got_hash = probe_hash_due(i, run) ? hash_around(&w, x >> 4, z >> 4, hashbuf) : want_hash;

        if (got_hash != want_hash)
        {
            printf("FAIL %s case %d scene %d variant %d pos (%d,%d,%d): hash of the 3x3 chunks want %016llx got "
                   "%016llx\n", dir, i, si, vi, x, y, z, (unsigned long long)want_hash, (unsigned long long)got_hash);
            ++fail;
            break;
        }

        if ((uint64_t)(wr.seed & 0xffffffffffffULL) != want_wr)
        {
            printf("FAIL %s case %d scene %d variant %d pos (%d,%d,%d): World.rand state want %012llx got %012llx\n",
                   dir, i, si, vi, x, y, z, (unsigned long long)want_wr,
                   (unsigned long long)(wr.seed & 0xffffffffffffULL));
            ++fail;
            break;
        }

        per_scene[si] += nw;
        total += nw;
        woff += (size_t)nw * 16;
    }

    if (!fail && run == cases)
    {
        snprintf(path, sizeof path, "%s/final.bin.gz", dir);
        fail = cmp_final(dir, path, &w, lcx, lcz, nchunks);
    }

    if (fail) probe_localize(argv[0], dir);

    if (!fail)
    {
        printf("PASS %s: %d/%d cases, %lld writes, %d chunks, seed %lld, skylightSubtracted %d\n", dir, run, cases,
               total, nchunks, (long long)seed, skylight);
        printf("     writes per scene:");

        for (int s = 0; s < nscenes; ++s) printf(" %s:%lld", scene_names[s], per_scene[s]);

        printf("\n");
    }

    free(hashbuf);
    free(wbuf);
    free(cbuf);
    free(sbuf);
    free(opoff);
    free(opn);
    free(lcx);
    free(lcz);
    free(manifest);
    world_free(&w);
    return fail;
}
