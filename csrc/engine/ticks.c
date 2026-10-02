/* The scheduled-update path, see ticks.h. The world's own Random (this.rand
 * in the immediate branch) draws only for the lava flow's tick-rate jitter
 * and the fizz sound and smoke, none of which write world state, so its seed
 * and stream never reach a probe output; it is kept so the call shape matches
 * the oracle's. */
#include "ticks.h"

#include <stdlib.h>
#include <string.h>

#include "blocks.h"
#include "arena.h"
#include "env.h"
#include "features_springs.h"
#include "blockwl.h"

#define ID_FIRE 51

/* Material indices, looked up by name so a regenerated blocks.h cannot change
 * what they mean (the same pattern features_lakes.c uses). */
static int mat_air = -1;

/* before main: the index is the same for every environment */
__attribute__((constructor)) static void mat_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, "air") == 0) mat_air = (int)i;
}

static int material_of(int block)
{
    return BLOCKS[block & 4095].material;
}

/* The current set: the replay's dimension, or the environment's own. */
#define TS (nw_env->ticks.cur)

/* World.rand (the immediate branch's updateTick draws) and the base the delay
 * is added to, World.worldInfo.getWorldTotalTime() (the set's world_rand and
 * total_time). The probe paths that never set them keep a Random(1) of their
 * own, as the springs lane's probe world did. */

static jrand *tick_rand(void)
{
    struct ticks_env *te = &nw_env->ticks;

    if (te->cur->world_rand != NULL) return te->cur->world_rand;

    if (!te->own_ready)
    {
        jr_seed(&te->own_rand, 1L);
        te->own_ready = 1;
    }

    return &te->own_rand;
}

/* Block.func_149698_L: Block's default is true, BlockDynamicLiquid overrides
 * to true, and among the registered blocks only BlockFire returns false. */
static int runs_update_at_once(int block)
{
    return (block & 4095) != ID_FIRE;
}

void ticks_set_immediate(int on)
{
    TS->immediate = on;
}

/* BlockFalling.field_149832_M (fallInstantly): true while a chunk provider
 * populates. */

void ticks_set_fall_instantly(int on)
{
    TS->fall_instantly = on;
}

static int falling_can_fall_into(int below)
{
    /* BlockFalling.func_149831_e: air, fire, or water or lava */
    int m = material_of(below);

    return m == mat_air || below == ID_FIRE || MATERIALS[m].is_liquid;
}

/* BlockFalling.updateTick -> func_149830_m, for the immediate branch: with
 * fallInstantly off and the chunks within 32 there, vanilla spawns an
 * EntityFallingBlock and writes nothing until that entity ticks (entities are
 * not run here); otherwise the block drops straight down. */
static void falling_update_now(struct world *w, int x, int y, int z, int id)
{
    if (!falling_can_fall_into(world_get_block(w, x, y - 1, z) & 4095) || y < 0) return;

    if (!TS->fall_instantly && world_check_chunks_exist(w, x - 32, y - 32, z - 32, x + 32, y + 32, z + 32)) return;

    BWL_EMIT(BWL_SET_BLOCK, w, x, y, z, 0, 0, 3);
    BWL_EMIT(BWL_FALL_TAIL, w, x, y, z, id);
}

/* the drop after the block left its cell */
static void falling_tail(struct world *w, int x, int y, int z, int id)
{
    while (falling_can_fall_into(world_get_block(w, x, y - 1, z) & 4095) && y > 0) --y;

    if (y > 0) BWL_EMIT(BWL_SET_BLOCK, w, x, y, z, id, 0, 3);
}

void ticks_set_rand(jrand *r)
{
    TS->world_rand = r;
}

jrand *ticks_get_rand(void)
{
    return TS->world_rand;
}

void ticks_set_total_time(int64_t t)
{
    TS->total_time = t;
}

int ticks_liquid_tick_rate(const struct world *w, int block)
{
    /* BlockLiquid.func_149738_a: water 5, lava 10 in a hasNoSky world (the
     * Nether and the End) and 30 in the overworld. */
    if (MATERIALS[material_of(block)].name != NULL &&
        MATERIALS[material_of(block)].name[0] == 'w')
        return 5;

    return w->dim != 0 ? 10 : 30;
}

/* BlockLiquid.func_149738_a's provider branch: the Nether's lava tick rate. */
int ticks_liquid_tick_rate_world(const struct world *w, int block)
{
    int water = MATERIALS[material_of(block)].name != NULL &&
                MATERIALS[material_of(block)].name[0] == 'w';

    return water ? 5 : (w->dim == -1 ? 10 : 30);
}

/* ------------------------------------------------------------- the pending
 * set: a binary min-heap on (time, priority, entry) and an open-addressing
 * set on (x, y, z, block) for the dedupe. */

/* NextTickListEntry.nextTickEntryID: one counter for every world */
#define NEXT_ID (nw_env->ticks.next_entry_id)

/* One hash slot: the four key values, with x == INT32_MIN for an empty slot. */
struct tick_slot { int32_t x, y, z, block; };

size_t ticks_slot_size(void)
{
    return sizeof(struct tick_slot);
}

static int before(const struct tick_entry *a, const struct tick_entry *b)
{
    if (a->time != b->time) return a->time < b->time;
    if (a->priority != b->priority) return a->priority < b->priority;
    return a->entry < b->entry;
}

static void heap_swap(struct tick_entry *a, struct tick_entry *b)
{
    struct tick_entry t = *a;
    *a = *b;
    *b = t;
}

static int64_t col_key(int x, int z)
{
    return (int64_t)(x >> 4) << 32 | (uint32_t)(z >> 4);
}

static size_t col_slot(const struct ticks_set *s, int64_t key)
{
    uint64_t h = (uint64_t)key * 0x9e3779b97f4a7c15ULL;
    return (size_t)(h >> 32) & (s->ncol - 1);
}

/* the column's count, NULL when the table has no slot for it */
static int32_t *col_find(const struct ticks_set *s, int64_t key)
{
    if (s->ncol == 0) return NULL;

    for (size_t i = col_slot(s, key);; i = (i + 1) & (s->ncol - 1))
    {
        if (s->ccounts[i] < 0) return NULL;
        if (s->ckeys[i] == key) return &s->ccounts[i];
    }
}

/* the table rebuilt from the heap at room for at least want keys at half
 * load (the columns at count 0 dropped) */
static void col_rebuild(struct ticks_set *s, size_t want)
{
    size_t n = 1024;

    while (n < 2 * want) n *= 2;
    slab_give(s->ckeys, s->ncol * sizeof *s->ckeys);
    slab_give(s->ccounts, s->ncol * sizeof *s->ccounts);
    s->ncol = n;
    s->ckeys = slab_take(n * sizeof *s->ckeys);
    s->ccounts = slab_take(n * sizeof *s->ccounts);
    for (size_t i = 0; i < n; ++i) s->ccounts[i] = -1;   /* empty */
    s->used_col = 0;

    for (int p = 0; p < TICKS_PARTS; ++p)
    {
        int m;
        const struct tick_entry *e = ticks_set_part(s, p, &m);

        for (int k = 0; k < m; ++k)
        {
            int64_t key = col_key(e[k].x, e[k].z);
            size_t i = col_slot(s, key);

            while (s->ccounts[i] >= 0 && s->ckeys[i] != key) i = (i + 1) & (n - 1);
            if (s->ccounts[i] < 0)
            {
                s->ckeys[i] = key;
                s->ccounts[i] = 0;
                ++s->used_col;
            }
            ++s->ccounts[i];
        }
    }
}

static void col_add(struct ticks_set *s, int x, int z)
{
    int64_t key = col_key(x, z);
    int32_t *c = col_find(s, key);

    if (c != NULL)
    {
        ++*c;
        return;
    }
    if (2 * (s->used_col + 1) > s->ncol)
    {
        /* the heap's entries are in the new table; the one being pushed is
         * added below */
        col_rebuild(s, s->used_col + 1);
        if ((c = col_find(s, key)) != NULL)
        {
            ++*c;
            return;
        }
    }

    size_t i = col_slot(s, key);

    while (s->ccounts[i] >= 0) i = (i + 1) & (s->ncol - 1);
    s->ckeys[i] = key;
    s->ccounts[i] = 1;
    ++s->used_col;
}

/* the run's i-th entry from its head */
static struct tick_entry *run_at(const struct ticks_run *u, int i)
{
    int k = u->head + i;

    return &u->e[k < u->cap ? k : k - u->cap];
}

/* the ring moved to cap entries, unwrapped from 0 */
static void run_resize(struct ticks_run *u, int cap)
{
    struct tick_entry *e = slab_take((size_t)cap * sizeof *e);
    int first = u->n < u->cap - u->head ? u->n : u->cap - u->head;

    if (u->n > 0)
    {
        memcpy(e, u->e + u->head, (size_t)first * sizeof *e);
        memcpy(e + first, u->e, (size_t)(u->n - first) * sizeof *e);
    }
    slab_give(u->e, (size_t)u->cap * sizeof *u->e);
    u->e = e;
    u->cap = cap;
    u->head = 0;
}

/* the run e goes to: the one whose last it follows most closely, else an
 * empty one; -1 for the heap */
static int run_for(const struct ticks_set *s, const struct tick_entry *e)
{
    int best = -1, empty = -1;

    for (int r = 0; r < TICKS_RUNS; ++r)
    {
        const struct ticks_run *u = &s->runs[r];

        if (u->n == 0)
        {
            if (empty < 0) empty = r;
        }
        else if (before(run_at(u, u->n - 1), e) &&
                 (best < 0 || before(run_at(&s->runs[best], s->runs[best].n - 1), run_at(u, u->n - 1))))
            best = r;
    }
    return best >= 0 ? best : empty;
}

static void heap_push(struct ticks_set *s, struct tick_entry e)
{
    int r = run_for(s, &e);

    if (r >= 0)
    {
        struct ticks_run *u = &s->runs[r];

        /* twice the room when full */
        if (u->n == u->cap) run_resize(u, u->cap ? 2 * u->cap : 1024);
        col_add(s, e.x, e.z);
        *run_at(u, u->n++) = e;
        return;
    }

    if (s->nheap == s->capheap)
    {
        /* a quarter more at a time: a snapshot's worldgen backlog (160,000
         * entries) is most of an environment's tick set while it drains */
        int cap = s->capheap ? s->capheap + s->capheap / 4 : 4096;
        s->heap = slab_grow(s->heap, (size_t)s->capheap * sizeof *s->heap, (size_t)cap * sizeof *s->heap);
        s->capheap = cap;
    }

    col_add(s, e.x, e.z);

    int i = s->nheap++;
    s->heap[i] = e;

    while (i > 0 && before(&s->heap[i], &s->heap[(i - 1) / 2]))
    {
        heap_swap(&s->heap[i], &s->heap[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
}

/* the root taken out and the last entry put back: the hole runs down the
 * smaller children to a leaf (one comparison a level), then the last entry
 * rises from there (Floyd's pop); any heap order gives the same pops, as
 * before() is a total order */
static void heap_pop_root(struct ticks_set *s)
{
    int n = --s->nheap;

    if (n == 0) return;

    struct tick_entry last = s->heap[n];
    int i = 0;

    for (;;)
    {
        int l = 2 * i + 1;

        if (l >= n) break;
        if (l + 1 < n && before(&s->heap[l + 1], &s->heap[l])) ++l;
        s->heap[i] = s->heap[l];
        i = l;
    }
    while (i > 0 && before(&last, &s->heap[(i - 1) / 2]))
    {
        s->heap[i] = s->heap[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    s->heap[i] = last;
}

static size_t slot_of_key(struct ticks_set *s, int x, int y, int z, int block)
{
    uint64_t h = (uint64_t)(uint32_t)x * 0x9e3779b97f4a7c15ULL ^
                 (uint64_t)(uint32_t)y * 0xc2b2ae3d27d4eb4fULL ^
                 (uint64_t)(uint32_t)z * 0x165667b19e3779f9ULL ^
                 (uint64_t)(uint32_t)block * 0x27d4eb2f165667c5ULL;
    h ^= h >> 29;
    return (size_t)h & (s->nslots - 1);
}

static int set_contains(struct ticks_set *s, int x, int y, int z, int block)
{
    if (s->nslots == 0) return 0;

    size_t i = slot_of_key(s, x, y, z, block);

    for (;;)
    {
        struct tick_slot *sl = &s->slots[i];

        if (sl->x == INT32_MIN) return 0;
        if (sl->x == x && sl->y == y && sl->z == z && sl->block == block) return 1;
        i = (i + 1) & (s->nslots - 1);
    }
}

/* the probe and the store; set_add grows first, the regrow calls this
 * directly (its doubled table never needs to grow again) */
static void set_insert(struct ticks_set *s, int x, int y, int z, int block)
{
    size_t i = slot_of_key(s, x, y, z, block);

    while (s->slots[i].x != INT32_MIN) i = (i + 1) & (s->nslots - 1);

    s->slots[i].x = x;
    s->slots[i].y = y;
    s->slots[i].z = z;
    s->slots[i].block = block;
    ++s->used_slots;
}

static void set_remove(struct ticks_set *s, int x, int y, int z, int block)
{
    if (s->nslots == 0) return;

    size_t i = slot_of_key(s, x, y, z, block);

    for (;;)
    {
        struct tick_slot *sl = &s->slots[i];

        if (sl->x == INT32_MIN) return;

        if (sl->x == x && sl->y == y && sl->z == z && sl->block == block) break;

        i = (i + 1) & (s->nslots - 1);
    }

    /* The lookup stops at the first empty slot, so the deletion has to keep
     * every probe chain whole: the slot is vacated and each following entry
     * whose chain crosses it is shifted back (backward-shift deletion). A
     * tombstone would cut the chain and make a live entry invisible, which
     * showed up as the pending set growing two entries past the oracle's.
     * One entry leaves, so the count drops once, not once per shift. */
    --s->used_slots;
    for (;;)
    {
        s->slots[i].x = INT32_MIN;

        size_t j = i;

        for (;;)
        {
            j = (j + 1) & (s->nslots - 1);

            if (s->slots[j].x == INT32_MIN) return;

            size_t k = slot_of_key(s, s->slots[j].x, s->slots[j].y, s->slots[j].z, s->slots[j].block);

            /* i lies on the probe path from k to j (cyclically), so the entry
             * at j can move into i */
            if ((i <= j) ? (k <= i || k > j) : (k <= i && k > j))
            {
                s->slots[i] = s->slots[j];
                i = j;
                break;
            }
        }
    }
}

/* The table at nslots (a power of two, the entries rehashed): only
 * membership is read from it, so its size is not observable. */
static void set_resize(struct ticks_set *s, size_t nslots)
{
    size_t old_cap = s->nslots;
    struct tick_slot *old = s->slots;

    s->nslots = nslots;
    s->slots = slab_take(s->nslots * sizeof *s->slots);

    for (size_t i = 0; i < s->nslots; ++i) s->slots[i].x = INT32_MIN;

    s->used_slots = 0;

    for (size_t i = 0; i < old_cap; ++i)
        if (old[i].x != INT32_MIN) set_insert(s, old[i].x, old[i].y, old[i].z, old[i].block);

    slab_give(old, old_cap * sizeof *old);
}

static void set_add(struct ticks_set *s, int x, int y, int z, int block)
{
    /* up to three quarters full */
    if (s->used_slots * 4 >= s->nslots * 3) set_resize(s, s->nslots ? s->nslots * 2 : 8192);

    set_insert(s, x, y, z, block);
}

void ticks_reset(int64_t entry_id)
{
    struct ticks_set *s = TS;

    s->nheap = 0;
    for (int r = 0; r < TICKS_RUNS; ++r) s->runs[r].head = s->runs[r].n = 0;
    slab_give(s->slots, s->nslots * sizeof *s->slots);
    s->slots = NULL;
    s->nslots = 0;
    s->used_slots = 0;
    slab_give(s->ckeys, s->ncol * sizeof *s->ckeys);
    slab_give(s->ccounts, s->ncol * sizeof *s->ccounts);
    s->ckeys = NULL;
    s->ccounts = NULL;
    s->ncol = s->used_col = 0;
    NEXT_ID = entry_id;
}

int64_t ticks_next_entry_id(void)
{
    return NEXT_ID;
}

void ticks_set_next_entry_id(int64_t id)
{
    NEXT_ID = id;
}

void ticks_load_entry(const struct tick_entry *e)
{
    struct ticks_set *s = TS;

    struct tick_entry c = *e;

    if (set_contains(s, c.x, c.y, c.z, c.block)) return;

    set_add(s, c.x, c.y, c.z, c.block);
    heap_push(s, c);
}

void ticks_load_chunk_entry(int x, int y, int z, int block, int delay, int priority)
{
    struct ticks_set *s = TS;

    int64_t entry_id = NEXT_ID++;

    if (set_contains(s, x, y, z, block & 4095)) return;

    struct tick_entry e;
    e.x = x;
    e.y = y;
    e.z = z;
    e.block = block & 4095;
    e.time = material_of(block) != mat_air ? (int64_t)delay + s->total_time : 0;
    e.priority = priority;
    e.entry = entry_id;

    set_add(s, x, y, z, e.block);
    heap_push(s, e);
}

/* entries ascending by before(), in place: a heapsort (glibc's qsort takes a
 * heap buffer past a few entries); before() is a total order, so the result
 * is qsort's */
static void sort_sift(struct tick_entry *a, int i, int end)
{
    for (;;)
    {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < end && before(&a[m], &a[l])) m = l;
        if (r < end && before(&a[m], &a[r])) m = r;
        if (m == i) return;
        struct tick_entry t = a[i];
        a[i] = a[m];
        a[m] = t;
        i = m;
    }
}

static void sort_entries(struct tick_entry *a, int n)
{
    for (int i = n / 2 - 1; i >= 0; --i) sort_sift(a, i, n);
    for (int end = n - 1; end > 0; --end)
    {
        struct tick_entry t = a[0];
        a[0] = a[end];
        a[end] = t;
        sort_sift(a, 0, end);
    }
}

int ticks_chunk_updates(int cx, int cz, struct tick_entry *out, int max_out)
{
    struct ticks_set *s = TS;

    int x0 = (cx << 4) - 2, x1 = (cx << 4) + 16;
    int z0 = (cz << 4) - 2, z1 = (cz << 4) + 16;
    int n = 0;

    /* the box is in the columns (cx - 1 .. cx, cz - 1 .. cz): none with an
     * entry, nothing to find */
    int any = 0;

    for (int i = -1; i <= 0 && !any; ++i)
        for (int k = -1; k <= 0 && !any; ++k)
        {
            const int32_t *c = col_find(s, col_key((cx + i) << 4, (cz + k) << 4));

            any = c != NULL && *c > 0;
        }
    if (!any) return 0;

    for (int p = 0; p < TICKS_PARTS; ++p)
    {
        int m;
        const struct tick_entry *e = ticks_set_part(s, p, &m);

        for (int i = 0; i < m; ++i)
        {
            if (e[i].x >= x0 && e[i].x < x1 && e[i].z >= z0 && e[i].z < z1)
            {
                if (out && n < max_out) out[n] = e[i];
                ++n;
            }
        }
    }

    if (out && n > 1)
    {
        int count = n < max_out ? n : max_out;
        sort_entries(out, count);
    }

    return n;
}

int ticks_chunk_bound(int cx, int cz)
{
    const struct ticks_set *s = TS;
    int n = 0;

    for (int i = -1; i <= 0; ++i)
        for (int k = -1; k <= 0; ++k)
        {
            const int32_t *c = col_find(s, col_key((cx + i) << 4, (cz + k) << 4));

            if (c != NULL) n += *c;
        }
    return n;
}

int ticks_set_count(const struct ticks_set *s)
{
    int n = s->nheap;

    for (int r = 0; r < TICKS_RUNS; ++r) n += s->runs[r].n;
    return n;
}

const struct tick_entry *ticks_set_part(const struct ticks_set *s, int part, int *n)
{
    if (part == 0)
    {
        *n = s->nheap;
        return s->heap;
    }

    const struct ticks_run *u = &s->runs[(part - 1) / 2];
    int first = u->n < u->cap - u->head ? u->n : u->cap - u->head;

    if (part % 2 == 1)
    {
        *n = first;
        return u->e + u->head;
    }
    *n = u->n - first;
    return u->e;
}

/* where the smallest entry is: a run, TICKS_RUNS for the heap's root, -1
 * when the set is empty */
static int first_at(const struct ticks_set *s)
{
    const struct tick_entry *best = s->nheap > 0 ? &s->heap[0] : NULL;
    int at = best != NULL ? TICKS_RUNS : -1;

    for (int r = 0; r < TICKS_RUNS; ++r)
    {
        const struct ticks_run *u = &s->runs[r];

        if (u->n > 0 && (best == NULL || before(&u->e[u->head], best)))
        {
            best = &u->e[u->head];
            at = r;
        }
    }
    return at;
}

const struct tick_entry *ticks_set_first(const struct ticks_set *s)
{
    int at = first_at(s);

    return at < 0 ? NULL : at == TICKS_RUNS ? &s->heap[0] : &s->runs[at].e[s->runs[at].head];
}

int ticks_pending_count(void)
{
    return ticks_set_count(TS);
}

static inline __attribute__((always_inline)) int ticks_pop_at(struct ticks_set *s, int at, struct tick_entry *out)
{
    if (at < 0) return 0;

    if (at < TICKS_RUNS)
    {
        struct ticks_run *u = &s->runs[at];

        *out = u->e[u->head];
        /* the membership slot of an entry a few pops on, fetched ahead */
        if (u->n > 8 && s->nslots > 0)
        {
            const struct tick_entry *a = run_at(u, 8);
            NW_PREFETCH(&s->slots[slot_of_key(s, a->x, a->y, a->z, a->block)]);
        }
        if (++u->head == u->cap) u->head = 0;
        /* a drained run gives its room back, once empty (a run that fills and
         * empties every tick keeps its room) */
        if (--u->n == 0)
        {
            u->head = 0;
            if (u->cap > 8192) run_resize(u, 4096);
        }
    }
    else
    {
        *out = s->heap[0];
        heap_pop_root(s);
    }
    --*col_find(s, col_key(out->x, out->z));
    set_remove(s, out->x, out->y, out->z, out->block);

    /* a drained backlog gives its room back (the heap's order is the
     * comparison's, a total order, so where the entries sit is not state) */
    if (s->capheap > 4096 && s->nheap < s->capheap / 4)
    {
        s->heap = slab_grow(s->heap, (size_t)s->capheap * sizeof *s->heap, (size_t)(s->capheap / 2) * sizeof *s->heap);
        s->capheap /= 2;
    }
    if (s->nslots > 8192 && s->used_slots * 8 < s->nslots) set_resize(s, s->nslots / 2);

    return 1;
}

int ticks_pop(struct tick_entry *out)
{
    struct ticks_set *s = TS;
    return ticks_pop_at(s, first_at(s), out);
}

/* WorldServer.tickUpdates checks the first entry's time, then removes that
 * same entry. No scheduling happens between those operations. Find the
 * minimum of the eight runs and the heap once for both operations. */
int ticks_pop_due(struct tick_entry *out, int64_t latest)
{
    struct ticks_set *s = TS;
    int at = first_at(s);
    if (at < 0) return 0;
    const struct tick_entry *first = at == TICKS_RUNS ? &s->heap[0] : &s->runs[at].e[s->runs[at].head];
    if (first->time > latest) return 0;
    return ticks_pop_at(s, at, out);
}

int ticks_first(struct tick_entry *out)
{
    const struct tick_entry *first = ticks_set_first(TS);

    if (first == NULL) return 0;
    *out = *first;
    return 1;
}

int ticks_peek(int n, struct tick_entry *out)
{
    struct ticks_set *s = TS;

    int total = ticks_set_count(s);

    if (n <= 0 || total == 0) return 0;
    if (n > total) n = total;

    /* the k smallest in order: a candidate heap of indices into the main heap,
     * seeded with the root, each pop pushing that node's children */
    /* the tick peeks one; a check that reads the whole set gets a heap */
    int small[3 * TICKS_PEEK_MAX];
    int *cand = n <= TICKS_PEEK_MAX ? small : slab_take((size_t)(3 * n) * sizeof *cand);
    int ncand = 0, got = 0, rh[TICKS_RUNS];

    for (int r = 0; r < TICKS_RUNS; ++r) rh[r] = 0;
    if (s->nheap > 0) cand[ncand++] = 0;

    /* the heap's in order merged with the runs' */
    while (got < n)
    {
        const struct tick_entry *best = ncand > 0 ? &s->heap[cand[0]] : NULL;
        int at = -1;

        for (int r = 0; r < TICKS_RUNS; ++r)
            if (rh[r] < s->runs[r].n && (best == NULL || before(run_at(&s->runs[r], rh[r]), best)))
            {
                best = run_at(&s->runs[r], rh[r]);
                at = r;
            }
        if (best == NULL) break;
        if (at >= 0)
        {
            out[got++] = *best;
            ++rh[at];
            continue;
        }

        int top = cand[0];
        cand[0] = cand[--ncand];

        if (ncand > 0)
        {
            int i = 0;

            for (;;)
            {
                int l = 2 * i + 1, r = l + 1, m = i;

                if (l < ncand && before(&s->heap[cand[l]], &s->heap[cand[m]])) m = l;
                if (r < ncand && before(&s->heap[cand[r]], &s->heap[cand[m]])) m = r;
                if (m == i) break;

                int t = cand[i];
                cand[i] = cand[m];
                cand[m] = t;
                i = m;
            }
        }

        out[got++] = s->heap[top];

        for (int c = 2 * top + 1; c <= 2 * top + 2 && c < s->nheap; ++c)
        {
            int i = ncand++;
            cand[i] = c;

            while (i > 0 && before(&s->heap[cand[i]], &s->heap[cand[(i - 1) / 2]]))
            {
                int t = cand[i];
                cand[i] = cand[(i - 1) / 2];
                cand[(i - 1) / 2] = t;
                i = (i - 1) / 2;
            }
        }
    }

    if (cand != small) slab_give(cand, (size_t)(3 * n) * sizeof *cand);
    return got;
}

void ticks_walk(void (*cb)(void *ctx, const struct tick_entry *e), void *ctx)
{
    struct ticks_set *s = TS;

    int total = ticks_set_count(s);
    const size_t copy_bytes = (size_t)(total ? total : 1) * sizeof(struct tick_entry);
    struct tick_entry *copy = slab_take(copy_bytes);

    int at = 0;

    for (int p = 0; p < TICKS_PARTS; ++p)
    {
        int m;
        const struct tick_entry *e = ticks_set_part(s, p, &m);

        memcpy(copy + at, e, (size_t)m * sizeof *copy);
        at += m;
    }
    sort_entries(copy, total);

    for (int i = 0; i < total; ++i) cb(ctx, &copy[i]);

    slab_give(copy, copy_bytes);
}

/* The schedule, from a worklist step or in place: under the immediate flag a
 * liquid's or a falling block's update is the call it makes (emitted). */
static void sched_body(struct world *w, int x, int y, int z, int block, int delay, int priority)
{
    struct ticks_set *s = TS;

    if (w->is_remote) return;

    /* WorldServer.func_147454_a constructs its NextTickListEntry before it
     * does anything else, and that construction is what takes the id: the
     * counter advances for a call the hash set then dedupes away, and for the
     * immediate branch's consumed entry, exactly as it does in vanilla (the
     * tick ids are the tie-break the pending set's order uses). */
    int64_t entry_id = NEXT_ID++;

    if (s->immediate && material_of(block) != mat_air)
    {
        if (runs_update_at_once(block))
        {
            /* var8 = 8: the update needs every chunk within 8 blocks */
            if (world_check_chunks_exist(w, x - 8, y - 8, z - 8, x + 8, y + 8, z + 8))
            {
                int here = world_get_block(w, x, y, z) & 4095;

                if (material_of(here) != mat_air && here == (block & 4095))
                {
                    /* Block.updateTick is virtual by the stored block's class:
                     * the dynamic liquids flow, the static ones only move their
                     * level or spread fire, and BlockFalling drops through. */
                    switch (here)
                    {
                    case 8:
                    case 10:
                        bwl_emit_(BWL_LIQUID_TICK, w, x, y, z, (int[BWL_ARGS]){block}, tick_rand());
                        break;
                    case 9:
                    case 11:
                        bwl_emit_(BWL_STATIC_TICK, w, x, y, z, (int[BWL_ARGS]){block}, tick_rand());
                        break;
                    case 12:
                    case 13:
                        falling_update_now(w, x, y, z, here);
                        break;
                    default:
                        break;
                    }
                }
            }

            return;
        }

        delay = 1;
    }

    if (!world_check_chunks_exist(w, x, y, z, x, y, z)) return;

    if (set_contains(s, x, y, z, block & 4095)) return;

    struct tick_entry e;
    e.x = x;
    e.y = y;
    e.z = z;
    e.block = block & 4095;
    e.time = material_of(block) != mat_air ? (int64_t)delay + s->total_time : 0;
    /* setPriority runs beside setScheduledTime, for a block that is not air */
    e.priority = material_of(block) != mat_air ? priority : 0;
    e.entry = entry_id;

    set_add(s, x, y, z, e.block);
    heap_push(s, e);
}

/* 1 when the schedule runs a block update at once: the immediate flag, and a
 * block whose updateTick is ported for it (sched_body's switch) */
static int sched_calls_back(int block)
{
    int b = block & 4095;

    return TS->immediate && b >= 8 && b <= 13;
}

void ticks_schedule_block_update(struct world *w, int x, int y, int z, int block, int delay)
{
    if (sched_calls_back(block)) BWL_CALL(BWL_SCHED, w, x, y, z, block, delay, 0);
    else sched_body(w, x, y, z, block, delay, 0);
}

void ticks_bwl_sched(struct world *w, int x, int y, int z, int block, int delay)
{
    if (!bwl_pending() && !sched_calls_back(block)) sched_body(w, x, y, z, block, delay, 0);
    else BWL_EMIT(BWL_SCHED, w, x, y, z, block, delay, 0);
}

void ticks_schedule_priority(struct world *w, int x, int y, int z, int block, int delay, int priority)
{
    if (sched_calls_back(block)) BWL_CALL(BWL_SCHED, w, x, y, z, block, delay, priority);
    else sched_body(w, x, y, z, block, delay, priority);
}

void ticks_bwl_sched_priority(struct world *w, int x, int y, int z, int block, int delay, int priority)
{
    if (!bwl_pending() && !sched_calls_back(block)) sched_body(w, x, y, z, block, delay, priority);
    else BWL_EMIT(BWL_SCHED, w, x, y, z, block, delay, priority);
}

/* Block.isEqualTo: the same block, or a pair func_149667_c accepts (a
 * diode's powered and unpowered forms, the two redstone torches). */
static int block_equal(int a, int b)
{
    a &= 4095;
    b &= 4095;
    if (a == b) return 1;
    if ((a == 93 || a == 94) && (b == 93 || b == 94)) return 1;
    if ((a == 149 || a == 150) && (b == 149 || b == 150)) return 1;
    if ((a == 75 || a == 76) && (b == 75 || b == 76)) return 1;
    return 0;
}

int ticks_scheduled_this_tick(int x, int y, int z, int block)
{
    struct ticks_set *s = TS;

    if (s->batch == NULL) return 0;

    for (int i = s->batch_i + 1; i < s->batch_n; ++i)
    {
        const struct tick_entry *e = &s->batch[i];

        if (e->x == x && e->y == y && e->z == z && block_equal(e->block, block)) return 1;
    }

    return 0;
}

void ticks_batch_begin(const struct tick_entry *batch, int n)
{
    TS->batch = batch;
    TS->batch_n = n;
    TS->batch_i = -1;
}

void ticks_batch_at(int i)
{
    TS->batch_i = i;
}

void ticks_batch_end(void)
{
    TS->batch = NULL;
    TS->batch_n = 0;
    TS->batch_i = -1;
}

int ticks_bwl_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;

    switch (f->op.kind)
    {
    case BWL_SCHED:
        sched_body(w, f->op.x, f->op.y, f->op.z, f->op.a[0], f->op.a[1], f->op.a[2]);
        return 1;

    case BWL_FALL_TAIL:
        falling_tail(w, f->op.x, f->op.y, f->op.z, f->op.a[0]);
        return 1;

    default:
        return 1;
    }
}


/* A set moved aside whole, with the counter as it stood (the saved-world
 * build runs its own ticks in between). */
struct ticks_backup {
    struct ticks_set set;
    int64_t next_entry_id;
};

struct ticks_backup *ticks_save(void)
{
    struct ticks_set *s = TS;
    struct ticks_backup *b = slab_take(sizeof *b);

    b->set = *s;
    b->next_entry_id = NEXT_ID;
    s->heap = NULL;
    s->nheap = s->capheap = 0;
    memset(s->runs, 0, sizeof s->runs);
    s->slots = NULL;
    s->nslots = s->used_slots = 0;
    s->ckeys = NULL;
    s->ccounts = NULL;
    s->ncol = s->used_col = 0;
    return b;
}

void ticks_restore(struct ticks_backup *b)
{
    struct ticks_set *s = TS;

    ticks_reset(0);
    slab_give(s->heap, (size_t)s->capheap * sizeof *s->heap);
    for (int r = 0; r < TICKS_RUNS; ++r) slab_give(s->runs[r].e, (size_t)s->runs[r].cap * sizeof *s->runs[r].e);
    *s = b->set;
    NEXT_ID = b->next_entry_id;
    slab_give(b, sizeof *b);
}

void ticks_use(struct ticks_set *set)
{
    nw_env->ticks.cur = set != NULL ? set : &nw_env->ticks.own;
}

void ticks_set_free(struct ticks_set *s)
{
    slab_give(s->heap, (size_t)s->capheap * sizeof *s->heap);
    for (int r = 0; r < TICKS_RUNS; ++r) slab_give(s->runs[r].e, (size_t)s->runs[r].cap * sizeof *s->runs[r].e);
    slab_give(s->slots, s->nslots * sizeof *s->slots);
    slab_give(s->ckeys, s->ncol * sizeof *s->ckeys);
    slab_give(s->ccounts, s->ncol * sizeof *s->ccounts);
    memset(s, 0, sizeof *s);
}
