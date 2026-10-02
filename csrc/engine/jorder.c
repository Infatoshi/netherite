/* JDK 8 hash-container orders as data, jorder.h. */
#include "jorder.h"
#include "envstack.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------- the window: JDK 8's HashMap
 *
 * java.util.HashMap as JDK 8 writes it, for the window's keys: putVal
 * appends to a bin's list, treeifyBin turns a bin that reaches 9 into
 * TreeNodes (a table under 64 resizes instead), resize splits each bin into
 * its low and high runs in order (a tree run of 6 or fewer goes back to a
 * list, a longer one is treeified again when the other run is not empty).
 * A tree bin's iteration order is its next-list: treeify keeps the list's
 * order and moves the red-black root to the front (moveRootToFront), and
 * putTreeVal links a new node after its tree parent, not at the tail. The
 * tree orders by the spread hash as a signed int; two keys of one hash go
 * by tieBreakOrder, which compares System.identityHashCode: taken here as
 * equal identity hashes give it (-1, left), the order the oracle has when
 * every identity hash is one (-XX:hashCode=2): a JVM's own identity hashes
 * are not state the engine has (edgecases3.md CAP-10, lane/longrun). */

/* a key's node: its number in insertion order is its index */
struct jhm_node {
    int32_t hash;
    int16_t next, prev, parent, left, right;
    uint8_t red;
};

struct jhm {
    struct jhm_node *e;
    int16_t *head;      /* each bin's first node (-1 empty) */
    uint8_t *tree;      /* 1 for a TreeNode bin */
    int16_t *head2;     /* resize's new table */
    uint8_t *tree2;
    int cap, size;
    int max_cap;        /* the tables' room */
};

static int jhm_rotate_left(struct jhm_node *e, int root, int p)
{
    int r, pp, rl;

    if (p >= 0 && (r = e[p].right) >= 0)
    {
        if ((rl = e[p].right = e[r].left) >= 0) e[rl].parent = (int16_t)p;
        if ((pp = e[r].parent = e[p].parent) < 0) { root = r; e[r].red = 0; }
        else if (e[pp].left == p) e[pp].left = (int16_t)r;
        else e[pp].right = (int16_t)r;
        e[r].left = (int16_t)p;
        e[p].parent = (int16_t)r;
    }
    return root;
}

static int jhm_rotate_right(struct jhm_node *e, int root, int p)
{
    int l, pp, lr;

    if (p >= 0 && (l = e[p].left) >= 0)
    {
        if ((lr = e[p].left = e[l].right) >= 0) e[lr].parent = (int16_t)p;
        if ((pp = e[l].parent = e[p].parent) < 0) { root = l; e[l].red = 0; }
        else if (e[pp].right == p) e[pp].right = (int16_t)l;
        else e[pp].left = (int16_t)l;
        e[l].right = (int16_t)p;
        e[p].parent = (int16_t)l;
    }
    return root;
}

static int jhm_balance_insertion(struct jhm_node *e, int root, int x)
{
    e[x].red = 1;
    for (;;)
    {
        int xp, xpp, xppl, xppr;

        if ((xp = e[x].parent) < 0) { e[x].red = 0; return x; }
        if (!e[xp].red || (xpp = e[xp].parent) < 0) return root;
        if (xp == (xppl = e[xpp].left))
        {
            if ((xppr = e[xpp].right) >= 0 && e[xppr].red)
            {
                e[xppr].red = 0;
                e[xp].red = 0;
                e[xpp].red = 1;
                x = xpp;
            }
            else
            {
                if (x == e[xp].right)
                {
                    root = jhm_rotate_left(e, root, x = xp);
                    xpp = (xp = e[x].parent) < 0 ? -1 : e[xp].parent;
                }
                if (xp >= 0)
                {
                    e[xp].red = 0;
                    if (xpp >= 0)
                    {
                        e[xpp].red = 1;
                        root = jhm_rotate_right(e, root, xpp);
                    }
                }
            }
        }
        else
        {
            if (xppl >= 0 && e[xppl].red)
            {
                e[xppl].red = 0;
                e[xp].red = 0;
                e[xpp].red = 1;
                x = xpp;
            }
            else
            {
                if (x == e[xp].left)
                {
                    root = jhm_rotate_right(e, root, x = xp);
                    xpp = (xp = e[x].parent) < 0 ? -1 : e[xp].parent;
                }
                if (xp >= 0)
                {
                    e[xp].red = 0;
                    if (xpp >= 0)
                    {
                        e[xpp].red = 1;
                        root = jhm_rotate_left(e, root, xpp);
                    }
                }
            }
        }
    }
}

/* moveRootToFront over the table head (the bin of the root's hash) */
static void jhm_root_to_front(struct jhm *m, int16_t *head, int cap, int root)
{
    struct jhm_node *e = m->e;
    int index = e[root].hash & (cap - 1);
    int first = head[index];

    if (root == first) return;

    int rp = e[root].prev, rn = e[root].next;

    head[index] = (int16_t)root;
    if (rn >= 0) e[rn].prev = (int16_t)rp;
    if (rp >= 0) e[rp].next = (int16_t)rn;
    if (first >= 0) e[first].prev = (int16_t)root;
    e[root].next = (int16_t)first;
    e[root].prev = -1;
}

/* the descent's direction at p for a key of hash h (never equal keys: the
 * window's and the set's keys are distinct) */
static int jhm_dir(const struct jhm_node *e, int p, int32_t h)
{
    int32_t ph = e[p].hash;

    if (ph > h) return -1;
    if (ph < h) return 1;
    return -1;
}

/* TreeNode.treeify over the list at first, into the table head */
static void jhm_treeify(struct jhm *m, int16_t *head, int cap, int first)
{
    struct jhm_node *e = m->e;
    int root = -1;

    /* treeifyBin's TreeNodes link back along the list (a plain Node has no
     * prev) */
    for (int x = first, p = -1; x >= 0; p = x, x = e[x].next) e[x].prev = (int16_t)p;

    for (int x = first, next; x >= 0; x = next)
    {
        next = e[x].next;
        e[x].left = e[x].right = -1;
        if (root < 0)
        {
            e[x].parent = -1;
            e[x].red = 0;
            root = x;
            continue;
        }
        for (int p = root;;)
        {
            int dir = jhm_dir(e, p, e[x].hash), xp = p;

            if ((p = dir <= 0 ? e[p].left : e[p].right) < 0)
            {
                e[x].parent = (int16_t)xp;
                if (dir <= 0) e[xp].left = (int16_t)x;
                else e[xp].right = (int16_t)x;
                root = jhm_balance_insertion(e, root, x);
                break;
            }
        }
    }
    jhm_root_to_front(m, head, cap, root);
}

/* TreeNode.split: bin j of the old table into j and j + bit of the new */
static void jhm_split(struct jhm *m, int j, int bit)
{
    struct jhm_node *e = m->e;
    int lo_head = -1, lo_tail = -1, hi_head = -1, hi_tail = -1, lc = 0, hc = 0;

    for (int x = m->head[j], next; x >= 0; x = next)
    {
        next = e[x].next;
        e[x].next = -1;
        if ((e[x].hash & bit) == 0)
        {
            if ((e[x].prev = (int16_t)lo_tail) < 0) lo_head = x;
            else e[lo_tail].next = (int16_t)x;
            lo_tail = x;
            ++lc;
        }
        else
        {
            if ((e[x].prev = (int16_t)hi_tail) < 0) hi_head = x;
            else e[hi_tail].next = (int16_t)x;
            hi_tail = x;
            ++hc;
        }
    }
    int runs[2][3] = {{lo_head, lc, j}, {hi_head, hc, j + bit}};
    for (int k = 0; k < 2; ++k)
    {
        int h = runs[k][0], c = runs[k][1], b = runs[k][2], other = runs[1 - k][0];

        if (h < 0) continue;
        m->head2[b] = (int16_t)h;
        /* untreeify keeps the order; a run the other left empty keeps its tree */
        if (c <= 6) m->tree2[b] = 0;
        else
        {
            m->tree2[b] = 1;
            if (other >= 0) jhm_treeify(m, m->head2, m->cap * 2, h);
        }
    }
}

static void jhm_resize(struct jhm *m)
{
    struct jhm_node *e = m->e;
    int old = m->cap, cap = old > 0 ? old * 2 : 16;

    if (cap > m->max_cap) abort();
    for (int b = 0; b < cap; ++b) { m->head2[b] = -1; m->tree2[b] = 0; }
    for (int j = 0; j < old; ++j)
    {
        int x = m->head[j];

        if (x < 0) continue;
        if (e[x].next < 0) { m->head2[e[x].hash & (cap - 1)] = (int16_t)x; e[x].prev = -1; }
        else if (m->tree[j]) jhm_split(m, j, old);
        else
        {
            int lo_tail = -1, hi_tail = -1;

            for (int next; x >= 0; x = next)
            {
                next = e[x].next;
                e[x].next = -1;
                int *tail = (e[x].hash & old) == 0 ? &lo_tail : &hi_tail;
                int b = (e[x].hash & old) == 0 ? j : j + old;

                if (*tail < 0) m->head2[b] = (int16_t)x;
                else e[*tail].next = (int16_t)x;
                *tail = x;
            }
        }
    }
    int16_t *h = m->head;
    uint8_t *t = m->tree;
    m->head = m->head2;
    m->tree = m->tree2;
    m->head2 = h;
    m->tree2 = t;
    m->cap = cap;
}

/* HashMap.putVal of node x, a key not held */
static void jhm_put(struct jhm *m, int x)
{
    struct jhm_node *e = m->e;

    if (m->cap == 0) jhm_resize(m);

    int b = e[x].hash & (m->cap - 1);

    e[x].next = e[x].prev = e[x].parent = e[x].left = e[x].right = -1;
    e[x].red = 0;
    if (m->head[b] < 0) m->head[b] = (int16_t)x;
    else if (m->tree[b])
    {
        /* putTreeVal: under the tree parent, after it in the list */
        int root = m->head[b];

        for (int p = root;;)
        {
            int dir = jhm_dir(e, p, e[x].hash), xp = p;

            if ((p = dir <= 0 ? e[p].left : e[p].right) < 0)
            {
                int xpn = e[xp].next;

                e[x].next = (int16_t)xpn;
                if (dir <= 0) e[xp].left = (int16_t)x;
                else e[xp].right = (int16_t)x;
                e[xp].next = (int16_t)x;
                e[x].parent = e[x].prev = (int16_t)xp;
                if (xpn >= 0) e[xpn].prev = (int16_t)x;
                jhm_root_to_front(m, m->head, m->cap, jhm_balance_insertion(e, root, x));
                break;
            }
        }
    }
    else
    {
        int p = m->head[b], bin_count = 0;

        while (e[p].next >= 0) { p = e[p].next; ++bin_count; }
        e[p].next = (int16_t)x;
        e[x].prev = (int16_t)p;
        /* treeifyBin: under 64 bins the table resizes instead */
        if (bin_count >= 7)
        {
            if (m->cap < 64) jhm_resize(m);
            else
            {
                m->tree[b] = 1;
                jhm_treeify(m, m->head, m->cap, m->head[b]);
            }
        }
    }
    if (++m->size > m->cap * 3 / 4) jhm_resize(m);
}

int jord_ccp_window_memo(struct jord_window_memo *m, int pcx, int pcz, int r, int *cap, int *out)
{
    if (m->valid && m->pcx == pcx && m->pcz == pcz && m->r == r && m->cap_in == *cap)
    {
        memcpy(out, m->out, sizeof out[0] * 2 * (size_t)m->n);
        *cap = m->cap_out;
        return m->n;
    }

    int cap_in = *cap, n = jord_ccp_window(pcx, pcz, r, cap, out);

    m->valid = 1;
    m->pcx = pcx;
    m->pcz = pcz;
    m->r = r;
    m->cap_in = cap_in;
    m->cap_out = *cap;
    m->n = n;
    memcpy(m->out, out, sizeof out[0] * 2 * (size_t)n);
    return n;
}

int jord_ccp_window(int pcx, int pcz, int r, int *cap, int *out)
{
    int n = r >= 0 ? (2 * r + 1) * (2 * r + 1) : 0;
    int c0 = *cap;

    if (c0 > JORD_WINDOW_MAX_CAP || n > JORD_WINDOW_MAX_KEYS) return 0;

    /* the table the fill ends at with no bin of 9 on the way: the one it
     * starts from (after clear()), or the doublings its size takes */
    int c1 = c0 > 0 ? c0 : 16;
    while (n > c1 * 3 / 4) c1 *= 2;
    if (c1 > JORD_WINDOW_MAX_CAP) return 0;

    /* a counting sort by bin: the counts, then each bin's first place, then
     * the keys in insertion order */
    uint16_t *start ENV_LOCAL = envstack_take(sizeof *start * (size_t)c1);
    uint32_t mask = (uint32_t)c1 - 1;
    int most = 0;

    memset(start, 0, sizeof start[0] * (size_t)c1);

    for (int dx = -r; dx <= r; ++dx)
        for (int dz = -r; dz <= r; ++dz)
        {
            int c = ++start[jord_spread(jord_ccp_hash(dx + pcx, dz + pcz)) & mask];
            if (c > most) most = c;
        }

    /* a fill that never resizes and never reaches 9 in a bin is a stable
     * sort by bin; one that grows might have treeified on the way */
    if (most <= 8 && c1 == c0)
    {
        int sum = 0;

        for (int b = 0; b < c1; ++b)
        {
            int c = start[b];
            start[b] = (uint16_t)sum;
            sum += c;
        }

        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
            {
                int k = start[jord_spread(jord_ccp_hash(dx + pcx, dz + pcz)) & mask]++;
                out[k * 2] = dx + pcx;
                out[k * 2 + 1] = dz + pcz;
            }

        return sum;
    }

    /* the HashMap itself */
    struct jhm_node *e ENV_LOCAL = envstack_take(sizeof *e * (size_t)n);
    int16_t *tabs ENV_LOCAL = envstack_take(sizeof *tabs * 2 * JORD_WINDOW_MAX_CAP);
    uint8_t *trees ENV_LOCAL = envstack_take(2 * JORD_WINDOW_MAX_CAP);
    struct jhm m = {e, tabs, trees, tabs + JORD_WINDOW_MAX_CAP, trees + JORD_WINDOW_MAX_CAP, c0, 0, JORD_WINDOW_MAX_CAP};

    for (int b = 0; b < c0; ++b) { m.head[b] = -1; m.tree[b] = 0; }

    int k = 0;

    for (int dx = -r; dx <= r; ++dx)
        for (int dz = -r; dz <= r; ++dz, ++k)
        {
            e[k].hash = (int32_t)jord_spread(jord_ccp_hash(dx + pcx, dz + pcz));
            jhm_put(&m, k);
        }

    int got = 0;

    for (int b = 0; b < m.cap; ++b)
        for (int x = m.head[b]; x >= 0; x = e[x].next)
        {
            out[got * 2] = x / (2 * r + 1) - r + pcx;
            out[got * 2 + 1] = x % (2 * r + 1) - r + pcz;
            ++got;
        }

    if (got != n) abort();
    *cap = m.cap;
    return got;
}

/* ------------------------------------------------------------- the set3 */

static uint32_t set3_hash(const int32_t *k)
{
    return jord_spread(jord_cpos_hash(k[0], k[1], k[2]));
}

/* The membership table is twice the JDK table: linear probing from the
 * spread hash, holding key number + 1. */
static void set3_index(struct jord_set3 *s)
{
    int len = s->cap * 2;
    uint32_t mask = (uint32_t)len - 1;

    if (len > s->idx_cap) abort();

    memset(s->index, 0, sizeof *s->index * (size_t)len);

    for (int i = 0; i < s->n; ++i)
    {
        uint32_t p = set3_hash(&s->keys[i * 3]) & mask;

        while (s->index[p] != 0) p = (p + 1) & mask;
        s->index[p] = i + 1;
    }
}

void jord_set3_init(struct jord_set3 *s, int32_t *keys, int max_keys, int32_t *index, int idx_cap,
                    int32_t *count, int max_cap)
{
    s->keys = keys;
    s->index = index;
    s->count = count;
    s->n = 0;
    s->max_keys = max_keys;
    s->cap = 16;
    s->idx_cap = idx_cap;
    s->max_cap = max_cap;
    s->treed = 0;
    memset(s->count, 0, sizeof *s->count * (size_t)s->cap);
    set3_index(s);
}

static int set3_find(const struct jord_set3 *s, int32_t x, int32_t y, int32_t z, uint32_t h, uint32_t *slot)
{
    uint32_t mask = (uint32_t)s->cap * 2 - 1;
    uint32_t p = h & mask;

    for (; s->index[p] != 0; p = (p + 1) & mask)
    {
        const int32_t *k = &s->keys[(s->index[p] - 1) * 3];

        if (k[0] == x && k[1] == y && k[2] == z) return 1;
    }

    *slot = p;
    return 0;
}

int jord_set3_contains(const struct jord_set3 *s, int32_t x, int32_t y, int32_t z)
{
    uint32_t slot;

    return set3_find(s, x, y, z, jord_spread(jord_cpos_hash(x, y, z)), &slot);
}

int jord_set3_add(struct jord_set3 *s, int32_t x, int32_t y, int32_t z)
{
    uint32_t h = jord_spread(jord_cpos_hash(x, y, z)), slot;

    if (set3_find(s, x, y, z, h, &slot)) return 0;

    /* a bin already 8 long takes JDK 8's treeifyBin (a resize under 64
     * bins): the order comes from the HashMap itself (jord_set3_order) */
    uint32_t bin = h & (uint32_t)(s->cap - 1);

    if (s->count[bin] >= 8) s->treed = 1;
    if (s->n == s->max_keys) abort();

    int32_t *k = &s->keys[s->n * 3];
    k[0] = x;
    k[1] = y;
    k[2] = z;
    s->index[slot] = ++s->n;
    ++s->count[bin];

    /* HashMap.putVal's resize: the table doubles once the size passes three
     * quarters of it; the bins' counts and the index follow */
    if (s->n > s->cap * 3 / 4)
    {
        s->cap *= 2;

        if (s->cap > s->max_cap) abort();

        memset(s->count, 0, sizeof *s->count * (size_t)s->cap);

        for (int i = 0; i < s->n; ++i) ++s->count[set3_hash(&s->keys[i * 3]) & (uint32_t)(s->cap - 1)];

        set3_index(s);
    }

    return 1;
}

/* A fill that reached 9 keys in a bin: the keys again, in insertion order,
 * through the JDK 8 HashMap (jhm_put), over the membership index's storage,
 * which the order no longer needs. */
static int set3_order_treed(struct jord_set3 *s, int32_t *out)
{
    int n = s->n, cap = 64;

    while (n > cap * 3 / 4) cap *= 2;
    /* the tables' room: the load's table, or the 64 bins treeifyBin
     * resizes to */
    size_t need = sizeof(struct jhm_node) * (size_t)n + (sizeof(int16_t) + 1) * 2 * (size_t)cap;
    if (n > JORD_SET3_TREED_MAX_KEYS || need > sizeof *s->index * (size_t)s->idx_cap) abort();

    struct jhm_node *e = (struct jhm_node *)(void *)s->index;
    int16_t *tabs = (int16_t *)(void *)(e + n);
    uint8_t *trees = (uint8_t *)(tabs + 2 * cap);
    struct jhm m = {e, tabs, trees, tabs + cap, trees + cap, 0, 0, cap};

    for (int i = 0; i < n; ++i)
    {
        e[i].hash = (int32_t)set3_hash(&s->keys[i * 3]);
        jhm_put(&m, i);
    }

    int got = 0;

    for (int b = 0; b < m.cap; ++b)
        for (int x = m.head[b]; x >= 0; x = e[x].next)
        {
            memcpy(&out[got * 3], &s->keys[x * 3], 3 * sizeof *out);
            ++got;
        }

    if (got != n) abort();
    s->cap = m.cap;
    return got;
}

int jord_set3_order(struct jord_set3 *s, int32_t *out)
{
    if (s->treed) return set3_order_treed(s, out);

    uint32_t mask = (uint32_t)s->cap - 1;
    int sum = 0;

    for (int b = 0; b < s->cap; ++b)
    {
        int c = s->count[b];
        s->count[b] = sum;
        sum += c;
    }

    for (int i = 0; i < s->n; ++i)
    {
        const int32_t *k = &s->keys[i * 3];
        int at = s->count[set3_hash(k) & mask]++;

        out[at * 3] = k[0];
        out[at * 3 + 1] = k[1];
        out[at * 3 + 2] = k[2];
    }

    return s->n;
}
