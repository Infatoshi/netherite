/* The client world's deferred light (lightdefer.h): the pending list and its
 * runs. The hooks that record and sync are world.c's. */
#include "lightdefer.h"
#include "world.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static int run_c(void *ctx, struct world *w, const struct ld_op *ops, size_t n)
{
    (void)ctx;
    return world_light_defer_run_c(w, ops, n);
}

struct lightdefer *lightdefer_new(const struct lightdefer_exec *exec)
{
    struct lightdefer *d = mmap(NULL, sizeof *d, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (d == MAP_FAILED) return NULL;
    memset(d, 0, offsetof(struct lightdefer, ops));
    d->syncs = LD_SYNC_ALL;
    if (exec != NULL) d->exec = *exec;
    else d->exec = (struct lightdefer_exec){NULL, run_c, NULL, NULL};
    return d;
}

void lightdefer_free(struct lightdefer *d)
{
    if (d == NULL) return;
    if (d->exec.free != NULL) d->exec.free(d->exec.ctx);
    munmap(d, sizeof *d);
}

int lightdefer_sync_cause(struct lightdefer *d, int cause)
{
    if (d == NULL || d->n == 0 || d->in_run) return 0;
    if (d->never)
    {
        d->n = 0;
        return 0;
    }

    size_t n = d->n;
    int rc;

    d->in_run = 1;
    d->st.runs++;
    d->st.ran += n;
    d->st.syncs[cause]++;
    rc = d->exec.run(d->exec.ctx, d->w, d->ops, n);
    d->n = 0;
    d->in_run = 0;
    if (rc != 0) d->st.failed = 1;
    return rc;
}

void lightdefer_report(const struct lightdefer *d, FILE *out)
{
    const struct lightdefer_stats *s = &d->st;

    fprintf(out,
            "light defer: %llu client light calls and %llu relight checks deferred%s; %llu runs (%llu operations; "
            "syncs: %llu at a write, %llu at a light read, %llu at a frame, %llu with the list full, %llu at a world "
            "change), at most %zu pending, %llu dropped with their world%s\n",
            (unsigned long long)s->updates, (unsigned long long)s->relights, d->never ? " (never run)" : "",
            (unsigned long long)s->runs, (unsigned long long)s->ran, (unsigned long long)s->syncs[LD_CAUSE_WRITE],
            (unsigned long long)s->syncs[LD_CAUSE_READ], (unsigned long long)s->syncs[LD_CAUSE_FRAME],
            (unsigned long long)s->syncs[LD_CAUSE_FULL], (unsigned long long)s->syncs[LD_CAUSE_WORLD], s->max_pending,
            (unsigned long long)s->dropped, s->failed ? "; an executor run FAILED" : "");
}
