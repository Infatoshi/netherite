/* The HUD's item records, one converted table a process and scene (itemtab.h). */
#include "itemtab.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "envheap.h"
#include "image.h"
#include "tape.h"

struct itemtab {
    char scene[1024];
    int nids;
    int64_t *first;             /* id i's records are first[i] .. first[i + 1] - 1 */
    size_t stride, size;
    unsigned char *rec;         /* each: int dmg, int ok, then the converted bytes */
    struct itemtab *next;
};

static struct itemtab *tabs;    /* the scenes read so far; a table is complete before it is linked */
static pthread_mutex_t tabs_lock = PTHREAD_MUTEX_INITIALIZER;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    char *s = NULL;
    if (fseek(f, 0, SEEK_END) == 0)
    {
        long n = ftell(f);
        if (n >= 0 && fseek(f, 0, SEEK_SET) == 0 && (s = malloc((size_t)n + 1)) != NULL)
        {
            if (fread(s, 1, (size_t)n, f) == (size_t)n) s[n] = 0;
            else { free(s); s = NULL; }
        }
    }
    fclose(f);
    return s;
}

static struct itemtab *build(const char *scene, size_t size, itemtab_conv conv)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/state/item_table.json", scene);
    char *s = read_file(path);
    struct jval *root = s ? json_parse(s) : NULL;   /* json_parse owns s */
    if (root == NULL) return NULL;
    int nids = json_len(root);   /* -1 for a value not an array, as an id's null */
    int64_t total = 0;
    for (int i = 0; i < nids; ++i) total += json_len(json_at(root, i)) > 0 ? json_len(json_at(root, i)) : 0;
    struct itemtab *t = proc_calloc(1, sizeof *t);
    size_t stride = (2 * sizeof(int) + size + 7) & ~(size_t)7;
    if (t) t->first = proc_calloc((size_t)nids + 1, sizeof *t->first);
    if (t && t->first) t->rec = proc_calloc(total > 0 ? (size_t)total : 1, stride);
    if (t == NULL || t->first == NULL || t->rec == NULL)
    {
        if (t) { proc_free(t->first); proc_free(t); }
        json_free(root);
        return NULL;
    }
    snprintf(t->scene, sizeof t->scene, "%s", scene);
    t->nids = nids;
    t->stride = stride;
    t->size = size;
    int64_t k = 0;
    for (int i = 0; i < nids; ++i)
    {
        const struct jval *byid = json_at(root, i);
        int n = json_len(byid);
        t->first[i] = k;
        if (n < 0) n = 0;
        for (int j = 0; j < n; ++j, ++k)
        {
            unsigned char *r = t->rec + (size_t)k * stride;
            int dmg = -1;
            int ok = conv(json_at(byid, j), r + 2 * sizeof(int), &dmg);
            memcpy(r, &dmg, sizeof dmg);
            memcpy(r + sizeof(int), &ok, sizeof ok);
        }
    }
    t->first[nids] = k;
    json_free(root);
    return t;
}

const struct itemtab *itemtab_get(const char *scene, size_t size, itemtab_conv conv)
{
    if (scene == NULL) return NULL;
    for (struct itemtab *t = __atomic_load_n(&tabs, __ATOMIC_ACQUIRE); t; t = t->next)
        if (t->size == size && !strcmp(t->scene, scene)) return t;
    pthread_mutex_lock(&tabs_lock);
    struct itemtab *t;
    for (t = tabs; t; t = t->next)
        if (t->size == size && !strcmp(t->scene, scene)) break;
    if (t == NULL)
    {
        /* the process's memory, not the current env's heap */
        int heap = nw_env ? image_heap_set(nw_env, 0) : 0;
        t = build(scene, size, conv);
        if (nw_env) image_heap_set(nw_env, heap);
        if (t)
        {
            t->next = tabs;
            __atomic_store_n(&tabs, t, __ATOMIC_RELEASE);
        }
    }
    pthread_mutex_unlock(&tabs_lock);
    return t;
}

int itemtab_lookup(const struct itemtab *t, int id, int meta, void *out)
{
    if (t == NULL || id < 0 || id >= t->nids) return 0;
    int64_t a = t->first[id], b = t->first[id + 1];
    if (a == b) return 0;
    int64_t pick = a;
    for (int64_t k = a; k < b; ++k)
    {
        int dmg;
        memcpy(&dmg, t->rec + (size_t)k * t->stride, sizeof dmg);
        if (dmg == meta) { pick = k; break; }
    }
    const unsigned char *r = t->rec + (size_t)pick * t->stride;
    int ok;
    memcpy(&ok, r + sizeof(int), sizeof ok);
    if (ok) memcpy(out, r + 2 * sizeof(int), t->size);
    return ok;
}
