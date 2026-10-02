/* The Long-keyed HashMap order emulation, jhashmap.h: it owns its heap
 * nodes. */
#include "jhashmap.h"

#include <stdlib.h>

/* Treeification threshold: JDK 8 turns a bucket into a red-black tree at 8
 * entries; an abort beats a silently wrong order. */
#define JHM_TREEIFY 8

/* ------------------------------- the Long-keyed map, jhashmap.h -------------- */

/* The key's Long.hashCode: key ^ key >>> 32, then HashMap's spread. */
static uint32_t jhm64_hash(int64_t key)
{
    uint32_t h = (uint32_t)((uint64_t)key ^ ((uint64_t)key >> 32));
    return h ^ (h >> 16);
}

void jhm64_init(struct jhm64 *m)
{
    m->table = calloc(16, sizeof *m->table);
    m->cap = 16;
    m->n = 0;
}

void jhm64_free(struct jhm64 *m)
{
    for (int i = 0; i < m->cap; ++i)
    {
        for (struct jhm64_node *p = m->table[i]; p; )
        {
            struct jhm64_node *next = p->next;
            free(p);
            p = next;
        }
    }

    free(m->table);
    m->table = NULL;
    m->cap = m->n = 0;
}

/* The JDK 8 resize: every old chain splits into a low and a high half, in
 * chain order. */
static void jhm64_resize(struct jhm64 *m)
{
    int newcap = m->cap * 2;
    struct jhm64_node **lo = calloc((size_t)newcap, sizeof *lo);
    struct jhm64_node **hi = calloc((size_t)newcap, sizeof *hi);
    struct jhm64_node **table = calloc((size_t)newcap, sizeof *table);

    for (int b = 0; b < m->cap; ++b)
    {
        struct jhm64_node *lo_head = NULL, *lo_tail = NULL;
        struct jhm64_node *hi_head = NULL, *hi_tail = NULL;

        for (struct jhm64_node *p = m->table[b]; p; p = p->next)
        {
            int nb = (int)(jhm64_hash(p->key) & (uint32_t)(newcap - 1));

            if (nb == b)
            {
                if (lo_tail) lo_tail->next = p; else lo_head = p;
                lo_tail = p;
            }
            else
            {
                if (hi_tail) hi_tail->next = p; else hi_head = p;
                hi_tail = p;
            }
        }

        if (lo_tail) lo_tail->next = NULL;
        if (hi_tail) hi_tail->next = NULL;
        lo[b] = lo_head;
        hi[newcap / 2 + b] = hi_head;
    }

    free(m->table);
    m->table = table;
    m->cap = newcap;

    for (int b = 0; b < newcap; ++b) m->table[b] = b < newcap / 2 ? lo[b] : hi[b];

    free(lo);
    free(hi);
}

void jhm64_put(struct jhm64 *m, int64_t key, int64_t value)
{
    int b = (int)(jhm64_hash(key) & (uint32_t)(m->cap - 1));
    int depth = 0;

    for (struct jhm64_node *p = m->table[b]; p; p = p->next, ++depth)
    {
        if (p->key == key)
        {
            p->value = value;
            return;
        }
    }

    struct jhm64_node *node = malloc(sizeof *node);
    node->next = NULL;
    node->key = key;
    node->value = value;

    struct jhm64_node **tail = &m->table[b];

    while (*tail) tail = &(*tail)->next;
    *tail = node;
    ++m->n;

    /* A bucket that just took its ninth node: treeifyBin, which for a table
     * below the 64 minimum treeify capacity resizes instead. Above it the
     * bucket would be a red-black tree, whose order this does not emulate. */
    if (depth >= JHM_TREEIFY)
    {
        if (m->cap < 64) jhm64_resize(m);
        else abort();
    }

    if (m->n > (m->cap * 3) / 4) jhm64_resize(m);
}

int jhm64_size(const struct jhm64 *m)
{
    return m->n;
}

int jhm64_contains(const struct jhm64 *m, int64_t key)
{
    int b = (int)(jhm64_hash(key) & (uint32_t)(m->cap - 1));

    for (struct jhm64_node *p = m->table[b]; p; p = p->next)
        if (p->key == key) return 1;

    return 0;
}

int jhm64_order(const struct jhm64 *m, int64_t *out, int64_t *keys)
{
    int k = 0;

    for (int b = 0; b < m->cap; ++b)
    {
        for (struct jhm64_node *p = m->table[b]; p; p = p->next)
        {
            out[k] = p->value;
            if (keys) keys[k] = p->key;
            ++k;
        }
    }

    return k;
}
