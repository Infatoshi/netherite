/* chunksToUnload as keys with list stamps, chunkset.h. */
#include "chunkset.h"
#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define CU_MIN 16
/* the membership index: twice the most keys */
#define CU_IDX (2 * CU_MAX_NODES)

static int cs_rotate_left(struct chunkset *s, int root, int p);
static int cs_rotate_right(struct chunkset *s, int root, int p);
static int cs_balance_insertion(struct chunkset *s, int root, int x);
static int cs_balance_deletion(struct chunkset *s, int root, int x);
static void cs_try_presize(struct chunkset *s, int size);

static int cs_spread(int64_t key)
{
    uint32_t h = (uint32_t)((uint64_t)key ^ (uint64_t)key >> 32);   /* Long.hashCode */
    h ^= h >> 16;
    return (int)(h & 0x7fffffffu);
}

static int cs_bin(const struct chunkset *s, int h)
{
    return h & (s->n - 1);
}

/* Long.compareTo: signed. */
static int cs_cmp(int64_t a, int64_t b)
{
    return a < b ? -1 : a > b ? 1 : 0;
}

/* ---------------------------------------------------------- membership */

static uint32_t idx_home(int64_t key)
{
    return (uint32_t)(((uint64_t)key * 0x9e3779b97f4a7c15ull) >> 47) & (CU_IDX - 1);
}

/* The key's slot, -1 when it is not held; *at gets the index position it
 * sits at or would go to. */
static int idx_find(const struct chunkset *s, int64_t key, uint32_t *at)
{
    uint32_t i = idx_home(key);

    for (; s->index[i] != 0; i = (i + 1) & (CU_IDX - 1))
        if (s->e[s->index[i] - 1].key == key)
        {
            *at = i;
            return s->index[i] - 1;
        }

    *at = i;
    return -1;
}

/* Linear probing's delete: the entries after the hole that may move back
 * into it do. */
static void idx_remove(struct chunkset *s, uint32_t i)
{
    uint32_t j = i;

    for (;;)
    {
        s->index[i] = 0;

        for (;;)
        {
            j = (j + 1) & (CU_IDX - 1);

            if (s->index[j] == 0) return;

            uint32_t k = idx_home(s->e[s->index[j] - 1].key);

            if (i <= j ? (i < k && k <= j) : (i < k || k <= j)) continue;

            break;
        }

        s->index[i] = s->index[j];
        i = j;
    }
}

int cs_has(const struct chunkset *s, int64_t key)
{
    uint32_t at;

    return s->e != NULL && idx_find(s, key, &at) >= 0;
}

void cs_restamp(struct chunkset *s, const int64_t *keys, int n)
{
    uint32_t at;
    int64_t next = 1;

    for (int i = 0; i < s->ne; ++i)
        if (s->e[i].live) s->e[i].stamp = 0;
    for (int i = 0; i < n; ++i)
    {
        int slot = idx_find(s, keys[i], &at);
        if (slot >= 0) s->e[slot].stamp = next++;
    }
    s->up = next;
    s->down = -1;
    ++s->gen;
}

/* ------------------------------------------------------------ the set */

void cs_init(struct chunkset *s)
{
    if (s->e == NULL)
    {
        s->count = fixed_array(CU_MAX_BINS, sizeof(int));
        s->root = fixed_array(CU_MAX_BINS, sizeof(int));
        s->count_alt = fixed_array(CU_MAX_BINS, sizeof(int));
        s->root_alt = fixed_array(CU_MAX_BINS, sizeof(int));
        s->start = fixed_array(CU_MAX_BINS, sizeof(int));
        s->run = fixed_array(CU_MAX_NODES, sizeof(int));
        s->index = fixed_array(CU_IDX, sizeof(int32_t));
        s->e = fixed_array(CU_MAX_NODES, sizeof(struct cu_slot));
    }
    else
    {
        /* the index empties key by key */
        for (int i = 0; i < s->ne; ++i)
        {
            uint32_t at;

            if (s->e[i].live && idx_find(s, s->e[i].key, &at) >= 0) idx_remove(s, at);
        }
    }

    s->n = CU_MIN;

    for (int i = 0; i < s->n; ++i) { s->count[i] = 0; s->root[i] = -1; }

    s->size = 0;
    s->sizeCtl = CU_MIN - CU_MIN / 4;
    s->ne = 0;
    s->free = -1;
    s->up = 1;
    s->down = -1;
    ++s->gen;
}

void cs_init_bins(struct chunkset *s, int bins)
{
    cs_init(s);
    if (bins <= s->n || bins > CU_MAX_BINS || (bins & (bins - 1))) return;
    s->n = bins;
    for (int i = 0; i < s->n; ++i) { s->count[i] = 0; s->root[i] = -1; }
    s->sizeCtl = bins - bins / 4;
}

size_t cs_bytes(const struct chunkset *s)
{
    if (s->e == NULL) return 0;
    size_t n = 0;
    const int *bins[5] = {s->count, s->root, s->count_alt, s->root_alt, s->start};
    for (int i = 0; i < 5; ++i) n += fixed_array_resident(bins[i], CU_MAX_BINS, sizeof(int));
    return n + fixed_array_resident(s->run, CU_MAX_NODES, sizeof(int))
         + fixed_array_resident(s->index, CU_IDX, sizeof(int32_t))
         + fixed_array_resident(s->e, CU_MAX_NODES, sizeof(struct cu_slot));
}

void cs_free(struct chunkset *s)
{
    if (s->e != NULL)
    {
        fixed_array_free(s->count, CU_MAX_BINS, sizeof(int));
        fixed_array_free(s->root, CU_MAX_BINS, sizeof(int));
        fixed_array_free(s->count_alt, CU_MAX_BINS, sizeof(int));
        fixed_array_free(s->root_alt, CU_MAX_BINS, sizeof(int));
        fixed_array_free(s->start, CU_MAX_BINS, sizeof(int));
        fixed_array_free(s->run, CU_MAX_NODES, sizeof(int));
        fixed_array_free(s->index, CU_IDX, sizeof(int32_t));
        fixed_array_free(s->e, CU_MAX_NODES, sizeof(struct cu_slot));
    }
    memset(s, 0, sizeof *s);
}

/* A new key's slot, in the index and its bin's count. */
static int cs_add(struct chunkset *s, int64_t key, int h, uint32_t at)
{
    int x;

    if (s->free >= 0)
    {
        x = s->free;
        s->free = s->e[x].parent;
    }
    else
    {
        if (s->ne == CU_MAX_NODES) abort();
        x = s->ne++;
    }

    s->e[x].key = key;
    s->e[x].hash = h;
    s->e[x].parent = s->e[x].left = s->e[x].right = -1;
    s->e[x].red = 0;
    s->e[x].live = 1;
    s->index[at] = x + 1;
    ++s->count[cs_bin(s, h)];
    ++s->size;
    ++s->gen;
    return x;
}

/* The live keys' slots by bin (a counting sort, slots ascending inside a
 * bin), then each bin's run by stamp: the list order. start[b] ends at the
 * end of bin b's run. */
static int cs_runs(struct chunkset *s, int *out)
{
    int sum = 0;

    for (int b = 0; b < s->n; ++b)
    {
        s->start[b] = sum;
        sum += s->count[b];
    }

    for (int i = 0; i < s->ne; ++i)
        if (s->e[i].live) out[s->start[cs_bin(s, s->e[i].hash)]++] = i;

    for (int b = 0, lo = 0; b < s->n; lo = s->start[b++])
    {
        /* insertion sort: a bin holds a few keys */
        for (int k = lo + 1; k < s->start[b]; ++k)
        {
            int x = out[k], j = k;

            for (; j > lo && s->e[out[j - 1]].stamp > s->e[x].stamp; --j) out[j] = out[j - 1];

            out[j] = x;
        }
    }

    return sum;
}

int cs_order(struct chunkset *s, int *out)
{
    return cs_runs(s, out);
}

/* The TreeBin constructor over a bin's list (slots in list order): the tree
 * half of putTreeVal for each in turn. Returns the root. */
static int cs_tree_build(struct chunkset *s, const int *list, int k)
{
    int r = -1;

    for (int i = 0; i < k; ++i)
    {
        int x = list[i];
        s->e[x].parent = s->e[x].left = s->e[x].right = -1;

        if (r < 0)
        {
            s->e[x].red = 0;
            r = x;
            continue;
        }

        for (int p = r;;)
        {
            int dir;

            if (s->e[p].hash > s->e[x].hash) dir = -1;
            else if (s->e[p].hash < s->e[x].hash) dir = 1;
            else dir = cs_cmp(s->e[x].key, s->e[p].key);

            int xp = p;

            if ((p = dir <= 0 ? s->e[xp].left : s->e[xp].right) < 0)
            {
                s->e[x].parent = xp;

                if (dir <= 0) s->e[xp].left = x;
                else s->e[xp].right = x;

                r = cs_balance_insertion(s, r, x);
                break;
            }
        }
    }

    return r;
}

/* One bin's list in order, into s->run. */
static int cs_bin_list(struct chunkset *s, int b)
{
    int k = 0;

    for (int i = 0; i < s->ne; ++i)
        if (s->e[i].live && cs_bin(s, s->e[i].hash) == b) s->run[k++] = i;

    for (int a = 1; a < k; ++a)
    {
        int x = s->run[a], j = a;

        for (; j > 0 && s->e[s->run[j - 1]].stamp > s->e[x].stamp; --j) s->run[j] = s->run[j - 1];

        s->run[j] = x;
    }

    return k;
}

static int cs_rotate_left(struct chunkset *s, int root, int p)
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

static int cs_rotate_right(struct chunkset *s, int root, int p)
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

static int cs_balance_insertion(struct chunkset *s, int root, int x)
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
                    root = cs_rotate_left(s, root, xp);
                    x = xp;
                    xp = s->e[x].parent;
                }

                if (xp >= 0)
                {
                    s->e[xp].red = 0;

                    if ((xpp = s->e[xp].parent) >= 0)
                    {
                        s->e[xpp].red = 1;
                        root = cs_rotate_right(s, root, xpp);
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
                    root = cs_rotate_right(s, root, xp);
                    x = xp;
                    xp = s->e[x].parent;
                }

                if (xp >= 0)
                {
                    s->e[xp].red = 0;

                    if ((xpp = s->e[xp].parent) >= 0)
                    {
                        s->e[xpp].red = 1;
                        root = cs_rotate_left(s, root, xpp);
                    }
                }
            }
        }
    }
}

/* balanceDeletion, from CLR by way of ConcurrentHashMap. */
static int cs_balance_deletion(struct chunkset *s, int root, int x)
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
                root = cs_rotate_left(s, root, xp);
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
                        root = cs_rotate_right(s, root, xpr);
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
                        root = cs_rotate_left(s, root, xp);
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
                root = cs_rotate_right(s, root, xp);
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
                        root = cs_rotate_left(s, root, xpl);
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
                        root = cs_rotate_right(s, root, xp);
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
static void cs_treeify(struct chunkset *s, int b)
{
    if (s->n < 64) { cs_try_presize(s, s->n << 1); return; }

    int k = cs_bin_list(s, b);
    s->root[b] = cs_tree_build(s, s->run, k);
}

/* ConcurrentHashMap.transfer as ported: every bin splits into its lo and hi
 * runs over the new table, each keeping its order, which the stamps already
 * say; a tree bin's run over 6 keys is a TreeBin again, rebuilt over the run
 * when the other run is not empty, the old tree when it is. */
static void cs_resize_once(struct chunkset *s)
{
    int old_n = s->n;
    int *old_root = s->root, *old_count = s->count;

    s->n = old_n * 2;
    if (s->n > CU_MAX_BINS) abort();
    s->sizeCtl = (old_n << 1) - (old_n >> 1);
    s->root = s->root_alt;
    s->count = s->count_alt;
    s->root_alt = old_root;
    s->count_alt = old_count;

    for (int i = 0; i < s->n; ++i) { s->count[i] = 0; s->root[i] = -1; }

    for (int i = 0; i < s->ne; ++i)
        if (s->e[i].live) ++s->count[cs_bin(s, s->e[i].hash)];

    cs_runs(s, s->run);

    for (int b = 0; b < old_n; ++b)
    {
        if (old_root[b] < 0) continue;

        int lo = b, hi = b + old_n, lc = s->count[lo], hc = s->count[hi];

        if (lc > 6) s->root[lo] = hc != 0 ? cs_tree_build(s, &s->run[s->start[lo] - lc], lc) : old_root[b];
        if (hc > 6) s->root[hi] = lc != 0 ? cs_tree_build(s, &s->run[s->start[hi] - hc], hc) : old_root[b];
    }
}

/* tryPresize: the table doubles (possibly more than once) until it holds
 * tableSizeFor(size + size/2 + 1). */
static void cs_try_presize(struct chunkset *s, int size)
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

        cs_resize_once(s);
    }
}

/* ConcurrentHashMap.putVal's addCount: the table doubles once the size
 * reaches sizeCtl. */
static void cs_add_count(struct chunkset *s)
{
    if (s->size >= s->sizeCtl) cs_resize_once(s);
}

void cs_put(struct chunkset *s, int64_t key)
{
    int h = cs_spread(key);
    int b = cs_bin(s, h);
    uint32_t at;
    int held = idx_find(s, key, &at);

    if (s->root[b] >= 0)
    {
        /* the tree bin: a held key is found; a new one goes under its tree
         * parent and to the front of the list */
        if (held >= 0) return;

        int p = s->root[b], xp = -1, dir = 0;

        while (p >= 0)
        {
            int ph = s->e[p].hash;

            if (ph > h) dir = -1;
            else if (ph < h) dir = 1;
            else dir = cs_cmp(key, s->e[p].key);

            xp = p;
            p = dir <= 0 ? s->e[xp].left : s->e[xp].right;
        }

        int x = cs_add(s, key, h, at);
        s->e[x].stamp = s->down--;
        s->e[x].parent = xp;

        if (dir <= 0) s->e[xp].left = x;
        else s->e[xp].right = x;

        if (!s->e[xp].red) s->e[x].red = 1;
        else s->root[b] = cs_balance_insertion(s, s->root[b], x);

        cs_add_count(s);
        return;
    }

    /* the plain bin: putVal's binCount counts the nodes the walk touches,
     * not the appended one, so the bin treeifies when it reaches 9. A held
     * key ends the walk at its place in the list: no node, no addCount, but
     * the treeify check still runs on that count. */
    if (held >= 0)
    {
        if (s->count[b] < 8) return;

        int place = 1;

        for (int i = 0; i < s->ne; ++i)
            if (s->e[i].live && cs_bin(s, s->e[i].hash) == b && s->e[i].stamp < s->e[held].stamp) ++place;

        if (place >= 8) cs_treeify(s, b);
        return;
    }

    int bin_count = s->count[b];
    int x = cs_add(s, key, h, at);
    s->e[x].stamp = s->up++;

    if (bin_count >= 8) cs_treeify(s, b);

    cs_add_count(s);
}

int cs_take(struct chunkset *s, int64_t key)
{
    uint32_t at;
    int p = idx_find(s, key, &at);

    if (p < 0) return 0;

    int b = cs_bin(s, s->e[p].hash);

    idx_remove(s, at);
    --s->count[b];
    --s->size;
    s->e[p].live = 0;

    int r = s->root[b];

    /* a plain bin, or a tree bin left empty, or one too small (by its
     * tree's shape, before the delete) goes back to a list */
    if (r < 0 || s->count[b] == 0 || s->e[r].right < 0 || s->e[r].left < 0 || s->e[s->e[r].left].left < 0)
    {
        s->root[b] = -1;
        s->e[p].parent = s->free;
        s->free = p;
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

    s->root[b] = s->e[p].red ? rr : cs_balance_deletion(s, rr, replacement);

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


    s->e[p].parent = s->free;
    s->free = p;
    return 1;
}
