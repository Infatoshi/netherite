/* The environment image relocated between rows (GPU plan L12): the
 * whole-server replay of test_snapshots.c runs for each recording DIR's
 * manifest names, twice, each run in its own image environment (env_new,
 * image.h) on its own thread, a baton handing over before every row in turn
 * (a1, b1, a2, b2, ...), so the recordings interleave. Before every EVERY-th
 * row (the first after the start, then each EVERY rows), run a's image is
 * copied to a new mapping at another address and a continues there: the
 * old mapping is released, so anything that still named it would fault.
 * Run b is a's twin at another address and the same row: a word of a's
 * image that points into it and differs from b's at the same offset is a
 * pointer and is rebased (image_relocate); a value that happens to look
 * like one is the same in both. Every run must replay exact, a's across its
 * moves as b's without any.
 *
 * DIR/manifest.json: {"recs": ["NAME", ...], "every": N (250)}; the
 * recordings live beside this area (DIR/../../snapshots/NAME).
 *
 * Per recording it prints the moves, the bytes copied and the words rebased
 * per move (the absolute pointers the image still holds), and the words
 * that differ between the twins without pointing into the image (pointers
 * to memory outside it that the run made: what the image does not hold).
 *
 * The negative check no-rebase (test_relocate no-rebase DIR) copies without
 * rebasing: the first run that moves must fail. */
#undef main
#define main snapshots_main
#include "test_snapshots.c"
#undef main
#define main gate_test_main

#include <pthread.h>
#include <sys/mman.h>

#include "../engine/env.h"
#include "../engine/image.h"

#define RELOC_MAX_RUNS 16

struct run {
    int index;                  /* in baton order */
    int twin;                   /* the other run of the recording */
    int mover;                  /* a: moves; b: its twin, never moves */
    int every;
    char name[256];
    char dir[4096];
    struct env *env;            /* where the run's image is now */
    int rows;                   /* row hooks so far */
    int rc;
    /* the moves */
    int moves;
    size_t copied, rebased, rebased_min, rebased_max, outside, outside_max;
    size_t rebased_in[IMG_PARTS];
    uintptr_t first_from, first_to;
};

static struct run runs[RELOC_MAX_RUNS];
static int nruns;
static int no_rebase;

static pthread_mutex_t baton_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t baton_cv = PTHREAD_COND_INITIALIZER;
static int baton_turn;
static int baton_done[RELOC_MAX_RUNS];
static _Thread_local int baton_me;

static void baton_wait_turn(void)
{
    while (baton_turn != baton_me) pthread_cond_wait(&baton_cv, &baton_mu);
}

/* the next run in baton order that has not finished, or me */
static int baton_next(int me)
{
    for (int k = 1; k <= nruns; ++k)
    {
        int i = (me + k) % nruns;
        if (!baton_done[i]) return i;
    }
    return me;
}

/* The words of a's live extents that differ from b's without pointing into
 * a's image: what a names outside its image that the run made. */
static size_t count_outside(const struct env *a, const struct env *b)
{
    struct img_extent ext[64];
    int n = image_extents(a, ext, 64);
    const unsigned char *ab = (const unsigned char *)a, *bb = (const unsigned char *)b;
    size_t count = 0;

    for (int i = 0; i < n; ++i)
    {
        size_t off = ext[i].off & ~(size_t)7, end = (ext[i].off + ext[i].len + 7) & ~(size_t)7;
        const uint64_t *x = (const uint64_t *)(ab + off), *y = (const uint64_t *)(bb + off);
        for (size_t k = 0; k < (end - off) / 8; ++k)
            if (x[k] != y[k] && (uintptr_t)x[k] - (uintptr_t)ab >= a->img.size) ++count;
    }
    return count;
}

/* no-rebase: a plain copy to another slot, the old mapping released */
static struct env *copy_only(struct env *a)
{
    struct env *twin = env_new();   /* a slot, then its image taken as the destination */
    if (twin == NULL) return NULL;
    struct img_extent ext[64];
    int n = image_extents(a, ext, 64);
    for (int i = 0; i < n; ++i) memcpy((unsigned char *)twin + ext[i].off, (unsigned char *)a + ext[i].off, ext[i].len);
    arena_live_replace(&twin->arena, NULL);
    arena_live_replace(&a->arena, &twin->arena);
    munmap(a, a->img.size);
    return twin;
}

/* The row hook: hand over, and once it is this run's turn again (every other
 * run waiting in its own hook), a mover due to move moves. Its twin is at the
 * same row: it runs next in baton order and has replayed exactly the rows
 * this run has. */
static void reloc_row(void)
{
    pthread_mutex_lock(&baton_mu);
    baton_turn = baton_next(baton_me);
    pthread_cond_broadcast(&baton_cv);
    baton_wait_turn();
    pthread_mutex_unlock(&baton_mu);

    struct run *r = &runs[baton_me];
    int k = r->rows++;
    if (!r->mover || k < 1 || (k - 1) % r->every != 0) return;
    struct run *b = &runs[r->twin];
    if (baton_done[r->twin] || b->rows != r->rows - 1) return;   /* the twin is not at this row */

    struct env *from = nw_env, *to;
    struct img_reloc_stats st = {0};
    size_t outside = count_outside(from, b->env);
    if (no_rebase) to = copy_only(from);
    else to = image_relocate(from, b->env, &st);
    if (to == NULL)
    {
        printf("FAIL %s: the image did not move at row hook %d\n", r->name, k);
        exit(1);
    }
    if (r->moves == 0)
    {
        r->first_from = (uintptr_t)from;
        r->first_to = (uintptr_t)to;
        r->rebased_min = st.pointers_rebased;
    }
    ++r->moves;
    r->copied += st.bytes_copied;
    r->rebased += st.pointers_rebased;
    for (int p = 0; p < IMG_PARTS; ++p) r->rebased_in[p] += st.rebased_in[p];
    if (st.pointers_rebased < r->rebased_min) r->rebased_min = st.pointers_rebased;
    if (st.pointers_rebased > r->rebased_max) r->rebased_max = st.pointers_rebased;
    r->outside += outside;
    if (outside > r->outside_max) r->outside_max = outside;
    r->env = to;
    nw_env = to;
}

static void *run_main(void *arg)
{
    struct run *r = arg;

    baton_me = r->index;
    pthread_mutex_lock(&baton_mu);
    baton_wait_turn();
    pthread_mutex_unlock(&baton_mu);

    r->env = env_new();
    if (r->env == NULL) { r->rc = 2; printf("FAIL %s: no image environment\n", r->name); }
    else
    {
        nw_env = r->env;
        /* the twins must take the same path to the same image: with the
         * region cache on, the first to reach the seed world would build
         * it and its twin map the stored image, and their heaps would part
         * (regioncache.h); both build */
        regioncache_flag("off");
        char *argv[3] = {"test_snapshots", r->dir, NULL};
        r->rc = snapshots_main(2, argv);
    }

    pthread_mutex_lock(&baton_mu);
    baton_done[r->index] = 1;
    baton_turn = baton_next(r->index);
    pthread_cond_broadcast(&baton_cv);
    pthread_mutex_unlock(&baton_mu);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: test_relocate [no-rebase] DIR\n");
        return 2;
    }
    const char *dir = argv[argc - 1];
    for (int i = 1; i < argc - 1; ++i)
    {
        if (!strcmp(argv[i], "no-rebase")) no_rebase = 1;
        else { printf("FAIL unknown flag %s\n", argv[i]); return 2; }
    }

    char path[4200];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (f == NULL) { printf("FAIL %s: no manifest.json\n", dir); return 1; }
    static char text[65536];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    const struct jval *m = json_parse(text);
    const struct jval *recs = json_get(m, "recs");
    int64_t every = 250;
    if (json_get(m, "every") != NULL && (!json_int(json_get(m, "every"), &every) || every < 1))
    {
        printf("FAIL %s: every is not a positive count\n", dir);
        return 1;
    }
    char real[4096];
    if (realpath(dir, real) == NULL) { printf("FAIL %s: does not resolve\n", dir); return 1; }
    for (int i = 0; recs != NULL && json_at(recs, i) != NULL; ++i)
    {
        const char *name = json_str(json_at(recs, i));
        if (name == NULL || nruns + 2 > RELOC_MAX_RUNS) { printf("FAIL %s: bad recs list\n", dir); return 1; }
        for (int k = 0; k < 2; ++k)
        {
            struct run *r = &runs[nruns];
            r->index = nruns;
            r->twin = k == 0 ? nruns + 1 : nruns - 1;
            r->mover = k == 0;
            r->every = (int)every;
            snprintf(r->name, sizeof r->name, "%.200s%s", name, k == 0 ? "" : " (twin)");
            snprintf(r->dir, sizeof r->dir, "%.3000s/../../snapshots/%.900s", real, name);
            ++nruns;
        }
    }
    if (nruns == 0) { printf("FAIL %s: the manifest names no recordings\n", dir); return 1; }

    /* each replay keeps its harness state in thread-local storage and
     * recurses deep in places: a large stack */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)256 << 20);
    /* no guard pages: glibc marks them inside the stack mapping (a guard
     * region /proc/self/maps does not show), and the grave's sweep reads
     * every writable anonymous mapping (grave.h) */
    pthread_attr_setguardsize(&attr, 0);
    snapshots_row_hook = reloc_row;
    baton_turn = 0;

    pthread_t th[RELOC_MAX_RUNS];
    for (int i = 0; i < nruns; ++i)
        if (pthread_create(&th[i], &attr, run_main, &runs[i]) != 0)
        {
            printf("FAIL %s: cannot start run %d\n", dir, i);
            return 1;
        }
    for (int i = 0; i < nruns; ++i) pthread_join(th[i], NULL);

    int ok = 1;
    for (int i = 0; i < nruns; ++i)
    {
        struct run *r = &runs[i];
        if (r->rc != 0) ok = 0;
        if (!r->mover) continue;
        if (r->moves == 0) ok = 0;
        printf("relocate %s: rc %d, %d moves (the first %#lx to %#lx), %.1f MB copied per move, "
               "pointers rebased per move %zu to %zu (mean %.0f), words naming run memory outside the image "
               "per move up to %zu (mean %.0f)\n",
               r->name, r->rc, r->moves, (unsigned long)r->first_from, (unsigned long)r->first_to,
               r->moves ? (double)r->copied / r->moves / 1e6 : 0.0, r->rebased_min, r->rebased_max,
               r->moves ? (double)r->rebased / r->moves : 0.0, r->outside_max,
               r->moves ? (double)r->outside / r->moves : 0.0);
        if (r->moves)
            printf("relocate %s: rebased pointers per move by where they sit: struct env %.0f, pools %.0f, "
                   "list slab %.0f, heap %.0f\n", r->name, (double)r->rebased_in[IMG_PART_ENV] / r->moves,
                   (double)r->rebased_in[IMG_PART_POOLS] / r->moves, (double)r->rebased_in[IMG_PART_SLAB] / r->moves,
                   (double)r->rebased_in[IMG_PART_HEAP] / r->moves);
    }
    if (no_rebase)
    {
        printf("%s: %s\n", dir, ok ? "FAIL: the copy without rebasing replayed exact (the negative check)" :
                                     "OK: the copy without rebasing fails (the negative check)");
        return ok ? 1 : 0;
    }
    printf("%s: %s (every run exact, each mover's image moved every %lld rows)\n", dir,
           ok ? "OK: relocated replays exact" : "FAIL", (long long)every);
    return ok ? 0 : 1;
}
