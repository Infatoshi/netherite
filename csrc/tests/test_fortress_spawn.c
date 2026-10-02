/* Gate: ChunkProviderHell.getPossibleCreatures for the monster type
 * (csrc/engine/spawning.c spawning_hell_list over populate_nether.c's fortress
 * map) against the oracle's FortressSpawnProbe recording
 * (out/java/fortress_spawn/<name>/, made with
 *
 *   make -C oracle run SEED=2 CLASS=FortressSpawnProbe NAME=fs-2 \
 *     CMD='{"out":"<abs>/out/java/fortress_spawn/fs-2"}'
 *
 * ). The probe loads the Nether spawn area the way SpawnDumpDim does and
 * asks for the monster list over a grid around every fortress start; this
 * test builds the same area natively (seedworld_build_dim, which
 * test_seedworld checks chunk for chunk), then for every point compares the
 * list's source (0 the hell biome, 1 hasStructureAt, 2 func_142038_b over
 * nether brick) and the block below. The two lists' rows are compared with
 * the manifest's, and the fortress starts' boxes in the map's HashMap order.
 *
 * Negative check: --negative=anystart lets func_142038_b accept any start
 * whose box meets the column, not only the first in the HashMap's order
 * (vanilla's walk stops at the first sizeable start); it must fail at a point
 * over nether brick inside a later start's box.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/nbtjson.h"
#include "../engine/env.h"
#include "../engine/populate_nether.h"
#include "../engine/seedworld.h"
#include "../engine/spawning.h"
#include "../engine/tape.h"

static int negative_anystart;

static const char *kind_name(int k)
{
    switch (k)
    {
    case SP_BLAZE: return "EntityBlaze";
    case SP_PIG_ZOMBIE: return "EntityPigZombie";
    case SP_SKELETON: return "EntitySkeleton";
    case SP_MAGMA_CUBE: return "EntityMagmaCube";
    case SP_GHAST: return "EntityGhast";
    default: return "?";
    }
}

static int compare_list(const char *dir, const char *what, const struct jval *rows, const struct sp_list *l)
{
    if (json_len(rows) != l->n)
    {
        printf("FAIL %s: the %s list has %d rows, native %d\n", dir, what, json_len(rows), l->n);
        return 0;
    }
    for (int i = 0; i < l->n; ++i)
    {
        const struct jval *r = json_at(rows, i);
        int64_t w = 0, mn = 0, mx = 0;
        json_int(json_get(r, "weight"), &w);
        json_int(json_get(r, "min"), &mn);
        json_int(json_get(r, "max"), &mx);
        const char *cls = json_str(json_get(r, "class"));
        if (!cls || strcmp(cls, kind_name(l->kind[i])) || w != l->weight[i] ||
            mn != l->min_group[i] || mx != l->max_group[i])
        {
            printf("FAIL %s: %s row %d: the oracle %s %lld %lld-%lld, native %s %d %d-%d\n", dir, what, i,
                   cls ? cls : "?", (long long)w, (long long)mn, (long long)mx,
                   kind_name(l->kind[i]), l->weight[i], l->min_group[i], l->max_group[i]);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: test_fortress_spawn [--negative=anystart] DIR\n");
        return 2;
    }
    const char *dir = argv[argc - 1];   /* the input guard reads the last argument */
    for (int i = 1; i < argc - 1; ++i)
        if (!strcmp(argv[i], "--negative=anystart")) negative_anystart = 1;

    char path[1024];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)len + 1);
    if (fread(text, 1, (size_t)len, f) != (size_t)len) { fclose(f); return 2; }
    text[len] = 0;
    fclose(f);
    struct jval *m = json_parse(text);
    if (!m) { fprintf(stderr, "%s: manifest is not JSON\n", dir); return 2; }

    const char *kind = json_str(json_get(m, "kind"));
    if (!kind || strcmp(kind, "fortress_spawn"))
    {
        printf("SKIP %s: not a fortress spawn probe\n", dir);
        return 0;
    }
    int64_t seed = 0, npoints = 0;
    json_int(json_get(m, "seed"), &seed);
    json_int(json_get(m, "points"), &npoints);

    /* the Nether spawn area, as test_seedworld builds it */
    struct seedworld ow, sw;
    seedworld_init(&ow, seed);
    seedworld_build(&ow);
    seedworld_init_dim(&sw, seed, -1);
    seedworld_move_det(&sw, &ow);
    seedworld_free(&ow);
    seedworld_build_dim(&sw);

    /* the lists: the fortress one from a point the map answers 1 for is
     * checked with the points; here the two tables themselves */
    int via = 0;
    const struct sp_list *hell = spawning_list_for(8, CT_MONSTER);
    if (!compare_list(dir, "hell", json_get(m, "hell"), hell)) return 1;

    nw_env->cfg.populate_hell_negative_anystart = negative_anystart;

    snprintf(path, sizeof path, "%s/points.bin.gz", dir);
    gzFile g = gzopen(path, "rb");
    if (!g) { perror(path); return 2; }
    unsigned char rec[15];
    long long n = 0, counts[3] = {0, 0, 0};
    int fortress_checked = 0;
    while (gzread(g, rec, sizeof rec) == (int)sizeof rec)
    {
        int x = (int)(rec[0] | rec[1] << 8 | rec[2] << 16 | (unsigned)rec[3] << 24);
        int y = (int)(rec[4] | rec[5] << 8 | rec[6] << 16 | (unsigned)rec[7] << 24);
        int z = (int)(rec[8] | rec[9] << 8 | rec[10] << 16 | (unsigned)rec[11] << 24);
        int want = rec[12];
        int want_below = rec[13] | rec[14] << 8;
        int below = world_get_block(&sw.p.world, x, y - 1, z) & 4095;

        if (below != want_below)
        {
            printf("FAIL %s: block below (%d,%d,%d): the oracle %d, native %d\n", dir, x, y, z, want_below, below);
            return 1;
        }
        const struct sp_list *l = spawning_hell_list(&sw.p, CT_MONSTER, x, y, z, below, &via);
        if (via != want)
        {
            printf("FAIL %s: point %lld (%d,%d,%d): the oracle's list source %d, native %d\n",
                   dir, n, x, y, z, want, via);
            return 1;
        }
        if (via != 0 && !fortress_checked)
        {
            if (!compare_list(dir, "fortress", json_get(m, "fortress"), l)) return 1;
            fortress_checked = 1;
        }
        if (via == 0 && l != hell)
        {
            printf("FAIL %s: point %lld (%d,%d,%d) took neither the fortress nor the hell list\n", dir, n, x, y, z);
            return 1;
        }
        ++counts[want];
        ++n;
    }
    gzclose(g);

    if (n != npoints)
    {
        printf("FAIL %s: %lld points read, the manifest says %lld\n", dir, n, (long long)npoints);
        return 1;
    }
    if (!fortress_checked || counts[1] == 0)
    {
        printf("FAIL %s: no point inside a fortress, the recording checks nothing\n", dir);
        return 1;
    }

    printf("PASS %s: %lld points, %lld hell, %lld fortress by component, %lld fortress by brick, seed %lld\n",
           dir, n, counts[0], counts[1], counts[2], (long long)seed);
    seedworld_free(&sw);
    return 0;
}
