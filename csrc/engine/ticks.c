/* The scheduled-update path, see ticks.h. The world's own Random (this.rand
 * in the immediate branch) draws only for the lava flow's tick-rate jitter
 * and the fizz sound and smoke, none of which write world state, so its seed
 * and stream never reach a probe output; it is kept so the call shape matches
 * the oracle's. */
#include "ticks.h"

#include <stdio.h>
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
#define TS (neth_env->ticks.cur)

/* World.rand (the immediate branch's updateTick draws) and the base the delay
 * is added to, World.worldInfo.getWorldTotalTime() (the set's world_rand and
 * total_time). The probe paths that never set them keep a Random(1) of their
 * own, as the springs lane's probe world did. */

static jrand *tick_rand(void)
{
    struct ticks_env *te = &neth_env->ticks;

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
 * set: each chunk column's entries in order on (time, priority, entry), a
 * min-heap of the columns by their first, and an open-addressing set on
 * (x, y, z, block) for the dedupe. */

/* NextTickListEntry.nextTickEntryID: one counter for every world */
#define NEXT_ID (neth_env->ticks.next_entry_id)

#ifdef TICKS_VERIFY
/* The verify build (make ... CFLAGS+=-DTICKS_VERIFY, never the product's):
 * a frozen set keeps the set it was (shadow), a push goes to both, and the
 * public calls below are wrappers that run the frozen set's answer and the
 * shadow's and compare; the counts print at exit. Process-wide counters:
 * the build fails `make globals` on purpose. */
static long long tv_checks, tv_bad, tv_frozen;

static void tv_check(int ok, const char *what)
{
    __atomic_add_fetch(&tv_checks, 1, __ATOMIC_RELAXED);
    if (!ok && __atomic_add_fetch(&tv_bad, 1, __ATOMIC_RELAXED) <= 20)
        fprintf(stderr, "ticks verify: %s differs from the unfrozen set's\n", what);
}

__attribute__((destructor)) static void tv_report(void)
{
    fprintf(stderr, "ticks verify: %lld sets frozen, %lld checks, %lld mismatches\n", tv_frozen, tv_checks, tv_bad);
}

#define ticks_chunk_updates ticks_chunk_updates_one
#define ticks_chunk_bound ticks_chunk_bound_one
#define ticks_set_count ticks_set_count_one
#define ticks_set_first ticks_set_first_one
#define ticks_pop ticks_pop_one
#define ticks_pop_due ticks_pop_due_one
#define ticks_pop_due_n ticks_pop_due_n_one
#define ticks_pop_due_nb ticks_pop_due_nb_one
#define ticks_pop_due_view ticks_pop_due_view_one
#define ticks_peek ticks_peek_one

void ticks_verify(int ok, const char *what)
{
    tv_check(ok, what);
}
#endif

static int before(const struct tick_entry *a, const struct tick_entry *b)
{
    if (a->time != b->time) return a->time < b->time;
    if (a->priority != b->priority) return a->priority < b->priority;
    return a->entry < b->entry;
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

/* the column's slot, NULL when the table has none for it */
static struct ticks_col *col_find(const struct ticks_set *s, int64_t key)
{
    if (s->ncol == 0) return NULL;

    for (size_t i = col_slot(s, key);; i = (i + 1) & (s->ncol - 1))
    {
        if (s->cols[i].n < 0) return NULL;
        if (s->cols[i].key == key) return &s->cols[i];
    }
}

/* ------------------------------------------------ the heap of the columns:
 * one node a column with entries, keyed by its first (the order's three
 * fields copied, so a sift reads only the node array); a column's at is its
 * node, -1 for none */

static int head_before(const struct ticks_head *a, const struct ticks_head *b)
{
    if (a->time != b->time) return a->time < b->time;
    if (a->priority != b->priority) return a->priority < b->priority;
    return a->entry < b->entry;
}

static void head_of(struct ticks_head *h, const struct ticks_col *c, int32_t col)
{
    const struct tick_entry *f = &c->e[c->head];

    h->time = f->time;
    h->entry = f->entry;
    h->priority = f->priority;
    h->col = col;
}

static void node_put(struct ticks_set *s, int i, const struct ticks_head *h)
{
    s->order[i] = *h;
    s->cols[h->col].at = i;
}

static void node_up(struct ticks_set *s, int i)
{
    struct ticks_head h = s->order[i];

    while (i > 0 && head_before(&h, &s->order[(i - 1) / 2]))
    {
        node_put(s, i, &s->order[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
    node_put(s, i, &h);
}

static void node_down(struct ticks_set *s, int i)
{
    struct ticks_head h = s->order[i];
    int n = s->norder;

    for (;;)
    {
        int l = 2 * i + 1;

        if (l >= n) break;
        if (l + 1 < n && head_before(&s->order[l + 1], &s->order[l])) ++l;
        if (!head_before(&s->order[l], &h)) break;
        node_put(s, i, &s->order[l]);
        i = l;
    }
    node_put(s, i, &h);
}

/* column c (slot col) joins the heap */
static void node_add(struct ticks_set *s, struct ticks_col *c, int32_t col)
{
    if (s->norder == s->caporder)
    {
        int cap = s->caporder ? 2 * s->caporder : 256;
        s->order = slab_grow(s->order, (size_t)s->caporder * sizeof *s->order, (size_t)cap * sizeof *s->order);
        s->caporder = cap;
    }
    head_of(&s->order[s->norder], c, col);
    c->at = s->norder++;
    node_up(s, c->at);
}

/* node i leaves the heap (its column emptied) */
static void node_drop(struct ticks_set *s, int i)
{
    s->cols[s->order[i].col].at = -1;
    if (i == --s->norder) return;

    struct ticks_head h = s->order[s->norder];

    node_put(s, i, &h);
    if (i > 0 && head_before(&h, &s->order[(i - 1) / 2])) node_up(s, i);
    else node_down(s, i);
}

/* the table moved to room for at least want keys at half load, each
 * column's entries kept (the columns at count 0 dropped; they hold no room),
 * and the heap made again over the new slots */
static void col_rebuild(struct ticks_set *s, size_t want)
{
    size_t n = 1024;

    while (n < 2 * want) n *= 2;

    struct ticks_col *old = s->cols;
    size_t nold = s->ncol;

    s->ncol = n;
    s->cols = slab_take(n * sizeof *s->cols);
    for (size_t i = 0; i < n; ++i) s->cols[i].n = -1;   /* empty */
    s->used_col = 0;
    s->norder = 0;

    for (size_t k = 0; k < nold; ++k)
    {
        if (old[k].n <= 0) continue;

        size_t i = col_slot(s, old[k].key);

        while (s->cols[i].n >= 0) i = (i + 1) & (n - 1);
        s->cols[i] = old[k];
        ++s->used_col;
        node_add(s, &s->cols[i], (int32_t)i);
    }
    slab_give(old, nold * sizeof *old);
}

/* ------------------------------------------- a column's membership: each
 * (x, y, z, block) its column holds as one word, x & 15, z & 15, y (nine
 * bits: checkChunksExist refuses a y outside 0..255 before any schedule) and
 * the block; ~0 an empty slot. One entry a key, so a column's keys are its
 * n; the table at most half full. */

#define KEY_EMPTY 0xffffffffu

static uint32_t mem_key(int x, int y, int z, int block)
{
    return (uint32_t)(x & 15) | (uint32_t)(z & 15) << 4 | ((uint32_t)y & 511u) << 8 | (uint32_t)(block & 4095) << 17;
}

static uint32_t mem_slot(const struct ticks_col *c, uint32_t k)
{
    return (k * 0x9e3779b1u) >> (32 - __builtin_ctz((unsigned)c->kcap));
}

static int mem_has(const struct ticks_col *c, uint32_t k)
{
    if (c->kcap == 0) return 0;
    for (uint32_t i = mem_slot(c, k);; i = (i + 1) & (uint32_t)(c->kcap - 1))
    {
        if (c->keys[i] == k) return 1;
        if (c->keys[i] == KEY_EMPTY) return 0;
    }
}

static void mem_put(struct ticks_col *c, uint32_t k)
{
    uint32_t i = mem_slot(c, k);

    while (c->keys[i] != KEY_EMPTY) i = (i + 1) & (uint32_t)(c->kcap - 1);
    c->keys[i] = k;
}

/* the table at kcap slots, its keys put again (where a key sits is not
 * state: only membership is read) */
static void mem_resize(struct ticks_col *c, int kcap)
{
    uint32_t *old = c->keys;
    int ocap = c->kcap;

    c->kcap = kcap;
    c->keys = slab_take((size_t)kcap * sizeof *c->keys);
    memset(c->keys, 0xff, (size_t)kcap * sizeof *c->keys);
    for (int i = 0; i < ocap; ++i)
        if (old[i] != KEY_EMPTY) mem_put(c, old[i]);
    slab_give(old, (size_t)ocap * sizeof *old);
}

/* k joins, with the column's n already counting it */
static void mem_add(struct ticks_col *c, uint32_t k)
{
    if (2 * c->n > c->kcap) mem_resize(c, c->kcap ? 2 * c->kcap : 16);
    mem_put(c, k);
}

/* k leaves, with the column's n no longer counting it: the slot vacated and
 * each later key whose probe crosses it shifted back (backward-shift
 * deletion keeps every probe chain whole). The table keeps its room until
 * the column empties (set_pop_first gives it back then): shrunk at an
 * eighth full, a column whose count swung across that line resized at each
 * swing (at 200-step episodes a resize every 8.5 pushes, 36% of them
 * shrinks; lane/blockticks). */
static void mem_del(struct ticks_col *c, uint32_t k)
{
    const uint32_t mask = (uint32_t)(c->kcap - 1);
    uint32_t i = mem_slot(c, k);

    while (c->keys[i] != k) i = (i + 1) & mask;
    for (;;)
    {
        c->keys[i] = KEY_EMPTY;

        uint32_t j = i;

        for (;;)
        {
            j = (j + 1) & mask;
            if (c->keys[j] == KEY_EMPTY) return;

            uint32_t h = mem_slot(c, c->keys[j]);

            if ((i <= j) ? (h <= i || h > j) : (h <= i && h > j))
            {
                c->keys[i] = c->keys[j];
                i = j;
                break;
            }
        }
    }
}

/* ------------------------------------------------ the base (ticks.h): a
 * start's entries, read only, the set's from bcur on */

static uint32_t base_hash(int x, int y, int z, int block)
{
    uint64_t h = (uint64_t)(uint32_t)x * 0x9e3779b97f4a7c15ULL ^ (uint64_t)(uint32_t)z * 0xc2b2ae3d27d4eb4fULL ^
                 ((uint64_t)(uint32_t)y << 12 | (uint32_t)block) * 0x165667b19e3779f9ULL;

    return (uint32_t)(h >> 32);
}

/* the index of b's entry equal to (x, y, z, block), -1 for none */
static int base_find(const struct ticks_base *b, int x, int y, int z, int block)
{
    const uint32_t mask = b->nmem - 1;

    for (uint32_t i = base_hash(x, y, z, block) & mask;; i = (i + 1) & mask)
    {
        uint32_t v = b->mem[i];

        if (v == 0) return -1;

        const struct tick_entry *e = &b->e[v - 1];

        if (e->x == x && e->y == y && e->z == z && e->block == block) return (int)(v - 1);
    }
}

static const struct ticks_bcol *base_col(const struct ticks_base *b, int64_t key)
{
    const uint32_t mask = b->ncol - 1;

    for (uint32_t i = (uint32_t)(((uint64_t)key * 0x9e3779b97f4a7c15ULL) >> 32) & mask;; i = (i + 1) & mask)
    {
        if (b->col[i].n == 0) return NULL;
        if (b->col[i].key == key) return &b->col[i];
    }
}

/* the live part of the base's column key (part 0 all of it, 1 to 3 its
 * strips part[0] to part[2]): its indices at or past bcur, *ix the first;
 * the count */
static int base_col_live(const struct ticks_set *s, int64_t key, int part, const int32_t **ix)
{
    const struct ticks_base *b = s->base;
    const struct ticks_bcol *c = b != NULL ? base_col(b, key) : NULL;

    if (c == NULL) return 0;

    const int32_t *v = part == 0 ? b->ix + c->at : b->six + c->part[part - 1][0];
    int lo = 0, hi = part == 0 ? c->n : c->part[part - 1][1];
    const int len = hi;

    while (lo < hi)
    {
        int m = (lo + hi) / 2;

        if (v[m] < s->bcur) lo = m + 1;
        else hi = m;
    }
    *ix = v + lo;
    return len - lo;
}

/* the base's first live entry, NULL for none */
static inline const struct tick_entry *base_first(const struct ticks_set *s)
{
    return s->base != NULL ? &s->base->e[s->bcur] : NULL;
}

/* k more of the base's entries taken; drained, the set lets go of it */
static inline void base_take(struct ticks_set *s, int k)
{
    ++s->mods;
    s->bcur += k;
    if (s->bcur == s->base->n)
    {
        s->base = NULL;
        s->bcur = 0;
    }
}

static int base_live(const struct ticks_set *s)
{
    return s->base != NULL ? s->base->n - s->bcur : 0;
}

/* the set holds an entry equal to (x, y, z, block): the HashSet's contains */
/* the base's column of key holds a live entry: its indices rise, so its last
 * is at or past bcur (a base entry of the column is no later than it) */
static int base_col_has_live(const struct ticks_set *s, int64_t key)
{
    const struct ticks_bcol *c = base_col(s->base, key);

    return c != NULL && c->n > 0 && s->base->ix[c->at + c->n - 1] >= s->bcur;
}

static int set_contains(struct ticks_set *s, int x, int y, int z, int block)
{
    const int64_t key = col_key(x, z);
    const struct ticks_col *c = col_find(s, key);
    const int r = (c != NULL && c->n > 0 && mem_has(c, mem_key(x, y, z, block))) ||
                  (s->base != NULL && base_col_has_live(s, key) && base_find(s->base, x, y, z, block) >= s->bcur);

#ifdef TICKS_VERIFY
    if (s->shadow != NULL) tv_check(r == set_contains(s->shadow, x, y, z, block), "contains");
#endif
    return r;
}

/* e into its column, after every entry before() puts first (the schedules of
 * a tick come in rising time and id, so the walk back is short), its key into
 * the column's membership, and the column's node where its first changed */
static void set_push(struct ticks_set *s, const struct tick_entry *e)
{
#ifdef TICKS_VERIFY
    if (s->shadow != NULL) set_push(s->shadow, e);
#endif
    if (e->y < -256 || e->y > 255)
    {
        fprintf(stderr, "ticks: a pending tick at y %d is outside the membership's range\n", e->y);
        abort();
    }

    int64_t key = col_key(e->x, e->z);
    struct ticks_col *c = col_find(s, key);

    if (c == NULL)
    {
        if (2 * (s->used_col + 1) > s->ncol) col_rebuild(s, s->used_col + 1);

        size_t i = col_slot(s, key);

        while (s->cols[i].n >= 0 && s->cols[i].key != key) i = (i + 1) & (s->ncol - 1);
        c = &s->cols[i];
        if (c->n < 0)
        {
            c->key = key;
            c->e = NULL;
            c->head = c->n = c->cap = 0;
            c->at = -1;
            c->keys = NULL;
            c->kcap = 0;
            ++s->used_col;
        }
    }
    if (c->head + c->n == c->cap)
    {
        if (c->head >= c->n && c->head > 0)
        {
            /* half of the room is popped: slide back */
            memmove(c->e, c->e + c->head, (size_t)c->n * sizeof *c->e);
            c->head = 0;
        }
        else
        {
            /* half again, to the end of the slab class it rounds up to */
            int cap = (int)(slab_block_bytes((size_t)(c->cap ? c->cap + c->cap / 2 : 8) * sizeof *c->e) / sizeof *c->e);
            struct tick_entry *ne = slab_take((size_t)cap * sizeof *ne);

            if (c->n > 0) memcpy(ne, c->e + c->head, (size_t)c->n * sizeof *ne);
            slab_give(c->e, (size_t)c->cap * sizeof *c->e);
            c->e = ne;
            c->cap = cap;
            c->head = 0;
        }
    }

    struct tick_entry *a = c->e + c->head;
    int p = c->n;

    while (p > 0 && before(e, &a[p - 1])) --p;
    if (p < c->n) memmove(a + p + 1, a + p, (size_t)(c->n - p) * sizeof *a);
    a[p] = *e;
    ++c->n;
    ++s->count;
    ++s->mods;
    mem_add(c, mem_key(e->x, e->y, e->z, e->block));
    if (c->at < 0) node_add(s, c, (int32_t)(c - s->cols));
    else if (p == 0)
    {
        head_of(&s->order[c->at], c, (int32_t)(c - s->cols));
        node_up(s, c->at);
    }
}

/* the set's smallest out, into *out: the first of the root's column, its key
 * out of the column's membership */
static void set_pop_first(struct ticks_set *s, struct tick_entry *out)
{
    int32_t col = s->order[0].col;
    struct ticks_col *c = &s->cols[col];

    *out = c->e[c->head];
    ++c->head;
    --s->count;
    ++s->mods;
    if (--c->n == 0)
    {
        slab_give(c->e, (size_t)c->cap * sizeof *c->e);
        slab_give(c->keys, (size_t)c->kcap * sizeof *c->keys);
        c->e = NULL;
        c->keys = NULL;
        c->head = c->cap = c->kcap = 0;
        node_drop(s, 0);
        return;
    }
    mem_del(c, mem_key(out->x, out->y, out->z, out->block));
    head_of(&s->order[0], c, col);
    node_down(s, 0);
}

/* every column's room, the table and the heap; the base let go */
static void col_free(struct ticks_set *s)
{
    for (size_t i = 0; i < s->ncol; ++i)
        if (s->cols[i].n >= 0)
        {
            slab_give(s->cols[i].e, (size_t)s->cols[i].cap * sizeof *s->cols[i].e);
            slab_give(s->cols[i].keys, (size_t)s->cols[i].kcap * sizeof *s->cols[i].keys);
        }
    slab_give(s->cols, s->ncol * sizeof *s->cols);
    slab_give(s->order, (size_t)s->caporder * sizeof *s->order);
    s->cols = NULL;
    s->ncol = s->used_col = 0;
    s->order = NULL;
    s->norder = s->caporder = 0;
    s->count = 0;
    s->base = NULL;
    s->bcur = 0;
    ++s->mods;
#ifdef TICKS_VERIFY
    if (s->shadow != NULL)
    {
        col_free(s->shadow);
        slab_give(s->shadow, sizeof *s->shadow);
        s->shadow = NULL;
    }
#endif
}

size_t ticks_set_bytes(const struct ticks_set *s)
{
    size_t b = slab_block_bytes(s->ncol * sizeof *s->cols) + slab_block_bytes((size_t)s->caporder * sizeof *s->order);

    for (size_t i = 0; i < s->ncol; ++i)
        if (s->cols[i].n >= 0)
            b += slab_block_bytes((size_t)s->cols[i].cap * sizeof *s->cols[i].e) +
                 slab_block_bytes((size_t)s->cols[i].kcap * sizeof *s->cols[i].keys);
    return b;
}

void ticks_reset(int64_t entry_id)
{
    col_free(TS);
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

    set_push(s, &c);
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

    set_push(s, &e);
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

    /* the box is in the columns (cx - 1 .. cx, cz - 1 .. cz): each one's
     * entries in the set's order, merged, are the box's sorted (the heapsort
     * of a walk over the whole set gave the same, before() being a total
     * order); a short out gets the smallest max_out. A column's live base
     * entries are a list of their own, read through its indices. */
    const struct tick_entry *a[8];
    const int32_t *ix[8];
    int at[8], m[8], nc = 0;

    for (int i = -1; i <= 0; ++i)
        for (int k = -1; k <= 0; ++k)
        {
            const int64_t key = col_key((cx + i) << 4, (cz + k) << 4);
            const struct ticks_col *c = col_find(s, key);

            if (c != NULL && c->n > 0)
            {
                a[nc] = c->e + c->head;
                ix[nc] = NULL;
                m[nc] = c->n;
                at[nc++] = 0;
            }
            /* the base's own column whole; of a column west or north only
             * its strip the box reaches */
            if ((m[nc] = base_col_live(s, key, (i == -1) + 2 * (k == -1), &ix[nc])) > 0)
            {
                a[nc] = s->base->e;
                at[nc++] = 0;
            }
        }

#define ENT(c, i) (ix[c] != NULL ? &a[c][ix[c][i]] : &a[c][i])
#define IN_BOX(e) ((e)->x >= x0 && (e)->x < x1 && (e)->z >= z0 && (e)->z < z1)
    if (out == NULL || max_out <= 0)
    {
        for (int c = 0; c < nc; ++c)
            for (int i = 0; i < m[c]; ++i) n += IN_BOX(ENT(c, i));
        return n;
    }

    for (;;)
    {
        int best = -1;

        for (int c = 0; c < nc; ++c)
        {
            while (at[c] < m[c] && !IN_BOX(ENT(c, at[c]))) ++at[c];
            if (at[c] < m[c] && (best < 0 || before(ENT(c, at[c]), ENT(best, at[best])))) best = c;
        }
        if (best < 0) break;
        if (n < max_out) out[n] = *ENT(best, at[best]);
        ++n;
        ++at[best];
    }
#undef IN_BOX
#undef ENT

    return n;
}

int ticks_chunk_bound(int cx, int cz)
{
    const struct ticks_set *s = TS;
    int n = 0;

    for (int i = -1; i <= 0; ++i)
        for (int k = -1; k <= 0; ++k)
        {
            const int64_t key = col_key((cx + i) << 4, (cz + k) << 4);
            const struct ticks_col *c = col_find(s, key);
            const int32_t *ix;

            if (c != NULL) n += c->n;
            n += base_col_live(s, key, (i == -1) + 2 * (k == -1), &ix);
        }
    return n;
}

int ticks_set_count(const struct ticks_set *s)
{
    return s->count + base_live(s);
}

int ticks_set_parts(const struct ticks_set *s)
{
    return (int)s->ncol + (s->base != NULL);
}

const struct tick_entry *ticks_set_part(const struct ticks_set *s, int part, int *n)
{
    if ((size_t)part == s->ncol)
    {
        *n = base_live(s);
        return base_first(s);
    }

    const struct ticks_col *c = &s->cols[part];

    *n = c->n > 0 ? c->n : 0;
    return c->n > 0 ? c->e + c->head : NULL;
}

/* the columns' smallest, NULL for none */
static inline const struct tick_entry *col_first(const struct ticks_set *s)
{
    if (s->norder == 0) return NULL;

    const struct ticks_col *c = &s->cols[s->order[0].col];

    return &c->e[c->head];
}

const struct tick_entry *ticks_set_first(const struct ticks_set *s)
{
    const struct tick_entry *m = col_first(s), *b = base_first(s);

    return b == NULL ? m : m == NULL || before(b, m) ? b : m;
}

int ticks_pending_count(void)
{
    return ticks_set_count(TS);
}

/* the set's smallest out: the base's first or the columns' */
static int set_pop(struct ticks_set *s, struct tick_entry *out)
{
    const struct tick_entry *m = col_first(s), *b = base_first(s);

    if (b != NULL && (m == NULL || before(b, m)))
    {
        *out = *b;
        base_take(s, 1);
        return 1;
    }
    if (m == NULL) return 0;
    set_pop_first(s, out);
    return 1;
}

int ticks_pop(struct tick_entry *out)
{
    return set_pop(TS, out);
}

/* WorldServer.tickUpdates checks the first entry's time, then removes that
 * same entry. No scheduling happens between those operations. */
int ticks_pop_due(struct tick_entry *out, int64_t latest)
{
    struct ticks_set *s = TS;
    const struct tick_entry *f = ticks_set_first(s);

    if (f == NULL || f->time > latest) return 0;
    return set_pop(s, out);
}

/* the end of the base's due run from lo below hi: the first index whose
 * entry is past latest or not before m (NULL for none), hi when every one
 * is. The base is in the set's order, so the entries that pass are a prefix:
 * a binary search, where a walk read each of a drain's 1,000 a tick. */
static int base_due_end(const struct tick_entry *e, int lo, int hi, int64_t latest, const struct tick_entry *m)
{
    if (lo < hi && e[hi - 1].time <= latest && (m == NULL || before(&e[hi - 1], m))) return hi;
    while (lo < hi)
    {
        int mid = lo + (hi - lo) / 2;

        if (e[mid].time <= latest && (m == NULL || before(&e[mid], m))) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* ticks_pop_due up to max times into out[], the count: the base's entries
 * a run at a time (every one before the columns' first), copied whole; bix
 * (when not NULL) each one's base index, -1 for a column's */
static int pop_due(struct ticks_set *s, struct tick_entry *out, int32_t *bix, int max, int64_t latest)
{
    int n = 0;

    while (n < max)
    {
        const struct tick_entry *m = col_first(s), *b = base_first(s);

        if (b != NULL && (m == NULL || before(b, m)))
        {
            const struct tick_entry *e = s->base->e;
            const int lim = s->bcur + (max - n < s->base->n - s->bcur ? max - n : s->base->n - s->bcur);
            const int k = base_due_end(e, s->bcur, lim, latest, m);

            if (k == s->bcur) break;
            memcpy(out + n, e + s->bcur, (size_t)(k - s->bcur) * sizeof *out);
            if (bix != NULL)
                for (int j = s->bcur; j < k; ++j) bix[n + j - s->bcur] = j;
            n += k - s->bcur;
            base_take(s, k - s->bcur);
            continue;
        }
        if (m == NULL || s->order[0].time > latest) break;
        if (bix != NULL) bix[n] = -1;
        set_pop_first(s, &out[n++]);
    }
    return n;
}

int ticks_pop_due_n(struct tick_entry *out, int max, int64_t latest)
{
    return pop_due(TS, out, NULL, max, latest);
}

int ticks_pop_due_nb(struct tick_entry *out, int32_t *bix, int max, int64_t latest)
{
    return pop_due(TS, out, bix, max, latest);
}

const struct tick_entry *ticks_pop_due_view(struct tick_entry *out, int32_t *bix, int max, int64_t latest, int *n,
                                            int *b0)
{
    struct ticks_set *s = TS;
    const struct tick_entry *m = col_first(s), *b = base_first(s);

    *b0 = -1;
    if (b != NULL && max > 0 && (m == NULL || before(b, m)))
    {
        /* pop_due's first run, and whether pop_due would stop after it: max
         * reached, or nothing due next (the base's next and the columns'
         * first, whichever comes first) */
        const struct tick_entry *e = s->base->e;
        const int lim = s->bcur + (max < s->base->n - s->bcur ? max : s->base->n - s->bcur);
        const int k = base_due_end(e, s->bcur, lim, latest, m);
        const struct tick_entry *next = k < s->base->n ? &e[k] : NULL;

        if (next == NULL || (m != NULL && before(m, next))) next = m;
        if (k > s->bcur && (k - s->bcur == max || next == NULL || next->time > latest))
        {
            *b0 = s->bcur;
            *n = k - s->bcur;
            base_take(s, k - s->bcur);
            return e + *b0;
        }
    }
    *n = pop_due(s, out, bix, max, latest);
    return out;
}

const struct ticks_base *ticks_base_current(void)
{
    return TS->base;
}

int ticks_first(struct tick_entry *out)
{
    const struct tick_entry *first = ticks_set_first(TS);

    if (first == NULL) return 0;
    *out = *first;
    return 1;
}

/* a peek's candidate: node h of the set's heap, j entries into its column */
struct peek_cand { int h, j; };

static const struct tick_entry *cand_entry(const struct ticks_set *s, struct peek_cand c)
{
    const struct ticks_col *col = &s->cols[s->order[c.h].col];

    return &col->e[col->head + c.j];
}

/* the columns' n smallest in order */
static int col_peek(const struct ticks_set *s, int n, struct tick_entry *out)
{
    int total = s->count;

    if (n <= 0 || total == 0) return 0;
    if (n > total) n = total;

    /* the k smallest in order: a candidate heap seeded with the root's first;
     * a candidate taken pushes its column's next and, for a column's first,
     * the first of the node's two children (the tick peeks one; a check that
     * reads the whole set gets a heap) */
    struct peek_cand small[2 * TICKS_PEEK_MAX + 1];
    const size_t cand_bytes = (size_t)(2 * n + 1) * sizeof(struct peek_cand);
    struct peek_cand *cand = n <= TICKS_PEEK_MAX ? small : slab_take(cand_bytes);
    int ncand = 0, got = 0;

    cand[ncand++] = (struct peek_cand){0, 0};

    while (got < n && ncand > 0)
    {
        struct peek_cand top = cand[0];

        out[got++] = *cand_entry(s, top);
        cand[0] = cand[--ncand];
        for (int i = 0;;)
        {
            int l = 2 * i + 1, r = l + 1, m = i;

            if (l < ncand && before(cand_entry(s, cand[l]), cand_entry(s, cand[m]))) m = l;
            if (r < ncand && before(cand_entry(s, cand[r]), cand_entry(s, cand[m]))) m = r;
            if (m == i) break;

            struct peek_cand t = cand[i];
            cand[i] = cand[m];
            cand[m] = t;
            i = m;
        }

        struct peek_cand next[3];
        int nn = 0;

        if (top.j + 1 < s->cols[s->order[top.h].col].n) next[nn++] = (struct peek_cand){top.h, top.j + 1};
        if (top.j == 0)
            for (int c = 2 * top.h + 1; c <= 2 * top.h + 2 && c < s->norder; ++c) next[nn++] = (struct peek_cand){c, 0};
        for (int k = 0; k < nn; ++k)
        {
            int i = ncand++;
            cand[i] = next[k];

            while (i > 0 && before(cand_entry(s, cand[i]), cand_entry(s, cand[(i - 1) / 2])))
            {
                struct peek_cand t = cand[i];
                cand[i] = cand[(i - 1) / 2];
                cand[(i - 1) / 2] = t;
                i = (i - 1) / 2;
            }
        }
    }

    if (cand != small) slab_give(cand, cand_bytes);
    return got;
}

int ticks_peek(int n, struct tick_entry *out)
{
    struct ticks_set *s = TS;

    if (s->base == NULL) return col_peek(s, n, out);

    int total = ticks_set_count(s);

    if (n <= 0 || total == 0) return 0;
    if (n > total) n = total;

    /* the columns' n smallest merged with the base's live entries */
    struct tick_entry small[TICKS_PEEK_MAX];
    int nm = s->count < n ? s->count : n;
    const size_t bytes = (size_t)(nm ? nm : 1) * sizeof *small;
    struct tick_entry *m = nm <= TICKS_PEEK_MAX ? small : slab_take(bytes);
    const struct tick_entry *b = base_first(s);
    const int nb = base_live(s);
    int i = 0, j = 0, got = 0;

    nm = col_peek(s, nm, m);
    while (got < n)
    {
        if (j < nm && (i >= nb || before(&m[j], &b[i]))) out[got++] = m[j++];
        else out[got++] = b[i++];
    }
    if (m != small) slab_give(m, bytes);
    return got;
}

void ticks_walk(void (*cb)(void *ctx, const struct tick_entry *e), void *ctx)
{
    struct ticks_set *s = TS;

    int total = ticks_set_count(s);
    const size_t copy_bytes = (size_t)(total ? total : 1) * sizeof(struct tick_entry);
    struct tick_entry *copy = slab_take(copy_bytes);

    int at = 0;

    for (int p = 0; p < ticks_set_parts(s); ++p)
    {
        int m;
        const struct tick_entry *e = ticks_set_part(s, p, &m);

        if (m > 0) memcpy(copy + at, e, (size_t)m * sizeof *copy);
        at += m;
    }
    sort_entries(copy, total);

    for (int i = 0; i < total; ++i) cb(ctx, &copy[i]);

    slab_give(copy, copy_bytes);
}

/* checkChunksExist over one cell of a server world: the y test, then its
 * chunk's, the last answer kept while the chunk map holds (the drain's
 * notifications schedule cell after cell of one chunk) */
static int sched_cell_exists(struct ticks_set *s, struct world *w, int x, int y, int z)
{
    if (!(y >= 0 && y < 256)) return 0;

    int cx = x >> 4, cz = z >> 4;

    if (s->ce_w == w && s->ce_ver == w->map_ver + 1 && s->ce_cx == cx && s->ce_cz == cz) return s->ce_ok;
    s->ce_ok = world_check_chunks_exist(w, x, y, z, x, y, z);
    s->ce_w = w;
    s->ce_ver = w->map_ver + 1;
    s->ce_cx = cx;
    s->ce_cz = cz;
    return s->ce_ok;
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

    if (!sched_cell_exists(s, w, x, y, z)) return;

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

    set_push(s, &e);
}

/* 1 when the schedule runs a block update at once: the immediate flag, and a
 * block whose updateTick is ported for it (sched_body's switch) */
static int sched_calls_back(int block)
{
    int b = block & 4095;

    return TS->immediate && b >= 8 && b <= 13;
}

int ticks_reschedule_absent(int n)
{
    if (TS->immediate) return 0;
    NEXT_ID += n;
    return 1;
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
    s->cols = NULL;
    s->ncol = s->used_col = 0;
    s->order = NULL;
    s->norder = s->caporder = s->count = 0;
    s->base = NULL;
    s->bcur = 0;
    ++s->mods;
#ifdef TICKS_VERIFY
    s->shadow = NULL;
#endif
    return b;
}

void ticks_restore(struct ticks_backup *b)
{
    struct ticks_set *s = TS;

    ticks_reset(0);
    *s = b->set;
    NEXT_ID = b->next_entry_id;
    slab_give(b, sizeof *b);
}

void ticks_use(struct ticks_set *set)
{
    neth_env->ticks.cur = set != NULL ? set : &neth_env->ticks.own;
}

void ticks_set_free(struct ticks_set *s)
{
    col_free(s);
    /* the count goes on: a set made again never repeats one with other entries */
    uint32_t mods = s->mods;
    memset(s, 0, sizeof *s);
    s->mods = mods + 1;
}

uint32_t ticks_set_mods(const struct ticks_set *s)
{
    return s->mods;
}

/* ------------------------------------------------------ freezing a start */

static uint32_t pow2_at_least(uint32_t v)
{
    uint32_t n = 16;

    while (n < v) n *= 2;
    return n;
}

static struct ticks_bcol *bcol_slot(struct ticks_bcol *col, uint32_t ncol, int64_t key)
{
    const uint32_t mask = ncol - 1;
    uint32_t i = (uint32_t)(((uint64_t)key * 0x9e3779b97f4a7c15ULL) >> 32) & mask;

    while (col[i].n != 0 && col[i].key != key) i = (i + 1) & mask;
    return &col[i];
}

#ifdef TICKS_VERIFY
/* the verify build: the columns ticks_load_entry filled become the shadow
 * of the set the base holds */
static void columns_frozen(struct ticks_set *s)
{
    struct ticks_set *sh = slab_take(sizeof *sh);

    *sh = *s;
    sh->shadow = NULL;
    s->cols = NULL;
    s->ncol = s->used_col = 0;
    s->order = NULL;
    s->norder = s->caporder = s->count = 0;
    ++s->mods;
    s->shadow = sh;
    __atomic_add_fetch(&tv_frozen, 1, __ATOMIC_RELAXED);
}
#endif

/* a base of e[0..n) (process memory, sorted by before(), one entry a key,
 * the base's own from here) over at most ncol chunk columns */
static struct ticks_base *base_make(struct tick_entry *e, int n, uint32_t ncol)
{
    struct ticks_base *b = proc_calloc(1, sizeof *b);

    b->refs = 1;
    b->e = e;
    b->n = n;
    b->nmem = pow2_at_least(2 * (uint32_t)b->n);
    b->mem = proc_calloc(b->nmem, sizeof *b->mem);
    for (int i = 0; i < b->n; ++i)
    {
        const struct tick_entry *e = &b->e[i];
        uint32_t k = base_hash(e->x, e->y, e->z, e->block) & (b->nmem - 1);

        while (b->mem[k] != 0) k = (k + 1) & (b->nmem - 1);
        b->mem[k] = (uint32_t)i + 1;
    }

    /* the columns: each one's count, its start in ix, then its indices in
     * rising order */
    b->ncol = pow2_at_least(2 * ncol);
    b->col = proc_calloc(b->ncol, sizeof *b->col);
    for (int i = 0; i < b->n; ++i)
    {
        const int64_t key = col_key(b->e[i].x, b->e[i].z);
        struct ticks_bcol *c = bcol_slot(b->col, b->ncol, key);

        c->key = key;
        ++c->n;
    }
    int32_t at = 0;
    int32_t *fill = proc_calloc(b->ncol, sizeof *fill);

    for (uint32_t i = 0; i < b->ncol; ++i)
    {
        b->col[i].at = at;
        at += b->col[i].n;
    }
    b->ix = proc_malloc((size_t)(b->n ? b->n : 1) * sizeof *b->ix);
    for (int i = 0; i < b->n; ++i)
    {
        struct ticks_bcol *c = bcol_slot(b->col, b->ncol, col_key(b->e[i].x, b->e[i].z));

        b->ix[c->at + fill[c - b->col]++] = i;
    }
    proc_free(fill);

    /* each column's strips, in its rising order: east (x & 15 at least 14),
     * south (z & 15 at least 14), the corner (both) */
    int32_t ns = 0;

    for (uint32_t i = 0; i < b->ncol; ++i)
        for (int j = 0; j < b->col[i].n; ++j)
        {
            const struct tick_entry *e = &b->e[b->ix[b->col[i].at + j]];

            ns += ((e->x & 15) >= 14) + ((e->z & 15) >= 14) + ((e->x & 15) >= 14 && (e->z & 15) >= 14);
        }
    b->six = proc_malloc((size_t)(ns ? ns : 1) * sizeof *b->six);
    ns = 0;
    for (uint32_t i = 0; i < b->ncol; ++i)
        for (int p = 0; p < 3; ++p)
        {
            struct ticks_bcol *c = &b->col[i];

            c->part[p][0] = ns;
            for (int j = 0; j < c->n; ++j)
            {
                const struct tick_entry *e = &b->e[b->ix[c->at + j]];
                const int east = (e->x & 15) >= 14, south = (e->z & 15) >= 14;

                if (p == 0 ? east : p == 1 ? south : east && south) b->six[ns++] = b->ix[c->at + j];
            }
            c->part[p][1] = ns - c->part[p][0];
        }
    return b;
}

int ticks_load_all(const struct tick_entry *v, int n)
{
    struct ticks_set *s = TS;
    struct ticks_env *te = &neth_env->ticks;

    if (te->freeze_min <= 0 || n < te->freeze_min || s->base != NULL || s->count != 0 || te->nmade >= 3)
    {
        for (int i = 0; i < n; ++i) ticks_load_entry(&v[i]);
        return 1;
    }

    /* ticks_load_entry's dedupe: the first entry of each (x, y, z, block)
     * in load order, then the set's order; and the columns they reach */
    struct tick_entry *e = proc_malloc((size_t)n * sizeof *e);
    const uint32_t nm = pow2_at_least(2 * (uint32_t)n);
    uint32_t *mem = proc_calloc(nm, sizeof *mem);
    int64_t *cols = proc_malloc((size_t)nm * sizeof *cols);
    uint32_t ncol = 0;
    int m = 0;

    for (uint32_t i = 0; i < nm; ++i) cols[i] = INT64_MIN;
    for (int i = 0; i < n; ++i)
    {
        const struct tick_entry *x = &v[i];
        uint32_t k = base_hash(x->x, x->y, x->z, x->block) & (nm - 1);
        int dup = 0;

        for (; mem[k] != 0; k = (k + 1) & (nm - 1))
        {
            const struct tick_entry *y = &e[mem[k] - 1];

            if (y->x == x->x && y->y == x->y && y->z == x->z && y->block == x->block) { dup = 1; break; }
        }
        if (dup) continue;
        if (x->y < -256 || x->y > 255)
        {
            fprintf(stderr, "ticks: a pending tick at y %d is outside the membership's range\n", x->y);
            abort();
        }
        e[m] = *x;
        mem[k] = (uint32_t)++m;

        const int64_t ck = col_key(x->x, x->z);
        uint32_t c = (uint32_t)(((uint64_t)ck * 0x9e3779b97f4a7c15ULL) >> 32) & (nm - 1);

        while (cols[c] != INT64_MIN && cols[c] != ck) c = (c + 1) & (nm - 1);
        if (cols[c] == INT64_MIN)
        {
            cols[c] = ck;
            ++ncol;
        }
    }
    proc_free(mem);
    proc_free(cols);
#ifdef TICKS_VERIFY
    /* the shadow: the set ticks_load_entry makes */
    for (int i = 0; i < n; ++i) ticks_load_entry(&v[i]);
    columns_frozen(s);
#endif
    sort_entries(e, m);

    const struct ticks_base *b;

    if (te->adopt[te->nmade] != NULL)
    {
        /* a twin's load: the start's base, entry for entry */
        b = te->adopt[te->nmade];
        if (b->n != m || memcmp(b->e, e, (size_t)m * sizeof *e) != 0) te->adopt_bad = 1;
        proc_free(e);
    }
    else
        b = base_make(e, m, ncol);
    te->made[te->nmade++] = (struct ticks_base *)b;
    s->base = te->adopt_bad ? NULL : b;
    s->bcur = 0;
    ++s->mods;
    return !te->adopt_bad;
}

void ticks_base_free(struct ticks_base *b)
{
    if (b == NULL) return;
    proc_free(b->e);
    proc_free(b->mem);
    proc_free(b->col);
    proc_free(b->ix);
    proc_free(b->six);
    proc_free(b->quiet);
    proc_free(b->qrun);
    proc_free(b->qstay);
    proc_free(b);
}

#ifdef TICKS_VERIFY
#undef ticks_chunk_updates
#undef ticks_chunk_bound
#undef ticks_set_count
#undef ticks_set_first
#undef ticks_pop
#undef ticks_pop_due
#undef ticks_pop_due_n
#undef ticks_pop_due_nb
#undef ticks_pop_due_view
#undef ticks_peek

/* the call on the shadow: the current set swapped for it around the call */
#define ON_SHADOW(s, call)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        neth_env->ticks.cur = (s)->shadow;                                                                               \
        call;                                                                                                          \
        neth_env->ticks.cur = (s);                                                                                       \
    } while (0)

static int tv_same(const struct tick_entry *a, const struct tick_entry *b, int n)
{
    return n <= 0 || memcmp(a, b, (size_t)n * sizeof *a) == 0;
}

int ticks_set_count(const struct ticks_set *s)
{
    int n = ticks_set_count_one(s);

    if (s->shadow != NULL) tv_check(n == ticks_set_count_one(s->shadow), "count");
    return n;
}

const struct tick_entry *ticks_set_first(const struct ticks_set *s)
{
    const struct tick_entry *f = ticks_set_first_one(s);

    if (s->shadow != NULL)
    {
        const struct tick_entry *g = ticks_set_first_one(s->shadow);

        tv_check((f == NULL) == (g == NULL) && (f == NULL || tv_same(f, g, 1)), "first");
    }
    return f;
}

int ticks_chunk_updates(int cx, int cz, struct tick_entry *out, int max_out)
{
    struct ticks_set *s = TS;
    int n = ticks_chunk_updates_one(cx, cz, out, max_out);

    if (s->shadow != NULL)
    {
        int m = max_out > 0 && out != NULL ? max_out : 0;
        struct tick_entry *o2 = m > 0 ? slab_take((size_t)m * sizeof *o2) : NULL;
        int n2;

        ON_SHADOW(s, n2 = ticks_chunk_updates_one(cx, cz, o2, m));
        tv_check(n == n2 && tv_same(out, o2, n < m ? n : m), "chunk updates");
        if (o2 != NULL) slab_give(o2, (size_t)m * sizeof *o2);
    }
    return n;
}

int ticks_chunk_bound(int cx, int cz)
{
    struct ticks_set *s = TS;
    int n = ticks_chunk_bound_one(cx, cz);

    if (s->shadow != NULL)
    {
        int n2;

        /* a bound: at least the box's count (the base's strips make it
         * tighter than the shadow's whole columns) */
        ON_SHADOW(s, n2 = ticks_chunk_updates_one(cx, cz, NULL, 0));
        tv_check(n >= n2, "chunk bound");
    }
    return n;
}

int ticks_peek(int n, struct tick_entry *out)
{
    struct ticks_set *s = TS;
    int got = ticks_peek_one(n, out);

    if (s->shadow != NULL && n > 0)
    {
        struct tick_entry *o2 = slab_take((size_t)n * sizeof *o2);
        int got2;

        ON_SHADOW(s, got2 = ticks_peek_one(n, o2));
        tv_check(got == got2 && tv_same(out, o2, got), "peek");
        slab_give(o2, (size_t)n * sizeof *o2);
    }
    return got;
}

int ticks_pop(struct tick_entry *out)
{
    struct ticks_set *s = TS;
    int r = ticks_pop_one(out);

    if (s->shadow != NULL)
    {
        struct tick_entry o2;
        int r2;

        ON_SHADOW(s, r2 = ticks_pop_one(&o2));
        tv_check(r == r2 && (!r || tv_same(out, &o2, 1)), "pop");
    }
    return r;
}

int ticks_pop_due(struct tick_entry *out, int64_t latest)
{
    struct ticks_set *s = TS;
    int r = ticks_pop_due_one(out, latest);

    if (s->shadow != NULL)
    {
        struct tick_entry o2;
        int r2;

        ON_SHADOW(s, r2 = ticks_pop_due_one(&o2, latest));
        tv_check(r == r2 && (!r || tv_same(out, &o2, 1)), "pop due");
    }
    return r;
}

int ticks_pop_due_n(struct tick_entry *out, int max, int64_t latest)
{
    struct ticks_set *s = TS;
    int n = ticks_pop_due_n_one(out, max, latest);

    if (s->shadow != NULL && max > 0)
    {
        struct tick_entry *o2 = slab_take((size_t)max * sizeof *o2);
        int n2;

        ON_SHADOW(s, n2 = ticks_pop_due_n_one(o2, max, latest));
        tv_check(n == n2 && tv_same(out, o2, n), "pop due n");
        slab_give(o2, (size_t)max * sizeof *o2);
    }
    return n;
}

int ticks_pop_due_nb(struct tick_entry *out, int32_t *bix, int max, int64_t latest)
{
    struct ticks_set *s = TS;
    const struct ticks_base *b = s->base;
    int n = ticks_pop_due_nb_one(out, bix, max, latest);

    for (int i = 0; i < n; ++i)
        if (bix[i] >= 0) tv_check(b != NULL && bix[i] < b->n && tv_same(&b->e[bix[i]], &out[i], 1), "base index");
    if (s->shadow != NULL && max > 0)
    {
        struct tick_entry *o2 = slab_take((size_t)max * sizeof *o2);
        int n2;

        ON_SHADOW(s, n2 = ticks_pop_due_n_one(o2, max, latest));
        tv_check(n == n2 && tv_same(out, o2, n), "pop due nb");
        slab_give(o2, (size_t)max * sizeof *o2);
    }
    return n;
}

const struct tick_entry *ticks_pop_due_view(struct tick_entry *out, int32_t *bix, int max, int64_t latest, int *n,
                                            int *b0)
{
    struct ticks_set *s = TS;
    const struct ticks_base *b = s->base;
    const struct tick_entry *r = ticks_pop_due_view_one(out, bix, max, latest, n, b0);

    if (*b0 >= 0) tv_check(b != NULL && r == b->e + *b0 && *b0 + *n <= b->n, "a view's run");
    else
        for (int i = 0; i < *n; ++i)
            if (bix[i] >= 0) tv_check(b != NULL && bix[i] < b->n && tv_same(&b->e[bix[i]], &out[i], 1), "base index");
    if (s->shadow != NULL && max > 0)
    {
        struct tick_entry *o2 = slab_take((size_t)max * sizeof *o2);
        int n2;

        ON_SHADOW(s, n2 = ticks_pop_due_n_one(o2, max, latest));
        tv_check(*n == n2 && tv_same(r, o2, *n), "pop due view");
        slab_give(o2, (size_t)max * sizeof *o2);
    }
    return r;
}
#endif
