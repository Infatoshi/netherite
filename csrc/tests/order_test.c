/* The containers whose iteration order is game state (GPU audit L4), each
 * against the emulation it replaced, over randomised key sets: hash
 * collisions, resizes and, for chunksToUnload, treeified bins. The old
 * emulations live on here only, as the reference: the JDK 8 bucket chains
 * as they were ported (spawning.c's spmap, serverreplay.c's active-set
 * sort, jhashmap.c's jhm, potion.c's node map, chunkload.c's
 * ConcurrentHashMap). make test runs it (make -C csrc order). */
#include "../engine/jorder.h"
#include "../engine/potion.h"
#include "../engine/chunkset.h"
#include "../engine/explosion.h"
#include "../engine/arena.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint64_t rnd(void)
{
    uint64_t z = (rng_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static int rnd_in(int lo, int hi)
{
    return lo + (int)(rnd() % (uint64_t)(hi - lo + 1));
}

static int failures;
static long cs_tree_bins;   /* tree bins seen, summed over the samples */

static void fail(const char *what, long trial)
{
    if (failures++ < 10) fprintf(stderr, "order: %s differs from the old emulation (trial %ld)\n", what, trial);
}

/* the most keys one bin of a (2r+1)^2 window holds at a table of cap: the
 * old emulations kept insertion order, which JDK 8 leaves at a bin of 9
 * (the tree windows are checked against Java's own orders instead) */
static long tree_windows;

static int window_most(int pcx, int pcz, int r, int cap)
{
    static int count[4096];
    int most = 0;

    memset(count, 0, sizeof count[0] * (size_t)cap);
    for (int dx = -r; dx <= r; ++dx)
        for (int dz = -r; dz <= r; ++dz)
        {
            int c = ++count[jord_spread(jord_ccp_hash(dx + pcx, dz + pcz)) & (uint32_t)(cap - 1)];
            if (c > most) most = c;
        }
    return most;
}

/* ================================================= the spawner's eligible map
 * (spawning.c before L4: JDK 8 buckets with node-index chains) */

#define SPMAP_CAP 512
#define SPMAP_NODES 289
struct spmap {
    int head[SPMAP_CAP];
    struct { int next, cx, cz, value; } node[SPMAP_NODES];
    int cap, n;
};

static void spmap_init(struct spmap *m)
{
    for (int i = 0; i < 16; ++i) m->head[i] = -1;
    m->cap = 16;
    m->n = 0;
}

static int spmap_bucket(int32_t h, int cap)
{
    uint32_t u = (uint32_t)h;
    return (int)((u ^ (u >> 16)) & (uint32_t)(cap - 1));
}

static void spmap_resize(struct spmap *m)
{
    int newcap = m->cap * 2;

    if (newcap > SPMAP_CAP) abort();

    for (int b = 0; b < m->cap; ++b)
    {
        int lo_head = -1, lo_tail = -1, hi_head = -1, hi_tail = -1;

        for (int p = m->head[b]; p >= 0; p = m->node[p].next)
        {
            int nb = spmap_bucket(jord_ccp_hash(m->node[p].cx, m->node[p].cz), newcap);

            if (nb == b)
            {
                if (lo_tail >= 0) m->node[lo_tail].next = p; else lo_head = p;
                lo_tail = p;
            }
            else
            {
                if (hi_tail >= 0) m->node[hi_tail].next = p; else hi_head = p;
                hi_tail = p;
            }
        }

        if (lo_tail >= 0) m->node[lo_tail].next = -1;
        if (hi_tail >= 0) m->node[hi_tail].next = -1;
        m->head[b] = lo_head;
        m->head[m->cap + b] = hi_head;
    }

    m->cap = newcap;
}

static void spmap_put(struct spmap *m, int cx, int cz, int value)
{
    int b = spmap_bucket(jord_ccp_hash(cx, cz), m->cap);

    for (int p = m->head[b]; p >= 0; p = m->node[p].next)
    {
        if (m->node[p].cx == cx && m->node[p].cz == cz)
        {
            m->node[p].value = value;
            return;
        }
    }

    if (m->n == SPMAP_NODES) abort();

    int node = m->n++;

    m->node[node].next = -1;
    m->node[node].cx = cx;
    m->node[node].cz = cz;
    m->node[node].value = value;

    int *tail = &m->head[b];

    while (*tail >= 0) tail = &m->node[*tail].next;
    *tail = node;

    if (m->n > (m->cap * 3) / 4) spmap_resize(m);
}

static int spmap_order(const struct spmap *m, int *out)
{
    int k = 0;

    for (int b = 0; b < m->cap; ++b)
    {
        for (int p = m->head[b]; p >= 0; p = m->node[p].next)
        {
            out[k * 3] = m->node[p].cx;
            out[k * 3 + 1] = m->node[p].cz;
            out[k * 3 + 2] = m->node[p].value;
            ++k;
        }
    }

    return k;
}

/* The player's chunk anywhere in the int range a position can floor to,
 * and near the origin, where recordings play. */
static void random_chunk(int *pcx, int *pcz)
{
    int far = rnd() & 1;

    *pcx = far ? (int32_t)(uint32_t)rnd() / 16 : rnd_in(-3000, 3000);
    *pcz = far ? (int32_t)(uint32_t)rnd() / 16 : rnd_in(-3000, 3000);
}

static long test_spawner(long trials)
{
    static struct spmap m;
    int old[289 * 3], got[289 * 2];

    for (long t = 0; t < trials; ++t)
    {
        int pcx, pcz;

        random_chunk(&pcx, &pcz);
        spmap_init(&m);

        for (int dx = -8; dx <= 8; ++dx)
            for (int dz = -8; dz <= 8; ++dz)
                spmap_put(&m, dx + pcx, dz + pcz, (dx == -8 || dx == 8 || dz == -8 || dz == 8));

        int n = spmap_order(&m, old);
        int bins = jord_cap(289);
        int k = jord_ccp_window(pcx, pcz, 8, &bins, got);

        if (window_most(pcx, pcz, 8, jord_cap(289)) >= 9) { ++tree_windows; continue; }

        if (n != k || m.cap != jord_cap(289)) { fail("spawner eligible set", t); continue; }

        for (int i = 0; i < n; ++i)
        {
            int cx = got[i * 2], cz = got[i * 2 + 1];
            int edge = cx == pcx - 8 || cx == pcx + 8 || cz == pcz - 8 || cz == pcz + 8;

            if (old[i * 3] != cx || old[i * 3 + 1] != cz || old[i * 3 + 2] != edge)
            {
                fail("spawner eligible set", t);
                break;
            }
        }
    }

    return trials;
}

/* ===================================================== the active chunk set
 * (serverreplay.c before L4: a counting sort by bucket per tick) */

static int active_bucket(int x, int z, int cap)
{
    uint32_t h = (uint32_t)jord_ccp_hash(x, z);
    return (int)((h ^ (h >> 16)) & (uint32_t)(cap - 1));
}

static int active_capacity(int n)
{
    int cap = 16;

    while (n > cap * 3 / 4) cap *= 2;

    return cap;
}

#define SR_MAX_ACTIVE 1089

static int active_old(int view, int pcx, int pcz, int *out)
{
    int n = 0;
    int cx[SR_MAX_ACTIVE], cz[SR_MAX_ACTIVE];

    for (int dx = -view; dx <= view; ++dx)
        for (int dz = -view; dz <= view; ++dz)
        {
            if (n >= SR_MAX_ACTIVE) return 0;
            cx[n] = dx + pcx;
            cz[n] = dz + pcz;
            ++n;
        }

    int cap = active_capacity(n);
    int count[2048];
    if (cap > 2048) return 0;
    memset(count, 0, sizeof count);

    for (int i = 0; i < n; ++i) ++count[active_bucket(cx[i], cz[i], cap)];

    int start[2048], sum = 0;

    for (int b = 0; b < cap; ++b) { start[b] = sum; sum += count[b]; }

    int pos[2048];
    memcpy(pos, start, sizeof(int) * (size_t)cap);

    for (int i = 0; i < n; ++i)
    {
        int b = active_bucket(cx[i], cz[i], cap);
        int k = pos[b]++;
        out[k * 2] = cx[i];
        out[k * 2 + 1] = cz[i];
    }

    return n;
}

/* serverreplay.c's sr_active_set now */
static int active_new(int view, int pcx, int pcz, int *out)
{
    int side = 2 * view + 1;

    if (view >= 0 && side * side > SR_MAX_ACTIVE) return 0;

    int bins = jord_cap(view >= 0 ? side * side : 0);

    return jord_ccp_window(pcx, pcz, view, &bins, out);
}

static long test_active(long trials)
{
    int old[SR_MAX_ACTIVE * 2], got[SR_MAX_ACTIVE * 2];

    for (long t = 0; t < trials; ++t)
    {
        int pcx, pcz, view = rnd_in(-1, 17);

        random_chunk(&pcx, &pcz);

        int n = active_old(view, pcx, pcz, old);
        int k = active_new(view, pcx, pcz, got);

        if (view >= 0 && view <= 16 && window_most(pcx, pcz, view, jord_cap((2 * view + 1) * (2 * view + 1))) >= 9)
        {
            ++tree_windows;
            continue;
        }

        if (n != k || memcmp(old, got, sizeof(int) * 2 * (size_t)n) != 0) fail("active chunk set", t);
    }

    return trials;
}

/* ============================================ the explosion's affected set
 * (jhashmap.c before L4: JDK 8 buckets with node-index chains) */

#define JHM_TREEIFY 8

struct jhm_node {
    int32_t next;
    int32_t x, y, z;
    int64_t value;
};

struct jhm {
    int32_t *table, *table_alt;
    struct jhm_node *nodes;
    int cap;
    int n;
    int node_cap, table_cap;
};

static int jhm_bucket(uint32_t hash, int cap)
{
    return (int)(hash & (uint32_t)(cap - 1));
}

static void jhm_init(struct jhm *m, struct jhm_node *nodes, int node_cap, int32_t *table, int32_t *table_alt,
              int table_cap)
{
    m->nodes = nodes;
    m->node_cap = node_cap;
    m->table = table;
    m->table_alt = table_alt;
    m->table_cap = table_cap;
    m->cap = 16;
    m->n = 0;
    for (int i = 0; i < m->cap; ++i) m->table[i] = -1;
}

/* JDK 8 resize: the table doubles, then every old chain is walked in order
 * and re-linked into a low half and a high half, each keeping the chain
 * order. */
static void jhm_resize(struct jhm *m)
{
    int newcap = m->cap * 2;

    if (newcap > m->table_cap) abort();

    int32_t *old = m->table, *table = m->table_alt;

    for (int b = 0; b < m->cap; ++b)
    {
        int lo_head = -1, lo_tail = -1, hi_head = -1, hi_tail = -1;

        for (int p = old[b]; p >= 0; p = m->nodes[p].next)
        {
            const struct jhm_node *q = &m->nodes[p];
            int nb = jhm_bucket(jord_spread(jord_cpos_hash(q->x, q->y, q->z)), newcap);

            if (nb == b)
            {
                if (lo_tail >= 0) m->nodes[lo_tail].next = p; else lo_head = p;
                lo_tail = p;
            }
            else
            {
                if (hi_tail >= 0) m->nodes[hi_tail].next = p; else hi_head = p;
                hi_tail = p;
            }
        }

        if (lo_tail >= 0) m->nodes[lo_tail].next = -1;
        if (hi_tail >= 0) m->nodes[hi_tail].next = -1;
        table[b] = lo_head;
        table[m->cap + b] = hi_head;
    }

    m->table_alt = old;
    m->table = table;
    m->cap = newcap;
}

static void jhm_put(struct jhm *m, int32_t x, int32_t y, int32_t z, int64_t value)
{
    int b = jhm_bucket(jord_spread(jord_cpos_hash(x, y, z)), m->cap);
    int depth = 0;

    for (int p = m->table[b]; p >= 0; p = m->nodes[p].next, ++depth)
    {
        if (m->nodes[p].x == x && m->nodes[p].y == y && m->nodes[p].z == z)
        {
            m->nodes[p].value = value;
            return;
        }
    }

    if (depth >= JHM_TREEIFY)
    {
        /* a bucket this deep would be a red-black tree in JDK 8; not emulated */
        abort();
    }
    if (m->n == m->node_cap) abort();

    int node = m->n;
    m->nodes[node].next = -1;
    m->nodes[node].x = x;
    m->nodes[node].y = y;
    m->nodes[node].z = z;
    m->nodes[node].value = value;

    int32_t *tail = &m->table[b];

    while (*tail >= 0) tail = &m->nodes[*tail].next;
    *tail = node;
    ++m->n;

    /* HashMap.put knows the new size against the threshold before inserting;
     * the JDK 8 order is the same either way, only the resize timing differs
     * by when the table doubles. */
    if (m->n > (m->cap * 3) / 4) jhm_resize(m);
}

static int jhm_contains(const struct jhm *m, int32_t x, int32_t y, int32_t z)
{
    int b = jhm_bucket(jord_spread(jord_cpos_hash(x, y, z)), m->cap);

    for (int p = m->table[b]; p >= 0; p = m->nodes[p].next)
    {
        if (m->nodes[p].x == x && m->nodes[p].y == y && m->nodes[p].z == z) return 1;
    }

    return 0;
}

static int jhm_order(const struct jhm *m, int64_t *out, int32_t *out_xy)
{
    int k = 0;

    for (int b = 0; b < m->cap; ++b)
    {
        for (int i = m->table[b]; i >= 0; i = m->nodes[i].next)
        {
            const struct jhm_node *p = &m->nodes[i];
            if (out) out[k] = p->value;
            if (out_xy)
            {
                out_xy[k * 3] = p->x;
                out_xy[k * 3 + 1] = p->y;
                out_xy[k * 3 + 2] = p->z;
            }
            ++k;
        }
    }

    return k;
}

#define X_KEYS 16384
#define X_TABLE (2 * X_KEYS)

/* A ray-march-like key stream: positions in a box around a centre anywhere
 * in the world, many repeated; some trials add runs of keys whose hashes are
 * equal (z solved for the hash of another key) or share a bin. */
static long test_explosion(long trials)
{
    static struct jhm_node nodes[X_KEYS];
    static int32_t table[2][X_TABLE], index[2 * X_TABLE], count[X_TABLE];
    static int32_t keys[3 * X_KEYS], old[3 * X_KEYS], got[3 * X_KEYS], first[3 * X_KEYS];
    long total = 0;

    for (long t = 0; t < trials; ++t)
    {
        struct jhm m;
        struct jord_set3 s;
        int n_first = 0;

        jhm_init(&m, nodes, X_KEYS, table[0], table[1], X_TABLE);
        jord_set3_init(&s, keys, X_KEYS, index, 2 * X_TABLE, count, X_TABLE);

        int cx = rnd_in(-30000000, 30000000), cy = rnd_in(0, 255), cz = rnd_in(-30000000, 30000000);
        int radius = rnd_in(0, 12);
        int adds = rnd_in(0, (t % 16 == 0) ? 40000 : 3000);
        int collide = rnd() % 3 == 0;

        for (int a = 0; a < adds && m.n < X_KEYS - 1; ++a)
        {
            int32_t x = cx + rnd_in(-radius, radius), y = cy + rnd_in(-radius, radius), z = cz + rnd_in(-radius, radius);

            if (collide && n_first > 0 && rnd() % 4 == 0)
            {
                /* the same hash as an earlier key, from another x and y */
                int j = rnd_in(0, n_first - 1);
                int32_t h = jord_cpos_hash(first[j * 3], first[j * 3 + 1], first[j * 3 + 2]);

                x = first[j * 3] + rnd_in(-3, 3);
                y = first[j * 3 + 1] + rnd_in(-3, 3);
                z = (int32_t)((uint32_t)h - (uint32_t)x * 8976890u - (uint32_t)y * 981131u);
            }

            /* a bin already 8 deep stops both: leave such a key out */
            if (!jord_set3_contains(&s, x, y, z) && s.count[jord_spread(jord_cpos_hash(x, y, z)) & (uint32_t)(s.cap - 1)] >= 8)
                continue;

            if (!jhm_contains(&m, x, y, z))
            {
                first[n_first * 3] = x;
                first[n_first * 3 + 1] = y;
                first[n_first * 3 + 2] = z;
                ++n_first;
            }

            jhm_put(&m, x, y, z, 0);
            jord_set3_add(&s, x, y, z);
        }

        int n = jhm_order(&m, NULL, old);
        int k = jord_set3_order(&s, got);

        if (n != k || s.n != n_first || s.cap != m.cap || memcmp(old, got, sizeof(int32_t) * 3 * (size_t)n) != 0
            || memcmp(first, keys, sizeof(int32_t) * 3 * (size_t)n) != 0)
            fail("explosion affected set", t);

        total += n;
    }

    return total;
}

/* ========================================================== the potion map
 * (potion.c before L4: JDK 8 buckets of pooled nodes with pointer chains) */

struct old_potion_node {
    struct potion_effect eff;
    struct old_potion_node *next;
};

struct old_potion_map {
    int size;
    int capacity;
    struct old_potion_node *buckets[32];
    struct old_potion_node pool[32];
    int pool_used[32];
};


static void old_potion_map_init(struct old_potion_map *m)
{
    memset(m, 0, sizeof *m);
    m->capacity = 16;
}


static struct old_potion_node *old_alloc_node(struct old_potion_map *m)
{
    for (int i = 0; i < 32; i++)
    {
        if (!m->pool_used[i])
        {
            m->pool_used[i] = 1;
            m->pool[i].next = NULL;
            return &m->pool[i];
        }
    }
    return NULL;
}

static void old_free_node(struct old_potion_map *m, struct old_potion_node *n)
{
    int idx = (int)(n - m->pool);
    if (idx >= 0 && idx < 32) m->pool_used[idx] = 0;
}

static void old_map_resize_to_32(struct old_potion_map *m)
{
    struct old_potion_node *old_buckets[16];
    memcpy(old_buckets, m->buckets, sizeof old_buckets);
    memset(m->buckets, 0, sizeof m->buckets);
    m->capacity = 32;

    for (int j = 0; j < 16; j++)
    {
        struct old_potion_node *e = old_buckets[j];
        struct old_potion_node *loHead = NULL, *loTail = NULL;
        struct old_potion_node *hiHead = NULL, *hiTail = NULL;
        while (e != NULL)
        {
            struct old_potion_node *next = e->next;
            if ((e->eff.id & 16) == 0)
            {
                if (loTail == NULL) loHead = e;
                else loTail->next = e;
                loTail = e;
            }
            else
            {
                if (hiTail == NULL) hiHead = e;
                else hiTail->next = e;
                hiTail = e;
            }
            e = next;
        }
        if (loTail != NULL)
        {
            loTail->next = NULL;
            m->buckets[j] = loHead;
        }
        if (hiTail != NULL)
        {
            hiTail->next = NULL;
            m->buckets[j + 16] = hiHead;
        }
    }
}

static struct potion_effect *old_potion_map_get(struct old_potion_map *m, int id)
{
    int idx = id & (m->capacity - 1);
    for (struct old_potion_node *n = m->buckets[idx]; n; n = n->next)
    {
        if (n->eff.id == id) return &n->eff;
    }
    return NULL;
}


static int old_potion_map_put(struct old_potion_map *m, const struct potion_effect *eff)
{
    int idx = eff->id & (m->capacity - 1);
    for (struct old_potion_node *n = m->buckets[idx]; n; n = n->next)
    {
        if (n->eff.id == eff->id)
        {
            if (eff->amplifier > n->eff.amplifier)
            {
                n->eff.amplifier = eff->amplifier;
                n->eff.duration = eff->duration;
            }
            else if (eff->amplifier == n->eff.amplifier && n->eff.duration < eff->duration)
            {
                n->eff.duration = eff->duration;
            }
            else if (!eff->is_ambient && n->eff.is_ambient)
            {
                n->eff.is_ambient = eff->is_ambient;
            }
            return 0;
        }
    }
    struct old_potion_node *node = old_alloc_node(m);
    if (!node) return 0;
    node->eff = *eff;
    node->next = NULL;
    if (m->buckets[idx] == NULL)
    {
        m->buckets[idx] = node;
    }
    else
    {
        struct old_potion_node *cur = m->buckets[idx];
        while (cur->next) cur = cur->next;
        cur->next = node;
    }
    m->size++;
    if (m->size > 12 && m->capacity == 16)
    {
        old_map_resize_to_32(m);
    }
    return 1;
}

static int old_potion_map_remove(struct old_potion_map *m, int id, struct potion_effect *removed)
{
    int idx = id & (m->capacity - 1);
    struct old_potion_node **prev = &m->buckets[idx];
    for (struct old_potion_node *n = *prev; n; prev = &n->next, n = n->next)
    {
        if (n->eff.id == id)
        {
            if (removed) *removed = n->eff;
            *prev = n->next;
            old_free_node(m, n);
            m->size--;
            return 1;
        }
    }
    return 0;
}

static int old_potion_order(const struct old_potion_map *m, uint8_t *ids)
{
    int k = 0;

    for (int b = 0; b < m->capacity; ++b)
        for (const struct old_potion_node *n = m->buckets[b]; n; n = n->next) ids[k++] = n->eff.id;

    return k;
}

/* Random puts (new keys and merges), removes and clears over the 32 ids,
 * the order compared after every operation. */
static long test_potions(long trials)
{
    static struct old_potion_map om;
    struct potion_map m;
    long ops = 0;

    for (long t = 0; t < trials; ++t)
    {
        old_potion_map_init(&om);
        potion_map_init(&m);

        int nops = rnd_in(1, 200);
        int span = rnd() % 2 ? 32 : 24;

        for (int i = 0; i < nops; ++i, ++ops)
        {
            int id = rnd_in(0, span - 1), op = rnd_in(0, 99);

            if (op < 60)
            {
                struct potion_effect e = {0};
                e.id = (uint8_t)id;
                e.amplifier = (int8_t)rnd_in(0, 3);
                e.duration = rnd_in(0, 2000);
                e.is_ambient = (uint8_t)(rnd() & 1);
                e.is_splash = (uint8_t)(rnd() & 1);
                if (old_potion_map_put(&om, &e) != potion_map_put(&m, &e)) fail("potion map put", t);
            }
            else if (op < 97)
            {
                struct potion_effect a = {0}, b = {0};
                int ra = old_potion_map_remove(&om, id, &a), rb = potion_map_remove(&m, id, &b);
                if (ra != rb || memcmp(&a, &b, sizeof a) != 0) fail("potion map remove", t);
            }
            else
            {
                old_potion_map_init(&om);
                potion_map_init(&m);
            }

            uint8_t o[32], g[32];
            int no = old_potion_order(&om, o), ng = potion_map_order(&m, g);

            if (no != ng || om.size != m.size || om.capacity != m.capacity || memcmp(o, g, (size_t)no) != 0)
            {
                fail("potion map order", t);
                break;
            }

            for (int k = 0; k < no; ++k)
            {
                const struct potion_effect *a = old_potion_map_get(&om, o[k]), *b = potion_map_get(&m, g[k]);

                if (!a || !b || memcmp(a, b, sizeof *a) != 0) fail("potion map values", t);
            }
        }
    }

    return ops;
}

/* ========================================================= chunksToUnload
 * (chunkload.c before L4: the ConcurrentHashMap's bins as linked lists and
 * red-black trees over node indices) */

/* ---------------------------------------------------------------- the set
 *
 * The provider's chunksToUnload: Collections.newSetFromMap(new
 * ConcurrentHashMap()) of Long. A faithful port of the JDK 8 ConcurrentHashMap
 * bin structure, because the order the unload loop sees is the map's own:
 *
 *   the table is a power of two, put appends at the tail of a plain bin;
 *   a bin of 8 nodes treeifies into a TreeBin, whose list keeps the bin's
 *   order (first = the old head) and whose red-black tree orders by (spread
 *   hash, Long value); a put into a tree bin prepends the new node to the
 *   list; a remove unlinks it and may untreeify the bin back to a plain list
 *   in the list's order; a resize splits every bin's list into its lo and hi
 *   runs preserving order (a tree bin whose both runs stay over 6 keeps its
 *   old tree); addCount doubles the table once the size reaches sizeCtl (3/4
 *   of the table after the first transfer, where it is table + table/2);
 *   treeifyBin on a bin of 8 with a table under 64 presizes to
 *   tableSizeFor(3 * table) instead.
 *
 * One slot array serves the nodes; bins are plain lists (root < 0) or TreeBins
 * (root >= 0, with the list under head and prev/next both maintained, as the
 * tree code unlinks through both). */
#define OLD_CU_MIN 16

struct old_cu_slot
{
    int64_t key;
    int hash;       /* spread(Long.hashCode), what CHM stores per node */
    int next;       /* list next, -1 at the end */
    int prev;       /* list prev, -1 at the head (tree bins keep this live) */
    int parent, left, right;
    int red;
};

/* A set's fixed storage (made with its cl): the most bins and nodes. */
#define OLD_CU_MAX_BINS (1 << 16)
#define OLD_CU_MAX_NODES (1 << 16)

struct old_chunkset
{
    int *head;      /* per bin: the list head (a bin's first node) */
    int *root;      /* per bin: the TreeBin's root, -1 for a plain bin */
    int *head_alt, *root_alt;   /* the transfer's other table */
    int n;          /* bin count, a power of two */
    int size, sizeCtl;
    struct old_cu_slot *e;
    int ne, cape;
};

static void old_cs_resize_once(struct old_chunkset *s);   /* transfer to the double */

static int old_cs_rotate_left(struct old_chunkset *s, int root, int p);
static int old_cs_rotate_right(struct old_chunkset *s, int root, int p);
static int old_cs_balance_insertion(struct old_chunkset *s, int root, int x);
static int old_cs_balance_deletion(struct old_chunkset *s, int root, int x);
static int old_cs_tree_rebuild(struct old_chunkset *s, int b);

static int old_cs_spread(int64_t key)
{
    uint32_t h = (uint32_t)((uint64_t)key ^ (uint64_t)key >> 32);   /* Long.hashCode */
    h ^= h >> 16;
    return (int)(h & 0x7fffffffu);
}

static int old_cs_bin(const struct old_chunkset *s, int h)
{
    return h & (s->n - 1);
}

/* An empty set (a new table); the storage is made at the first init and
 * kept. */
static void old_cs_init(struct old_chunkset *s)
{
    if (s->e == NULL)
    {
        s->head = fixed_array(OLD_CU_MAX_BINS, sizeof(int));
        s->root = fixed_array(OLD_CU_MAX_BINS, sizeof(int));
        s->head_alt = fixed_array(OLD_CU_MAX_BINS, sizeof(int));
        s->root_alt = fixed_array(OLD_CU_MAX_BINS, sizeof(int));
        s->e = fixed_array(OLD_CU_MAX_NODES, sizeof(struct old_cu_slot));
    }
    s->n = OLD_CU_MIN;

    for (int i = 0; i < s->n; ++i) { s->head[i] = -1; s->root[i] = -1; }

    s->size = 0;
    s->sizeCtl = OLD_CU_MIN - OLD_CU_MIN / 4;
    s->ne = 0;
    s->cape = OLD_CU_MAX_NODES;
}


static int old_cs_slot(struct old_chunkset *s)
{
    if (s->ne == OLD_CU_MAX_NODES) abort();

    return s->ne++;
}

/* Long.compareTo: signed. */
static int old_cs_cmp(int64_t a, int64_t b)
{
    return a < b ? -1 : a > b ? 1 : 0;
}

/* TreeBin's tree over the bin's list, in list order: the TreeBin constructor
 * and the tree half of putTreeVal. Returns the new root; head[b] and the list
 * are untouched. */
static int old_cs_tree_rebuild(struct old_chunkset *s, int b)
{
    int r = -1;

    for (int x = s->head[b]; x >= 0; )
    {
        int nxt = s->e[x].next;
        s->e[x].parent = s->e[x].left = s->e[x].right = -1;

        if (r < 0)
        {
            s->e[x].red = 0;
            r = x;
        }
        else
        {
            int p = r;

            for (;;)
            {
                int dir;

                if (s->e[p].hash > s->e[x].hash) dir = -1;
                else if (s->e[p].hash < s->e[x].hash) dir = 1;
                else dir = old_cs_cmp(s->e[x].key, s->e[p].key);

                int xp = p;

                if ((p = dir <= 0 ? s->e[xp].left : s->e[xp].right) < 0)
                {
                    s->e[x].parent = xp;

                    if (dir <= 0) s->e[xp].left = x;
                    else s->e[xp].right = x;

                    r = old_cs_balance_insertion(s, r, x);
                    break;
                }
            }
        }

        x = nxt;
    }

    return r;
}

static int old_cs_rotate_left(struct old_chunkset *s, int root, int p)
{
    int r = s->e[p].right;

    if (r < 0) return root;

    s->e[p].right = s->e[r].left;

    if (s->e[p].right >= 0) s->e[s->e[p].right].parent = p;

    int pp = s->e[p].parent;
    s->e[r].parent = pp;

    if (pp < 0) { root = r; s->e[r].red = 0; }
    else if (s->e[pp].left == p) s->e[pp].left = r;
    else s->e[pp].right = r;

    s->e[r].left = p;
    s->e[p].parent = r;
    return root;
}

static int old_cs_rotate_right(struct old_chunkset *s, int root, int p)
{
    int l = s->e[p].left;

    if (l < 0) return root;

    s->e[p].left = s->e[l].right;

    if (s->e[p].left >= 0) s->e[s->e[p].left].parent = p;

    int pp = s->e[l].parent = s->e[p].parent;

    if (pp < 0) { root = l; s->e[l].red = 0; }
    else if (s->e[pp].right == p) s->e[pp].right = l;
    else s->e[pp].left = l;

    s->e[l].right = p;
    s->e[p].parent = l;
    return root;
}

static int old_cs_balance_insertion(struct old_chunkset *s, int root, int x)
{
    s->e[x].red = 1;

    for (;;)
    {
        int xp, xpp;

        if ((xp = s->e[x].parent) < 0) { s->e[x].red = 0; return x; }

        if (!s->e[xp].red || (xpp = s->e[xp].parent) < 0) return root;

        if (s->e[xpp].left == xp)
        {
            int xppr = s->e[xpp].right;

            if (xppr >= 0 && s->e[xppr].red)
            {
                s->e[xppr].red = 0;
                s->e[xp].red = 0;
                s->e[xpp].red = 1;
                x = xpp;
            }
            else
            {
                if (s->e[xp].right == x)
                {
                    root = old_cs_rotate_left(s, root, xp);
                    x = xp;
                    xp = s->e[x].parent;
                }

                if (xp >= 0)
                {
                    s->e[xp].red = 0;

                    if ((xpp = s->e[xp].parent) >= 0)
                    {
                        s->e[xpp].red = 1;
                        root = old_cs_rotate_right(s, root, xpp);
                    }
                }
            }
        }
        else
        {
            int xppl = s->e[xpp].left;

            if (xppl >= 0 && s->e[xppl].red)
            {
                s->e[xppl].red = 0;
                s->e[xp].red = 0;
                s->e[xpp].red = 1;
                x = xpp;
            }
            else
            {
                if (s->e[xp].left == x)
                {
                    root = old_cs_rotate_right(s, root, xp);
                    x = xp;
                    xp = s->e[x].parent;
                }

                if (xp >= 0)
                {
                    s->e[xp].red = 0;

                    if ((xpp = s->e[xp].parent) >= 0)
                    {
                        s->e[xpp].red = 1;
                        root = old_cs_rotate_left(s, root, xpp);
                    }
                }
            }
        }
    }
}

/* balanceDeletion, from CLR by way of ConcurrentHashMap. */
static int old_cs_balance_deletion(struct old_chunkset *s, int root, int x)
{
    for (;;)
    {
        int xp;

        if (x < 0 || x == root) return root;

        if ((xp = s->e[x].parent) < 0) { s->e[x].red = 0; return x; }

        if (s->e[x].red) { s->e[x].red = 0; return root; }

        if (s->e[xp].left == x)
        {
            int xpr = s->e[xp].right;

            if (xpr >= 0 && s->e[xpr].red)
            {
                s->e[xpr].red = 0;
                s->e[xp].red = 1;
                root = old_cs_rotate_left(s, root, xp);
                xpr = (xp = s->e[x].parent) < 0 ? -1 : s->e[xp].right;
            }

            if (xpr < 0) x = xp;
            else
            {
                int sl = s->e[xpr].left, sr = s->e[xpr].right;

                if ((sr < 0 || !s->e[sr].red) && (sl < 0 || !s->e[sl].red))
                {
                    s->e[xpr].red = 1;
                    x = xp;
                }
                else
                {
                    if (sr < 0 || !s->e[sr].red)
                    {
                        if (sl >= 0) s->e[sl].red = 0;

                        s->e[xpr].red = 1;
                        root = old_cs_rotate_right(s, root, xpr);
                        xpr = (xp = s->e[x].parent) < 0 ? -1 : s->e[xp].right;
                    }

                    if (xpr >= 0)
                    {
                        s->e[xpr].red = xp < 0 ? 0 : s->e[xp].red;

                        if ((sr = s->e[xpr].right) >= 0) s->e[sr].red = 0;
                    }

                    if (xp >= 0)
                    {
                        s->e[xp].red = 0;
                        root = old_cs_rotate_left(s, root, xp);
                    }

                    x = root;
                }
            }
        }
        else
        {
            int xpl = s->e[xp].left;

            if (xpl >= 0 && s->e[xpl].red)
            {
                s->e[xpl].red = 0;
                s->e[xp].red = 1;
                root = old_cs_rotate_right(s, root, xp);
                xpl = (xp = s->e[x].parent) < 0 ? -1 : s->e[xp].left;
            }

            if (xpl < 0) x = xp;
            else
            {
                int sl = s->e[xpl].left, sr = s->e[xpl].right;

                if ((sl < 0 || !s->e[sl].red) && (sr < 0 || !s->e[sr].red))
                {
                    s->e[xpl].red = 1;
                    x = xp;
                }
                else
                {
                    if (sl < 0 || !s->e[sl].red)
                    {
                        if (sr >= 0) s->e[sr].red = 0;

                        s->e[xpl].red = 1;
                        root = old_cs_rotate_left(s, root, xpl);
                        xpl = (xp = s->e[x].parent) < 0 ? -1 : s->e[xp].left;
                    }

                    if (xpl >= 0)
                    {
                        s->e[xpl].red = xp < 0 ? 0 : s->e[xp].red;

                        if ((sl = s->e[xpl].left) >= 0) s->e[sl].red = 0;
                    }

                    if (xp >= 0)
                    {
                        s->e[xp].red = 0;
                        root = old_cs_rotate_right(s, root, xp);
                    }

                    x = root;
                }
            }
        }
    }
}


/* treeifyBin at a plain bin of 8: under 64 bins the table presizes instead of
 * treeifying. The tree is built over the bin's list, whose order does not
 * change. */
static void old_cs_try_presize(struct old_chunkset *s, int size);

static void old_cs_treeify(struct old_chunkset *s, int b)
{
    if (s->n < 64) { old_cs_try_presize(s, s->n << 1); return; }

    s->root[b] = old_cs_tree_rebuild(s, b);
}

/* ConcurrentHashMap.transfer: split every bin into its lo and hi runs over the
 * new table, each run keeping its order; a tree bin splits through its list. */
static void old_cs_resize_once(struct old_chunkset *s)
{
    int old_n = s->n;
    int *old_head = s->head, *old_root = s->root;
    s->n = old_n * 2;
    if (s->n > OLD_CU_MAX_BINS) abort();
    s->sizeCtl = (old_n << 1) - (old_n >> 1);
    s->head = s->head_alt;
    s->root = s->root_alt;
    s->head_alt = old_head;
    s->root_alt = old_root;

    for (int i = 0; i < s->n; ++i) { s->head[i] = -1; s->root[i] = -1; }

    for (int b = 0; b < old_n; ++b)
    {
        int lo = -1, lo_tail = -1, hi = -1, hi_tail = -1, lc = 0, hc = 0;

        for (int i = old_head[b]; i >= 0; )
        {
            int nxt = s->e[i].next;

            /* the node itself moves: the run it lands in keeps the chain's
             * order, and the list links (both directions) are relinked */
            s->e[i].next = s->e[i].prev = -1;

            int nb = old_cs_bin(s, s->e[i].hash);

            if (nb == b)
            {
                s->e[i].prev = lo_tail;

                if (lo < 0) lo = i;
                else s->e[lo_tail].next = i;

                lo_tail = i;
                ++lc;
            }
            else
            {
                s->e[i].prev = hi_tail;

                if (hi < 0) hi = i;
                else s->e[hi_tail].next = i;

                hi_tail = i;
                ++hc;
            }

            i = nxt;
        }

        /* plain bin */
        if (old_root[b] < 0)
        {
            s->head[b] = lo;
            s->head[b + old_n] = hi;
            s->root[b] = s->root[b + old_n] = -1;
        }
        else
        {
            /* tree bin: a small run untreeifies; a run with an empty sibling
             * keeps the old tree as it stands */
            if (lc <= 6) { s->head[b] = lo; s->root[b] = -1; }
            else if (hc != 0)
            {
                s->head[b] = lo;
                s->root[b] = old_cs_tree_rebuild(s, b);
            }
            else
            {
                s->head[b] = lo;   /* the whole list, order unchanged */
                s->root[b] = old_root[b];
            }

            if (hc <= 6)
            {
                s->head[b + old_n] = hi;
                s->root[b + old_n] = -1;
            }
            else if (lc != 0)
            {
                s->head[b + old_n] = hi;
                s->root[b + old_n] = old_cs_tree_rebuild(s, b + old_n);
            }
            else
            {
                s->head[b + old_n] = hi;
                s->root[b + old_n] = old_root[b];
            }
        }
    }
}

/* tryPresize: the table doubles (possibly more than once) until it holds
 * tableSizeFor(size + size/2 + 1). */
static void old_cs_try_presize(struct old_chunkset *s, int size)
{
    int c;

    if (size >= (1 << 30)) c = 1 << 30;
    else
    {
        c = size + (size >> 1) + 1;
        int cc = 16;

        while (cc < c) cc <<= 1;

        c = cc;
    }

    while (s->sizeCtl >= 0)
    {
        if (c <= s->sizeCtl) break;

        old_cs_resize_once(s);
    }
}

/* ConcurrentHashMap.putVal's addCount: the table doubles once the size
 * reaches sizeCtl. */
static void old_cs_add_count(struct old_chunkset *s)
{
    if (s->size >= s->sizeCtl) old_cs_resize_once(s);
}

/* ConcurrentHashMap.putVal: no-op when the key is held. */
static void old_cs_put(struct old_chunkset *s, int64_t key)
{
    int h = old_cs_spread(key);
    int b = old_cs_bin(s, h);

    if (s->root[b] >= 0)
    {
        /* the tree bin: find, or add */
        int p = s->root[b], xp = -1, dir = 0;

        while (p >= 0)
        {
            int ph = s->e[p].hash;

            if (ph > h) dir = -1;
            else if (ph < h) dir = 1;
            else if (s->e[p].key == key) return;   /* the key is held */
            else dir = old_cs_cmp(key, s->e[p].key);

            xp = p;
            p = dir <= 0 ? s->e[xp].left : s->e[xp].right;
        }

        int x = old_cs_slot(s);
        s->e[x].key = key;
        s->e[x].hash = h;
        s->e[x].next = s->head[b];      /* prepend to the list */
        s->e[x].prev = -1;
        s->e[x].parent = xp;
        s->e[x].left = s->e[x].right = -1;

        if (s->head[b] >= 0) s->e[s->head[b]].prev = x;

        s->head[b] = x;

        if (dir <= 0) s->e[xp].left = x;
        else s->e[xp].right = x;

        if (!s->e[xp].red) s->e[x].red = 1;
        else s->root[b] = old_cs_balance_insertion(s, s->root[b], x);

        ++s->size;
        old_cs_add_count(s);
        return;
    }

    /* the plain bin: putVal's binCount counts the nodes the walk touches,
     * not the appended one, so the bin treeifies when it reaches 9. A held
     * key ends the walk: no node, no addCount, but the treeify check still
     * runs on the count the walk reached. */
    int bin_count = 0;
    int t = s->head[b];

    if (t >= 0)
    {
        int bc = 1;

        for (;;)
        {
            if (s->e[t].key == key)
            {
                if (bc >= 8)
                {
                    if (s->n < 64) old_cs_try_presize(s, s->n << 1);
                    else old_cs_treeify(s, b);
                }
                return;
            }
            if (s->e[t].next < 0) break;
            t = s->e[t].next;
            ++bc;
        }
        bin_count = bc;
    }

    int idx = old_cs_slot(s);
    s->e[idx].key = key;
    s->e[idx].hash = h;
    s->e[idx].next = -1;
    s->e[idx].prev = -1;
    s->e[idx].parent = s->e[idx].left = s->e[idx].right = -1;
    s->e[idx].red = 0;

    if (t < 0) s->head[b] = idx;
    else
    {
        /* both list links: a later treeify keeps the list, and a tree bin's
         * removal unlinks through prev */
        s->e[t].next = idx;
        s->e[idx].prev = t;
    }

    ++s->size;

    if (bin_count >= 8)
    {
        if (s->n < 64) old_cs_try_presize(s, s->n << 1);
        else old_cs_treeify(s, b);
    }

    old_cs_add_count(s);
}

/* ConcurrentHashMap.remove on a bin: unlink the list node, rebalance the
 * tree, and untreeify a tree bin that grew too small. */
static int old_cs_take(struct old_chunkset *s, int64_t key)
{
    int h = old_cs_spread(key);
    int b = old_cs_bin(s, h);
    int p, pred = -1;

    if (s->root[b] >= 0)
    {
        p = s->root[b];

        while (p >= 0)
        {
            int ph = s->e[p].hash;

            if (ph > h) p = s->e[p].left;
            else if (ph < h) p = s->e[p].right;
            else if (s->e[p].key == key) break;
            else p = old_cs_cmp(key, s->e[p].key) <= 0 ? s->e[p].left : s->e[p].right;
        }

        if (p < 0) return 0;

        pred = s->e[p].prev;   /* the tree bin's list unlink goes through prev */
    }
    else
    {
        for (p = s->head[b], pred = -1; p >= 0; pred = p, p = s->e[p].next)
            if (s->e[p].key == key) break;

        if (p < 0) return 0;
    }

    /* the list unlink, from removeTreeNode */
    int next = s->e[p].next;

    if (pred < 0) s->head[b] = next;
    else s->e[pred].next = next;

    if (next >= 0) s->e[next].prev = pred;

    s->e[p].next = -1;
    s->e[p].prev = -1;
    --s->size;

    if (s->root[b] < 0) return 1;

    /* the tree bin: too small means back to a list */
    int r = s->root[b];

    if (s->head[b] < 0)
    {
        s->root[b] = -1;
        return 1;
    }

    if (r < 0 || s->e[r].right < 0 || s->e[r].left < 0
        || s->e[s->e[r].left].left < 0)
    {
        s->root[b] = -1;
        return 1;
    }

    /* the red-black delete, from removeTreeNode */
    int pl = s->e[p].left, pr = s->e[p].right, replacement;
    int rr = r;

    if (pl >= 0 && pr >= 0)
    {
        int sc = pr;

        while (s->e[sc].left >= 0) sc = s->e[sc].left;

        int c = s->e[sc].red;
        s->e[sc].red = s->e[p].red;
        s->e[p].red = c;

        int sr = s->e[sc].right;
        int pp = s->e[p].parent;

        if (sc == pr)
        {
            s->e[p].parent = sc;
            s->e[sc].right = p;
        }
        else
        {
            int sp = s->e[sc].parent;

            if ((s->e[p].parent = sp) >= 0)
            {
                if (s->e[sp].left == sc) s->e[sp].left = p;
                else s->e[sp].right = p;
            }

            if ((s->e[sc].right = pr) >= 0) s->e[pr].parent = sc;
        }

        s->e[p].left = -1;

        if ((s->e[p].right = sr) >= 0) s->e[sr].parent = p;

        if ((s->e[sc].left = pl) >= 0) s->e[pl].parent = sc;

        if ((s->e[sc].parent = pp) < 0) rr = sc;
        else if (s->e[pp].left == p) s->e[pp].left = sc;
        else s->e[pp].right = sc;

        replacement = sr >= 0 ? sr : p;
    }
    else if (pl >= 0) replacement = pl;
    else if (pr >= 0) replacement = pr;
    else replacement = p;

    if (replacement != p)
    {
        int pp = s->e[replacement].parent = s->e[p].parent;

        if (pp < 0) rr = replacement;
        else if (s->e[pp].left == p) s->e[pp].left = replacement;
        else s->e[pp].right = replacement;

        s->e[p].left = s->e[p].right = s->e[p].parent = -1;
    }

    s->root[b] = s->e[p].red ? rr : old_cs_balance_deletion(s, rr, replacement);

    if (p == replacement)
    {
        int pp = s->e[p].parent;

        if (pp >= 0)
        {
            if (s->e[pp].left == p) s->e[pp].left = -1;
            else if (s->e[pp].right == p) s->e[pp].right = -1;

            s->e[p].parent = -1;
        }
    }

    return 1;
}

/* The iteration order: bins ascending, the list order inside a bin. */
static void old_cs_order(const struct old_chunkset *s, int *out, int *n)
{
    int k = 0;

    for (int b = 0; b < s->n; ++b)
        for (int i = s->head[b]; i >= 0; i = s->e[i].next) out[k++] = i;

    *n = k;
}


/* The key is in the set: a walk of its bin's list (a tree bin keeps one). */
static int old_cs_has(const struct old_chunkset *s, int64_t key)
{
    if (s->n == 0) return 0;

    for (int i = s->head[old_cs_bin(s, old_cs_spread(key))]; i >= 0; i = s->e[i].next)
        if (s->e[i].key == key) return 1;

    return 0;
}

static int old_cs_order_n(const struct old_chunkset *s, int *out)
{
    int n = 0;
    old_cs_order(s, out, &n);
    return n;
}

/* Keys whose Long.hashCode (cx ^ cz) collides in bulk: a trial's chunks come
 * from a square of side 2..90 somewhere in the world (most trials small, so
 * bins treeify, split, untreeify and treeify again), puts and removes mixed,
 * the order compared after every operation. */
static long test_chunkset(long trials)
{
    static struct old_chunkset os;
    static struct chunkset ns;
    static int oo[OLD_CU_MAX_NODES], no[CU_MAX_NODES];
    static int64_t held[CU_MAX_NODES];
    long ops = 0;

    for (long t = 0; t < trials; ++t)
    {
        old_cs_init(&os);
        cs_init(&ns);

        int side = rnd() % 4 == 0 ? rnd_in(40, 90) : rnd_in(2, 24);
        int x0 = rnd() % 3 == 0 ? (int32_t)(uint32_t)rnd() / 16 : rnd_in(-200, 200);
        int z0 = rnd() % 3 == 0 ? (int32_t)(uint32_t)rnd() / 16 : rnd_in(-200, 200);
        int nops = rnd_in(1, 6000), nheld = 0;
        int put_share = rnd_in(40, 90);

        for (int i = 0; i < nops; ++i, ++ops)
        {
            int op = rnd_in(0, 99);
            int64_t key;

            if (op < put_share || nheld == 0)
            {
                int cx = x0 + rnd_in(0, side - 1), cz = z0 + rnd_in(0, side - 1);
                key = (int64_t)(((uint64_t)(uint32_t)cz << 32) | (uint32_t)cx);
                int was = old_cs_has(&os, key);

                if (was != cs_has(&ns, key)) fail("chunksToUnload contains", t);

                old_cs_put(&os, key);
                cs_put(&ns, key);

                if (!was) held[nheld++] = key;
            }
            else if (op < 98)
            {
                int j = rnd_in(0, nheld - 1);
                key = held[j];
                held[j] = held[--nheld];

                if (old_cs_take(&os, key) != cs_take(&ns, key)) fail("chunksToUnload take", t);
            }
            else
            {
                /* a key that is not held */
                key = (int64_t)rnd();
                if (old_cs_take(&os, key) != cs_take(&ns, key)) fail("chunksToUnload take", t);
            }

            if (os.size != ns.size || os.n != ns.n) { fail("chunksToUnload size", t); break; }

            for (int b = 0; b < os.n; ++b)
                if ((os.root[b] >= 0) != (ns.root[b] >= 0)) { fail("chunksToUnload tree bins", t); break; }

            if (i % 64 == 0)
                for (int b = 0; b < os.n; ++b) cs_tree_bins += os.root[b] >= 0;

            if (i % 7 == 0 || i == nops - 1)
            {
                int a = old_cs_order_n(&os, oo), b = cs_order(&ns, no), same = a == b;

                for (int k = 0; same && k < a; ++k) same = os.e[oo[k]].key == ns.e[no[k]].key;

                if (!same) { fail("chunksToUnload order", t); break; }
            }
        }

    }

    return ops;
}

/* ========================================================================= */

/* ======================================== the windows JDK 8 treeifies
 * (tests/jorder_trees.txt: oracle/tests/JorderTrees.java's orders on the
 * oracle's JDK, spawner and active-set windows with a bin of 9 or more, from
 * a table of their own size, from none, and from a smaller one) */
/* a "b" line: the explosion set's HashSet of ChunkPosition, filled as
 * JorderTrees fills it, through jord_set3 over explosion.c's storage sizes */
static long set3_trees;

static uint64_t set3_st;
static int set3_next(void)
{
    set3_st = set3_st * 6364136223846793005ull + 1442695040888963407ull;
    return (int)(set3_st >> 33);
}

static void test_set3_tree(const char *line)
{
    unsigned long long seed, want;
    int adds, rad, cx, cy, cz, keys, table;
    static int32_t k3[3 * EXPL_MAX_AFFECTED], idx[2 * EXPL_MAX_TABLE], cnt[EXPL_MAX_TABLE], out[3 * EXPL_MAX_AFFECTED];
    struct jord_set3 s;

    if (sscanf(line, "b %llu %d %d %d %d %d %d %d %llx", &seed, &adds, &rad, &cx, &cy, &cz, &keys, &table, &want) != 9) return;
    jord_set3_init(&s, k3, EXPL_MAX_AFFECTED, idx, 2 * EXPL_MAX_TABLE, cnt, EXPL_MAX_TABLE);
    set3_st = seed;
    for (int a = 0; a < adds; ++a)
    {
        int x = cx + set3_next() % (2 * rad + 1) - rad;
        int y = cy + set3_next() % (2 * rad + 1) - rad;
        int z = cz + set3_next() % (2 * rad + 1) - rad;
        jord_set3_add(&s, x, y, z);
    }
    int n = jord_set3_order(&s, out);
    uint64_t h = 0xcbf29ce484222325ull;

    for (int i = 0; i < n * 3; ++i)
        for (int b = 0; b < 4; ++b) { h ^= ((uint32_t)out[i] >> (8 * b)) & 0xff; h *= 0x100000001b3ull; }
    if (n != keys || s.cap != table || h != want)
    {
        if (failures++ < 10)
            fprintf(stderr, "order: the explosion set %llu (%d keys) differs from Java's (%d keys, %d bins, %016llx; Java %d, %d, %016llx)\n",
                    seed, adds, n, s.cap, (unsigned long long)h, keys, table, want);
    }
    ++set3_trees;
}

static long test_trees(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    long n = 0;

    if (f == NULL)
    {
        fprintf(stderr, "order: no %s\n", path);
        ++failures;
        return 0;
    }
    while (fgets(line, sizeof line, f))
    {
        int r, c0, pcx, pcz, keys, table;
        unsigned long long want;
        static int out[JORD_WINDOW_MAX_KEYS * 2];

        if (line[0] == '#') continue;
        if (line[0] == 'b')
        {
            test_set3_tree(line);
            continue;
        }
        if (sscanf(line, "%d %d %d %d %d %d %llx", &r, &c0, &pcx, &pcz, &keys, &table, &want) != 7) continue;

        int bins = c0;
        int k = jord_ccp_window(pcx, pcz, r, &bins, out);
        uint64_t h = 0xcbf29ce484222325ull;

        for (int i = 0; i < k * 2; ++i)
            for (int b = 0; b < 4; ++b) { h ^= ((uint32_t)out[i] >> (8 * b)) & 0xff; h *= 0x100000001b3ull; }
        if (k != keys || bins != table || h != want)
        {
            if (failures++ < 10)
                fprintf(stderr, "order: the window r %d from %d bins at (%d, %d) differs from Java's (%d keys, %d bins, %016llx; Java %d, %d, %016llx)\n",
                        r, c0, pcx, pcz, k, bins, (unsigned long long)h, keys, table, want);
        }
        ++n;
    }
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    long scale = argc > 1 ? atol(argv[1]) : 1;
    long n_spawn = test_spawner(20000 * scale);
    long n_active = test_active(20000 * scale);
    long n_expl = test_explosion(400 * scale);
    long n_pot = test_potions(4000 * scale);
    long n_cs = test_chunkset(300 * scale);
    long n_trees = test_trees("tests/jorder_trees.txt");

    if (failures)
    {
        fprintf(stderr, "order: FAILED, %d mismatches\n", failures);
        return 1;
    }

    printf("order: the new orders match the old emulations: spawner %ld windows, active set %ld, explosion set %ld keys, potion map %ld operations, chunksToUnload %ld operations (%ld tree bins sampled); %ld windows with a tree bin and %ld explosion sets match Java's orders (%ld random ones left out)\n",
           n_spawn, n_active, n_expl, n_pot, n_cs, cs_tree_bins, n_trees, set3_trees, tree_windows);
    return 0;
}
