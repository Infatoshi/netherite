/* The block renderer's tables, one copy a process and directory (rbshare.h). */
#include "rbshare.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "env.h"
#include "envheap.h"
#include "image.h"
#include "render_blocks.h"

struct rbshare {
    char dir[1024];
    int ok;
    struct rb_table tab;
    struct rb_atlas atlas;
    struct rbshare *next;
};

static struct rbshare *shares;   /* the directories read so far; one is complete before it is linked */
static pthread_mutex_t shares_lock = PTHREAD_MUTEX_INITIALIZER;

static const struct rbshare *get(const char *dir)
{
    if (dir == NULL) return NULL;
    for (struct rbshare *s = __atomic_load_n(&shares, __ATOMIC_ACQUIRE); s; s = s->next)
        if (!strcmp(s->dir, dir)) return s;
    pthread_mutex_lock(&shares_lock);
    struct rbshare *s;
    for (s = shares; s; s = s->next)
        if (!strcmp(s->dir, dir)) break;
    if (s == NULL)
    {
        /* the process's memory, not the current env's heap */
        int heap = nw_env ? image_heap_set(nw_env, 0) : 0;
        s = proc_calloc(1, sizeof *s);
        if (s)
        {
            snprintf(s->dir, sizeof s->dir, "%s", dir);
            s->ok = rb_table_load(&s->tab, dir) == 0 && rb_atlas_load(&s->atlas, dir) == 0;
            s->next = shares;
            __atomic_store_n(&shares, s, __ATOMIC_RELEASE);
        }
        if (nw_env) image_heap_set(nw_env, heap);
    }
    pthread_mutex_unlock(&shares_lock);
    return s;
}

const struct rb_table *rb_table_shared(const char *dir)
{
    const struct rbshare *s = get(dir);
    return s && s->ok ? &s->tab : NULL;
}

const struct rb_atlas *rb_atlas_shared(const char *dir)
{
    const struct rbshare *s = get(dir);
    return s && s->ok ? &s->atlas : NULL;
}
