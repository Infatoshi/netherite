/* lighting_check [-j N] FILE... | -: stored light captures (test_snapshots
 * --light-cap save:FILE, lightcap.h lc_sink_file_create) replayed on the
 * device alone, C's side already in them: every call and its result against
 * C's, byte for byte, as --light-cap cuda does during a replay, without the
 * replay. N files at a time (default 8), each on its own thread and CUDA
 * stream (host.cu is built with per-thread default streams) in one
 * context, so the one-block replays run side by side on the device. "-"
 * reads the paths from stdin as they come (tests/light_check.sh feeds each
 * capture as it is made). A FILE ending in .zst is read through zstd. Each
 * file's lines as it finishes, every one "FILE<TAB>" and a line of
 * lighting_report's (the verdict last); exit 1 when any differs or fails. A
 * device with no memory for a replay (a co-tenant holds the 3090) is tried
 * again, up to five times, a little later each. */
#define _GNU_SOURCE
#include "lighting.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char **paths;
static int npaths, next_path, from_stdin, bad;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static double now_s(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* the stream's buffers read ahead of the device, two at a time, so reading
 * (zstd) and the device overlap */
struct ahead {
    FILE *f;
    uint64_t cap, len[2];
    uint8_t *b[2];
    int st[2];                  /* 0 free, 1 filled, 2 the end, 3 cut short */
    int stop;
    pthread_mutex_t m;
    pthread_cond_t c;
};

static void *reader(void *arg)
{
    struct ahead *A = arg;

    for (int i = 0;; i ^= 1)
    {
        pthread_mutex_lock(&A->m);
        while (A->st[i] != 0 && !A->stop) pthread_cond_wait(&A->c, &A->m);
        int stop = A->stop;
        pthread_mutex_unlock(&A->m);
        if (stop) return NULL;

        uint64_t len;
        int st = 1;

        if (fread(&len, sizeof len, 1, A->f) != 1 || len > A->cap || (len && fread(A->b[i], 1, len, A->f) != len)) st = 3;
        else if (len == 0) st = 2;
        pthread_mutex_lock(&A->m);
        A->len[i] = len;
        A->st[i] = st;
        pthread_cond_broadcast(&A->c);
        pthread_mutex_unlock(&A->m);
        if (st != 1) return NULL;
    }
}

/* one stored capture through the device; its verdict to out. *again: the
 * device failed (no memory for the context, the mirror or a launch), not a
 * difference */
static int replay_once(const char *path, FILE *out, int *again)
{
    size_t n = strlen(path);
    int piped = n > 4 && !strcmp(path + n - 4, ".zst");
    FILE *f;

    if (piped)
    {
        char cmd[4200];

        if (n >= 4096 || strchr(path, '\'') != NULL) return fprintf(out, "light device: a path zstd cannot take\n"), -1;
        snprintf(cmd, sizeof cmd, "zstd -q -dc '%s'", path);
        /* a 1 MB pipe (Linux's most): at 64 KB a loaded host spent more
         * time waking the reader than zstd took (7 s for 2.2 GB at load 150) */
        if ((f = popen(cmd, "r")) != NULL) fcntl(fileno(f), F_SETPIPE_SZ, 1 << 20);
    }
    else f = fopen(path, "rb");
    if (f == NULL) return fprintf(out, "light device: %s does not open\n", path), -1;

    struct lc_file_head *h = malloc(sizeof *h);
    struct lighting *g = NULL;
    struct ahead A = {.f = f, .m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
    int rc = -1, failed = 0;
    double wait = 0;
    struct lighting_result r;

    memset(&r, 0, sizeof r);
    if (fread(h, sizeof *h, 1, f) != 1 || h->magic != LC_FILE_MAGIC || h->version != LC_FILE_VERSION ||
        h->layout_bytes != sizeof h->L || h->cap == 0 || h->cap > ((size_t)1 << 32))
        fprintf(out, "light device: not a stored light capture of this version\n");
    else if ((A.b[0] = malloc(h->cap)) == NULL || (A.b[1] = malloc(h->cap)) == NULL)
        fprintf(out, "light device: no memory for the stream\n");
    else if ((g = lighting_create(&h->L, h->opacity, h->light, h->air, h->cap)) == NULL)
    {
        fprintf(out, "light device: the device did not start (lighting: on stderr)\n");
        *again = 1;
    }
    if (g != NULL)
    {
        pthread_t rt;

        A.cap = h->cap;
        pthread_create(&rt, NULL, reader, &A);
        for (int i = 0;; i ^= 1)
        {
            pthread_mutex_lock(&A.m);
            while (A.st[i] == 0) pthread_cond_wait(&A.c, &A.m);
            int st = A.st[i];
            pthread_mutex_unlock(&A.m);
            if (st == 3)
            {
                fprintf(out, "light device: the stored capture is cut short\n");
                failed = 2;
                break;
            }
            if (st == 2) break;

            double t = now_s();
            int k = lighting_run(g, A.b[i], A.len[i]);

            wait += now_s() - t;
            if (k != 0)
            {
                failed = k < 0;
                break;
            }
            pthread_mutex_lock(&A.m);
            A.st[i] = 0;
            pthread_cond_broadcast(&A.c);
            pthread_mutex_unlock(&A.m);
        }
        pthread_mutex_lock(&A.m);
        A.stop = 1;
        pthread_cond_broadcast(&A.c);
        pthread_mutex_unlock(&A.m);
        pthread_join(rt, NULL);
        lighting_result(g, &r);
        rc = failed == 2 ? -1 : lighting_report(&r, failed, wait, out);
        *again = failed == 1;
    }
    lighting_free(g);
    free(A.b[0]);
    free(A.b[1]);
    free(h);
    if (piped) pclose(f);
    else fclose(f);
    return rc;
}

/* a replay the device failed (the 3090 is shared: a co-tenant can hold its
 * memory) runs again from the start, up to five times, a little later each */
static int replay(const char *path, FILE *out)
{
    for (int a = 1;; ++a)
    {
        char *text = NULL;
        size_t n = 0;
        FILE *o = open_memstream(&text, &n);
        int again = 0, rc = replay_once(path, o, &again);

        fclose(o);
        if (!again || a == 5)
        {
            if (a > 1) fprintf(out, "light device: run %d times (the device failed before)\n", a);
            fputs(text, out);
            free(text);
            return rc;
        }
        free(text);
        sleep(30 * a);
    }
}

/* the next path, NULL at the end */
static char *next(void)
{
    char *p = NULL;

    pthread_mutex_lock(&lock);
    if (!from_stdin) p = next_path < npaths ? paths[next_path++] : NULL;
    else
    {
        size_t cap = 0;
        ssize_t n;

        while ((n = getline(&p, &cap, stdin)) >= 0)
        {
            while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r')) p[--n] = 0;
            if (n > 0) break;
        }
        if (n < 0)
        {
            free(p);
            p = NULL;
        }
    }
    pthread_mutex_unlock(&lock);
    return p;
}

static void *worker(void *arg)
{
    (void)arg;
    for (char *path; (path = next()) != NULL;)
    {
        char *text = NULL;
        size_t n = 0;
        FILE *out = open_memstream(&text, &n);
        int rc = replay(path, out);

        fclose(out);
        pthread_mutex_lock(&lock);
        bad |= rc != 0;
        for (char *p = text, *e; p && *p; p = e + 1)
        {
            e = strchr(p, '\n');
            if (e) *e = 0;
            printf("%s\t%s\n", path, p);
            if (!e) break;
        }
        fflush(stdout);
        pthread_mutex_unlock(&lock);
        free(text);
        if (from_stdin) free(path);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int j = 8, a = 1;

    if (a + 1 < argc && !strcmp(argv[a], "-j"))
    {
        j = atoi(argv[a + 1]);
        a += 2;
    }
    npaths = argc - a;
    paths = argv + a;
    from_stdin = npaths == 1 && !strcmp(paths[0], "-");
    if (npaths <= 0)
    {
        fprintf(stderr, "usage: lighting_check [-j N] FILE... | -\n");
        return 2;
    }
    if (j <= 0) j = 8;
    if (!from_stdin && j > npaths) j = npaths;

    pthread_t *t = calloc((size_t)j, sizeof *t);

    for (int i = 0; i < j; ++i) pthread_create(&t[i], NULL, worker, NULL);
    for (int i = 0; i < j; ++i) pthread_join(t[i], NULL);
    free(t);
    return bad;
}
