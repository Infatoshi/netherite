/* biome_scan.c - scan MC 1.11.2 GenLayer spawn-area biome coverage.
 *
 * Build from magma:
 *   gcc -O2 -ffp-contract=off -Wall -Icore -I. -I../blaze/core \
 *       -c trace/biome_scan.c -o trace/biome_scan.o
 *   gcc trace/biome_scan.o -o trace/biome_scan
 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#endif
#include "genlayer_biomes.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STRIDE = 8, HIT_THRESHOLD = 40, SUMMARY_LIMIT = 5 };

typedef struct {
    const char *name;
    int ids[4];
    int nid;
} Family;

static const Family FAMILIES[] = {
    { "desert", { 2, 17 }, 2 },
    { "jungle", { 21, 22, 23 }, 3 },
    { "savanna", { 35, 36 }, 2 },
    { "mesa", { 37, 38, 39 }, 3 },
    { "mushroom", { 14, 15 }, 2 },
    { "megataiga", { 32, 33 }, 2 },
    { "smaller_extreme_hills", { 20 }, 1 },
    { "ice", { 12, 13 }, 2 },
    { "sunflower_plains", { 129 }, 1 },
    { "roofed", { 29 }, 1 },
};

#define NFAMILIES ((int)(sizeof(FAMILIES) / sizeof(FAMILIES[0])))
#define MUSHROOM_FAMILY 4

static GLNode g_nodes[GL_MAX_NODES];
static GlArena g_arena;
static int g_counts[NFAMILIES];
static long long g_first[NFAMILIES][SUMMARY_LIMIT];
static int g_nfirst[NFAMILIES];
static long long g_mushroom_weak[SUMMARY_LIMIT];
static int g_nmushroom_weak;

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s --from SEED --to SEED [--box 400]\n", argv0);
}

static int parse_ll(const char *s, long long *out) {
    char *end = NULL;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno || end == s || *end != '\0') return 0;
    *out = v;
    return 1;
}

static int parse_int(const char *s, int *out) {
    long long v;
    if (!parse_ll(s, &v) || v < 0 || v > INT_MAX) return 0;
    *out = (int)v;
    return 1;
}

static int family_for_biome(int biome) {
    for (int f = 0; f < NFAMILIES; ++f) {
        for (int i = 0; i < FAMILIES[f].nid; ++i) {
            if (biome == FAMILIES[f].ids[i]) return f;
        }
    }
    return -1;
}

static void scan_seed(long long seed, int box) {
    int voronoi;
    int width = box * 2 + 1;

    memset(g_counts, 0, sizeof(g_counts));
    gl_build(g_nodes, (i64)seed, &voronoi);

    for (int z = -box; z <= box; z += STRIDE) {
        g_arena.off = 0;
        int *row = gl_getInts(g_nodes, &g_arena, voronoi, -box, z, width, 1);
        for (int x = -box; x <= box; x += STRIDE) {
            int f = family_for_biome(row[x + box]);
            if (f >= 0) ++g_counts[f];
        }
    }
}

static void remember_hits(long long seed) {
    for (int f = 0; f < NFAMILIES; ++f) {
        if (g_counts[f] >= HIT_THRESHOLD && g_nfirst[f] < SUMMARY_LIMIT)
            g_first[f][g_nfirst[f]++] = seed;
    }
    if (g_counts[MUSHROOM_FAMILY] >= 10 && g_nmushroom_weak < SUMMARY_LIMIT)
        g_mushroom_weak[g_nmushroom_weak++] = seed;
}

static void print_seed_line(long long seed) {
    printf("seed %lld:", seed);
    for (int f = 0; f < NFAMILIES; ++f) {
        if (g_counts[f] >= HIT_THRESHOLD) printf(" %s", FAMILIES[f].name);
    }
    putchar('\n');
}

static void print_summary(void) {
    puts("summary:");
    for (int f = 0; f < NFAMILIES; ++f) {
        printf("%s:", FAMILIES[f].name);
        for (int i = 0; i < g_nfirst[f]; ++i) printf(" %lld", g_first[f][i]);
        putchar('\n');
    }
    printf("mushroom_weak_ge_10:");
    for (int i = 0; i < g_nmushroom_weak; ++i) printf(" %lld", g_mushroom_weak[i]);
    putchar('\n');
}

int main(int argc, char **argv) {
    long long from = 0;
    long long to = 0;
    int have_from = 0;
    int have_to = 0;
    int box = 400;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--from") && i + 1 < argc) {
            if (!parse_ll(argv[++i], &from)) {
                fprintf(stderr, "bad --from value\n");
                return 2;
            }
            have_from = 1;
        } else if (!strcmp(argv[i], "--to") && i + 1 < argc) {
            if (!parse_ll(argv[++i], &to)) {
                fprintf(stderr, "bad --to value\n");
                return 2;
            }
            have_to = 1;
        } else if (!strcmp(argv[i], "--box") && i + 1 < argc) {
            if (!parse_int(argv[++i], &box)) {
                fprintf(stderr, "bad --box value\n");
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!have_from || !have_to) {
        usage(argv[0]);
        return 2;
    }

    if (from <= to) {
        for (long long seed = from; seed <= to; ++seed) {
            scan_seed(seed, box);
            print_seed_line(seed);
            remember_hits(seed);
            if (seed == LLONG_MAX) break;
        }
    } else {
        for (long long seed = from; seed >= to; --seed) {
            scan_seed(seed, box);
            print_seed_line(seed);
            remember_hits(seed);
            if (seed == LLONG_MIN) break;
        }
    }

    print_summary();
    return 0;
}
