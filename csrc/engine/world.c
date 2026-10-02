/* The native world core: World.setBlock and everything vanilla 1.7.10 does
 * under it, ported from oracle/src/world/World.java and
 * oracle/src/world/chunk/Chunk.java:
 *
 *   Chunk.func_150807_a   sections made on demand, height maps, the column
 *                         relight (relightBlock -> markBlocksDirtyVertical and
 *                         updateSkylightNeighborHeight), the occlusion flag
 *   World.func_147451_t   the sky and block light passes
 *   updateLightByType     the queued light engine over World's fixed
 *                         32768-entry list, with packed coordinates
 *   the 17-block guard    only runs the light pass when every chunk within 17
 *                         blocks is loaded, so the loaded set is world state
 *
 * Block callbacks: func_150807_a runs the replaced block's breakBlock, the
 * tile entity work (the old block's entity, then the new block's
 * onBlockAdded, then the new block's entity) and World.setBlock then
 * reports the write, runs the light pass and, for a flag-1 write, notifies
 * the six neighbors. breakBlock is ported for BlockLeaves and BlockLog
 * (block_break); onBlockAdded and onNeighborBlockChange live in blockcb.c,
 * except the chest's onBlockAdded (its facing), which is here next to the
 * tile entity store it pairs with (tileentity.c). Flag 2's client mark and
 * flag 4's render half move nothing. */
#include "comparator.h"
#include "jmath.h"
#include "world.h"
#include "arena.h"
#include "env.h"
#include "stronghold.h"
#include "blockcb.h"
#include "blockwl.h"
#include "blocks.h"
#include "end.h"
#include "features_lakes.h"
#include "jrand.h"
#include "light.h"
#include "lightcap.h"
#include "lightdefer.h"
#include "lzpack.h"
#include "place.h"
#include "nether.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the client world's deferred light run first (lightdefer.h; defined with the light engine) */
static void cw_sync(struct world *w, int kind);

/* The four ids whose block class overrides breakBlock among everything a
 * feature can replace: BlockLeaves (18) and BlockNewLeaf (161) share
 * BlockLeaves.breakBlock, BlockLog (17) and BlockNewLog (162) share
 * BlockLog.breakBlock. Every other block a feature writes over (air, grass,
 * dirt, stone, sand, gravel, water, vine, ...) inherits Block's empty bodies. */
enum { BLK_LEAVES_ID = 18, BLK_LOG_ID = 17, BLK_LEAVES2_ID = 161, BLK_LOG2_ID = 162 };

/* Facing.offsetsXForSide / offsetsYForSide / offsetsZForSide. */
static const int FACE_X[6] = {0, 0, 0, 0, -1, 1};
static const int FACE_Y[6] = {-1, 1, 0, 0, 0, 0};
static const int FACE_Z[6] = {0, 0, -1, 1, 0, 0};

#define XZ_IN_RANGE(v) ((v) >= -30000000 && (v) < 30000000)

/* Chunk's two local accessors, both air / 0 for a cell in a section that does
 * not exist. */
static inline int section_present(const struct chunk *c, int y)
{
    return (c->mask >> (y >> 4)) & 1;
}

static inline int chunk_block(const struct chunk *c, int x, int y, int z)
{
    return chunk_xyz_id(c, x, y, z);
}

static int chunk_meta(const struct chunk *c, int x, int y, int z)
{
    return chunk_xyz_meta(c, x, y, z);
}

/* ------------------------------------------------------------- cell storage */

static void *chunk_alloc(size_t n, int zero)
{
    void *p = zero ? calloc(1, n) : malloc(n);

    if (p == NULL)
    {
        fprintf(stderr, "chunk: out of memory\n");
        abort();
    }
    return p;
}

/* The constant nibble array v: slot v of the environment's nibble pool
 * (arena.c makes the sixteen with the pool). */
static inline const uint8_t *nib_const(int v)
{
    return nw_arena()->nibs.base + (size_t)v * SEC_NIB_BYTES;
}

static inline void sec_nib_point(struct chunk_sec *s, int k, const uint8_t *a)
{
    s->nib[k] = (int32_t)(a - (const uint8_t *)s);
}

/* array k held as the constant v (an own slot given back) */
static void sec_nib_const(struct chunk_sec *s, int k, int v)
{
    if (s->nib_own >> k & 1) pool_give_keep(&nw_arena()->nibs, (uint8_t *)s + s->nib[k]);
    s->nib_own = (uint8_t)(s->nib_own & ~(1u << k));
    sec_nib_point(s, k, nib_const(v));
}

/* array k made the band's own slot, its bytes not yet written */
static uint8_t *sec_nib_take(struct chunk_sec *s, int k)
{
    uint8_t *a = pool_take_raw(&nw_arena()->nibs);

    sec_nib_point(s, k, a);
    s->nib_own = (uint8_t)(s->nib_own | 1u << k);
    return a;
}

int nib_uniform(const uint8_t *a)
{
    uint64_t want = (uint64_t)(a[0] & 15) * 0x1111111111111111ULL;

    for (int i = 0; i < SEC_NIB_BYTES; i += 256)
    {
        uint64_t acc = 0;

        for (int j = 0; j < 256; j += 8)
        {
            uint64_t w;
            memcpy(&w, a + i + j, sizeof w);
            acc |= w ^ want;
        }
        if (acc) return -1;
    }
    return a[0] & 15;
}

void chunk_sec_nib_expand(struct chunk_sec *s, int k)
{
    const uint8_t *was = chunk_sec_nib(s, k);

    memcpy(sec_nib_take(s, k), was, SEC_NIB_BYTES);
}

void chunk_sec_nib_store(struct chunk_sec *s, int k, const uint8_t *a)
{
    int v = nib_uniform(a);

    if (v >= 0)
    {
        sec_nib_const(s, k, v);
        return;
    }
    uint8_t *to = s->nib_own >> k & 1 ? (uint8_t *)s + s->nib[k] : sec_nib_take(s, k);
    if (to != a) memcpy(to, a, SEC_NIB_BYTES);
}

void chunk_sec_nib_compact(struct chunk_sec *s)
{
    for (int k = 0; k < NIB_N; ++k)
        if (s->nib_own >> k & 1)
        {
            int v = nib_uniform((const uint8_t *)s + s->nib[k]);
            if (v >= 0) sec_nib_const(s, k, v);
        }
}

/* A band's storage: a slot of the environment's band pool (arena.h), zeroed
 * with its nibble arrays the constant 0 (or not, for a caller that writes
 * all of it); a dropped chunk's bands go back to it. */
static struct chunk_sec *sec_take(int zero)
{
    if (!zero) return pool_take_raw(&nw_arena()->bands);

    struct chunk_sec *s = pool_take(&nw_arena()->bands);

    for (int k = 0; k < NIB_N; ++k) sec_nib_point(s, k, nib_const(0));
    return s;
}

/* to (a raw slot) made a copy of from, unshared, with its own copies of
 * from's own arrays and the same constants */
static void sec_copy(struct chunk_sec *to, const struct chunk_sec *from)
{
    memcpy(to, from, sizeof *to);
    to->shared = 0;
    to->ids_hi = NULL;
    if (from->ids_hi != NULL)
    {
        to->ids_hi = chunk_alloc(SEC_CELLS / 2, 0);
        memcpy(to->ids_hi, from->ids_hi, SEC_CELLS / 2);
    }
    to->nib_own = 0;
    for (int k = 0; k < NIB_N; ++k)
        if (from->nib_own >> k & 1) memcpy(sec_nib_take(to, k), chunk_sec_nib(from, k), SEC_NIB_BYTES);
        else sec_nib_point(to, k, chunk_sec_nib(from, k));
}

void chunk_sec_flatten(const struct chunk_sec *s, struct chunk_sec_flat *out)
{
    memcpy(&out->h, s, sizeof out->h);
    out->h.nib_own = 0;
    for (int k = 0; k < NIB_N; ++k) out->h.nib[k] = SEC_FLAT_NIB(k);
    memcpy(out->metas, chunk_sec_metas(s), SEC_NIB_BYTES);
    memcpy(out->sky, chunk_sec_sky(s), SEC_NIB_BYTES);
    memcpy(out->blocklight, chunk_sec_blocklight(s), SEC_NIB_BYTES);
}

size_t chunk_sec_bytes(const struct chunk_sec *s)
{
    return sizeof *s + (size_t)__builtin_popcount(s->nib_own) * SEC_NIB_BYTES + (s->ids_hi ? SEC_CELLS / 2 : 0);
}

struct chunk_sec *chunk_sec_own(struct chunk *c, int s)
{
    struct chunk_sec *old = chunk_sec_at(c, s), *own = sec_take(0);
    uint64_t rtick = c->rtick_mask[s];

    sec_copy(own, old);
    --old->shared;
    chunk_sec_put(c, s, own);
    /* the copy holds the band's cells: the random tick's filter of them
     * stands (a filter made again reads the whole band; lane/coldaudit) */
    c->rtick_mask[s] = rtick;
    return own;
}

struct chunk_sec *chunk_sec_make(struct chunk *c, int s)
{
    struct chunk_sec *sec = chunk_sec_at(c, s);

    if (sec == NULL) chunk_sec_put(c, s, sec = sec_take(1));
    else if (sec->shared != 0) sec = chunk_sec_own(c, s);
    /* its caller stores the band's cells */
    c->rtick_mask[s] = 0;
    return sec;
}

struct chunk_sec *chunk_sec_hold(const struct chunk *c, int s)
{
    struct chunk_sec *sec = chunk_sec_at(c, s);
    if (sec != NULL) ++sec->shared;
    return sec;
}

/* The first cell (in the NBT order y << 8 | z << 4 | x) whose nibble in a
 * is not 0, or SEC_CELLS */
static int nbt_nib_first(const uint8_t *a)
{
    for (int b = 0; b < SEC_CELLS / 2; ++b)
        if (a[b]) return 2 * b + ((a[b] & 15) == 0);
    return SEC_CELLS;
}

/* NBT nibble array a (index y << 8 | z << 4 | x) into the band's order (x << 8
 * | z << 4 | y) at d */
static void nbt_nib_transpose(uint8_t *d, const uint8_t *a)
{
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 16; y += 2)
            {
                int v0 = z << 4 | x, lo = (a[(y << 8 | v0) >> 1] >> ((x & 1) << 2)) & 15;
                int hi = (a[((y + 1) << 8 | v0) >> 1] >> ((x & 1) << 2)) & 15;
                d[(x << 8 | z << 4 | y) >> 1] = (uint8_t)(lo | hi << 4);
            }
}

int chunk_sec_load(struct chunk *c, int s, const uint8_t *blocks, const uint8_t *add, const uint8_t *metas,
                   const uint8_t *sky, const uint8_t *blocklight)
{
    if (chunk_sec_at(c, s) != NULL) return 0;
    const uint8_t *nib[NIB_N] = {metas, sky, blocklight};
    int first[NIB_N], id_first = SEC_CELLS, hi_first = SEC_CELLS;
    for (int k = 0; k < NIB_N; ++k) first[k] = nib[k] ? nbt_nib_first(nib[k]) : SEC_CELLS;
    for (int v = 0; v < SEC_CELLS && id_first == SEC_CELLS; ++v)
        if (blocks[v]) id_first = v;
    if (add) hi_first = nbt_nib_first(add);
    if (hi_first < id_first) id_first = hi_first;
    if (id_first == SEC_CELLS && first[0] == SEC_CELLS && first[1] == SEC_CELLS && first[2] == SEC_CELLS) return 1;
    /* the per-cell stores took the band at its first value not 0, then each
     * nibble array its own slot at its first value not 0 (in cell order,
     * metadata, sky, block light within a cell), the high ids at the first
     * id past 255: the same order here */
    struct chunk_sec *sec = chunk_sec_make(c, s);
    for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) sec->ids[x << 8 | z << 4 | y] = blocks[y << 8 | z << 4 | x];
    int order[NIB_N] = {0, 1, 2};
    for (int i = 1; i < NIB_N; ++i)
        for (int j = i; j > 0 && first[order[j]] < first[order[j - 1]]; --j)
        {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    for (int i = 0; i < NIB_N; ++i)
    {
        int k = order[i];
        if (first[k] == SEC_CELLS) continue;
        nbt_nib_transpose(sec_nib_take(sec, k), nib[k]);
    }
    if (hi_first < SEC_CELLS)
    {
        sec->ids_hi = chunk_alloc(SEC_CELLS / 2, 0);
        nbt_nib_transpose(sec->ids_hi, add);
    }
    return 1;
}

static void chunk_sec_free(struct chunk_sec *s);

void chunk_sec_drop(struct chunk_sec *sec)
{
    chunk_sec_free(sec);
}

void chunk_sec_install(struct chunk *c, int s, struct chunk_sec *sec)
{
    if (chunk_sec_at(c, s) == sec)
    {
        chunk_sec_free(sec);   /* already there: the hold goes */
        return;
    }
    chunk_sec_free(chunk_sec_at(c, s));
    chunk_sec_put(c, s, sec);
}

void chunk_value_range(const char *what, int v)
{
    fprintf(stderr, "chunk: %s %d does not fit the band\n", what, v);
    abort();
}

void chunk_sec_set_id_hi(struct chunk_sec *s, int i, int v)
{
    if (s->ids_hi == NULL)
    {
        if (v == 0) return;
        s->ids_hi = chunk_alloc(SEC_CELLS / 2, 1);
    }
    nibble_set(s->ids_hi, i, v);
}

static void chunk_sec_free(struct chunk_sec *s)
{
    if (s == NULL) return;
    if (s->shared != 0)
    {
        --s->shared;      /* another chunk still holds it */
        return;
    }
    free(s->ids_hi);
    for (int k = 0; k < NIB_N; ++k)
        if (s->nib_own >> k & 1) pool_give_keep(&nw_arena()->nibs, (uint8_t *)s + s->nib[k]);
    pool_give_keep(&nw_arena()->bands, s);
}

void chunk_sec_column_out(const struct chunk_sec *s, int col, uint16_t *ids, uint8_t *metas, uint8_t *sky,
                          uint8_t *blocklight)
{
    int o = col << 4;

    if (s == NULL)
    {
        if (ids) memset(ids, 0, 16 * sizeof *ids);
        if (metas) memset(metas, 0, 16);
        if (sky) memset(sky, 0, 16);
        if (blocklight) memset(blocklight, 0, 16);
        return;
    }
    if (ids)
        for (int y = 0; y < 16; ++y) ids[y] = (uint16_t)chunk_sec_id(s, o + y);
    /* a column's 16 nibbles are the 8 bytes from o >> 1 */
    const uint8_t *sm = chunk_sec_metas(s), *ss = chunk_sec_sky(s), *sl = chunk_sec_blocklight(s);
    for (int k = 0; k < 8; ++k)
    {
        int b = (o >> 1) + k;
        if (metas) { metas[2 * k] = sm[b] & 15; metas[2 * k + 1] = sm[b] >> 4; }
        if (sky) { sky[2 * k] = ss[b] & 15; sky[2 * k + 1] = ss[b] >> 4; }
        if (blocklight) { blocklight[2 * k] = sl[b] & 15; blocklight[2 * k + 1] = sl[b] >> 4; }
    }
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
/* 16 cells of one nibble field, a byte each, into 8 bytes of nibbles (low
 * nibble first); 0 when a cell is over 15 (the caller's slow path names it) */
static inline int nibbles_pack16(const uint8_t *in, uint8_t *out)
{
    uint64_t a, b;
    memcpy(&a, in, 8);
    memcpy(&b, in + 8, 8);
    if ((a | b) & 0xf0f0f0f0f0f0f0f0ULL) return 0;
    /* byte 2k of x | x >> 4 is cell 2k | cell 2k+1 << 4; the even bytes
     * gathered are the packed column */
    a = (a | a >> 4) & 0x00ff00ff00ff00ffULL;
    b = (b | b >> 4) & 0x00ff00ff00ff00ffULL;
    a = (a | a >> 8) & 0x0000ffff0000ffffULL;
    b = (b | b >> 8) & 0x0000ffff0000ffffULL;
    a = (a | a >> 16) & 0xffffffffULL;
    b = (b | b >> 16) & 0xffffffffULL;
    a |= b << 32;
    memcpy(out, &a, 8);
    return 1;
}
#endif

/* a column's 8 bytes of array k from b: a constant array takes its own slot
 * only for bytes that differ from it */
static inline void sec_nib_put8(struct chunk_sec *s, int k, int b, const uint8_t *src)
{
    if (!(s->nib_own >> k & 1))
    {
        if (memcmp(chunk_sec_nib(s, k) + b, src, 8) == 0) return;
        chunk_sec_nib_expand(s, k);
    }
    memcpy((uint8_t *)s + s->nib[k] + b, src, 8);
}

void chunk_sec_column_in(struct chunk_sec *s, int col, const uint16_t *ids, const uint8_t *metas,
                         const uint8_t *sky, const uint8_t *blocklight)
{
    int o = col << 4;

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    /* the common column: every id under 256 in a band without high ids,
     * every nibble in range; the loops below take the rest, the same way */
    if (ids && metas && sky && blocklight && s->ids_hi == NULL)
    {
        uint64_t w[4];
        memcpy(w, ids, 32);
        if (((w[0] | w[1] | w[2] | w[3]) & 0xff00ff00ff00ff00ULL) == 0)
        {
            uint8_t m[8], k[8], l[8];
            if (nibbles_pack16(metas, m) && nibbles_pack16(sky, k) && nibbles_pack16(blocklight, l))
            {
                for (int y = 0; y < 16; ++y) s->ids[o + y] = (uint8_t)ids[y];
                sec_nib_put8(s, NIB_METAS, o >> 1, m);
                sec_nib_put8(s, NIB_SKY, o >> 1, k);
                sec_nib_put8(s, NIB_BLOCK, o >> 1, l);
                return;
            }
        }
    }
#endif

    if (ids)
        for (int y = 0; y < 16; ++y)
        {
            if (ids[y] > 4095) chunk_value_range("id", ids[y]);
            s->ids[o + y] = (uint8_t)ids[y];
            if (ids[y] > 255 || s->ids_hi) chunk_sec_set_id_hi(s, o + y, ids[y] >> 8);
        }
    for (int k = 0; k < 16; ++k)
    {
        if (metas && metas[k] > 15) chunk_value_range("metas", metas[k]);
        if (sky && sky[k] > 15) chunk_value_range("sky", sky[k]);
        if (blocklight && blocklight[k] > 15) chunk_value_range("blocklight", blocklight[k]);
    }
    const uint8_t *in[NIB_N] = {metas, sky, blocklight};
    for (int f = 0; f < NIB_N; ++f)
    {
        if (in[f] == NULL) continue;
        uint8_t b8[8];
        for (int k = 0; k < 8; ++k) b8[k] = (uint8_t)(in[f][2 * k] | in[f][2 * k + 1] << 4);
        sec_nib_put8(s, f, o >> 1, b8);
    }
}

/* A chunk's struct: a slot of the environment's chunk pool (arena.h),
 * zeroed; chunk_free gives it back. */
struct chunk *chunk_new(void)
{
    return pool_take(&nw_arena()->chunks);
}

static void chunk_drop_storage(struct chunk *c)
{
    for (int s = 0; s < 16; ++s)
    {
        chunk_sec_free(chunk_sec_at(c, s));
        c->band[s] = 0;
    }
}

void chunk_free(struct chunk *c)
{
    if (c == NULL) return;
    chunk_drop_storage(c);
    free(c->packed);
    te_store_free(&c->tes);
    pool_give(&nw_arena()->chunks, c);
}

void chunk_clear_cells(struct chunk *c)
{
    chunk_drop_storage(c);
    free(c->packed);
    c->packed = NULL;
    c->packed_n = 0;
}

void chunk_clear(struct chunk *c)
{
    chunk_drop_storage(c);
    free(c->packed);
    memset(c, 0, sizeof *c);
}

void chunk_copy(struct chunk *dst, const struct chunk *src)
{
    if (dst == src) return;
    chunk_drop_storage(dst);
    free(dst->packed);
    memcpy(dst, src, sizeof *dst);
    if (src->packed != NULL)
    {
        dst->packed = chunk_alloc(src->packed_n, 0);
        memcpy(dst->packed, src->packed, src->packed_n);
    }
    for (int s = 0; s < 16; ++s)
    {
        const struct chunk_sec *from = chunk_sec_at(src, s);
        if (from == NULL) continue;
        struct chunk_sec *to = sec_take(0);
        chunk_sec_put(dst, s, to);
        sec_copy(to, from);
    }
}

void chunk_share(struct chunk *dst, const struct chunk *src)
{
    if (dst == src) return;
    chunk_drop_storage(dst);
    free(dst->packed);
    memcpy(dst, src, sizeof *dst);
    if (src->packed != NULL)
    {
        dst->packed = chunk_alloc(src->packed_n, 0);
        memcpy(dst->packed, src->packed, src->packed_n);
    }
    for (int s = 0; s < 16; ++s)
    {
        struct chunk_sec *sec = chunk_sec_at(src, s);
        chunk_sec_put(dst, s, sec);
        if (sec != NULL) ++sec->shared;
    }
}

void chunk_share_band(struct chunk *dst, const struct chunk *src, int s)
{
    struct chunk_sec *sec = chunk_sec_at(src, s);

    if (chunk_sec_at(dst, s) == sec) return;
    chunk_sec_free(chunk_sec_at(dst, s));
    chunk_sec_put(dst, s, sec);
    if (sec != NULL) ++sec->shared;
}

void chunk_cells_in(struct chunk *c, const uint16_t *ids, const uint8_t *metas, const uint8_t *sky,
                    const uint8_t *blocklight)
{
    for (int s = 0; s < 16; ++s)
    {
        /* a band gets storage when any given field holds a nonzero cell in
         * it, or when it has storage already (whose cells are overwritten) */
        int any = c->band[s] != 0;

        for (int col = 0; col < 256 && !any; ++col)
        {
            int base = col << 8 | s << 4;
            uint64_t v = 0, w[4];

            if (ids)
            {
                memcpy(w, ids + base, 32);
                v |= w[0] | w[1] | w[2] | w[3];
            }
            if (metas)
            {
                memcpy(w, metas + base, 16);
                v |= w[0] | w[1];
            }
            if (sky)
            {
                memcpy(w, sky + base, 16);
                v |= w[0] | w[1];
            }
            if (blocklight)
            {
                memcpy(w, blocklight + base, 16);
                v |= w[0] | w[1];
            }
            any = v != 0;
        }
        if (!any) continue;

        struct chunk_sec *sec = chunk_sec_make(c, s);

        for (int col = 0; col < 256; ++col)
        {
            int base = col << 8 | s << 4;

            chunk_sec_column_in(sec, col, ids ? ids + base : NULL, metas ? metas + base : NULL,
                                sky ? sky + base : NULL, blocklight ? blocklight + base : NULL);
        }
        chunk_sec_nib_compact(sec);
    }
}

/* A band's nibble array as a byte per cell into the flat layout. */
static void nibbles_out(const uint8_t *nb, int s, uint8_t *flat)
{
    for (int col = 0; col < 256; ++col)
    {
        const uint8_t *p = nb + (col << 3);
        uint8_t *q = flat + (col << 8 | s << 4);

        for (int k = 0; k < 8; ++k)
        {
            q[2 * k] = p[k] & 15;
            q[2 * k + 1] = p[k] >> 4;
        }
    }
}

void chunk_cells_out(const struct chunk *c, uint16_t *ids, uint8_t *metas, uint8_t *sky, uint8_t *blocklight)
{
    for (int s = 0; s < 16; ++s)
    {
        const struct chunk_sec *sec = chunk_sec_at(c, s);

        if (sec == NULL)
        {
            for (int col = 0; col < 256; ++col)
            {
                int base = col << 8 | s << 4;

                if (ids) memset(ids + base, 0, 32);
                if (metas) memset(metas + base, 0, 16);
                if (sky) memset(sky + base, 0, 16);
                if (blocklight) memset(blocklight + base, 0, 16);
            }
            continue;
        }
        if (ids)
            for (int col = 0; col < 256; ++col)
            {
                uint16_t *q = ids + (col << 8 | s << 4);
                const uint8_t *p = sec->ids + (col << 4);

                if (sec->ids_hi == NULL)
                    for (int y = 0; y < 16; ++y) q[y] = p[y];
                else
                    for (int y = 0; y < 16; ++y) q[y] = (uint16_t)chunk_sec_id(sec, (col << 4) + y);
            }
        if (metas) nibbles_out(chunk_sec_metas(sec), s, metas);
        if (sky) nibbles_out(chunk_sec_sky(sec), s, sky);
        if (blocklight) nibbles_out(chunk_sec_blocklight(sec), s, blocklight);
    }
}

/* The blob: the band mask (two bytes), then per band a byte of flags (bit
 * 0, 1, 2: its metadata, sky light or block light is all zero and left out;
 * bit 3: the id's high nibbles follow), its ids, the arrays not left out and
 * the high nibbles, compressed by lzpack.h's codec (it replaced raw deflate
 * at level 1, five times the instructions to pack and 2.6 times to unpack:
 * lane/spillfast). Most light and metadata arrays are zero (four in five in
 * a Nether run; the Nether's sky always): left out, they cost neither a
 * copy nor the codec's time. One scratch buffer holds the raw form (with
 * the decoder's slack), the compressed form and the codec's table. */
#define SEC_PACKED (1 + SEC_CELLS + 4 * (SEC_CELLS / 2))
#define pack_scratch (nw_env->world.pack_scratch)
#define PACK_RAW (2 + 16 * SEC_PACKED)
#define PACK_OUT ((PACK_RAW + LZP_SLACK + 7) & ~7)
#define PACK_TAB ((PACK_OUT + LZP_BOUND(PACK_RAW) + 7) & ~7)
#define PACK_SCRATCH (PACK_TAB + LZP_TABLE_BYTES)

static int nibbles_zero(const uint8_t *a)
{
    uint64_t acc = 0;

    for (int i = 0; i < SEC_CELLS / 2; i += 8)
    {
        uint64_t v;
        memcpy(&v, a + i, sizeof v);
        acc |= v;
    }
    return acc == 0;
}

void chunk_pack(struct chunk *c)
{
    if (c->packed != NULL) return;

    uint16_t have = 0;

    for (int s = 0; s < 16; ++s)
        if (c->band[s]) have |= (uint16_t)(1 << s);
    if (have == 0) return;
    if (pack_scratch == NULL) pack_scratch = chunk_alloc(PACK_SCRATCH, 0);

    pack_scratch[0] = (uint8_t)have;
    pack_scratch[1] = (uint8_t)(have >> 8);
    size_t o = 2;
    for (int s = 0; s < 16; ++s)
    {
        const struct chunk_sec *sec = chunk_sec_at(c, s);
        if (sec == NULL) continue;
        const uint8_t *nb[NIB_N] = {chunk_sec_metas(sec), chunk_sec_sky(sec), chunk_sec_blocklight(sec)};
        uint8_t fl = (uint8_t)(nibbles_zero(nb[0]) | nibbles_zero(nb[1]) << 1 | nibbles_zero(nb[2]) << 2 |
                               (sec->ids_hi != NULL) << 3);
        pack_scratch[o++] = fl;
        memcpy(pack_scratch + o, sec->ids, SEC_CELLS); o += SEC_CELLS;
        for (int k = 0; k < NIB_N; ++k)
            if (!(fl >> k & 1)) { memcpy(pack_scratch + o, nb[k], SEC_CELLS / 2); o += SEC_CELLS / 2; }
        if (fl & 8) { memcpy(pack_scratch + o, sec->ids_hi, SEC_CELLS / 2); o += SEC_CELLS / 2; }
    }

    uint32_t n = lzp_compress(pack_scratch, (uint32_t)o, pack_scratch + PACK_OUT, (uint32_t *)(void *)(pack_scratch + PACK_TAB));
    c->packed = chunk_alloc(n, 0);
    memcpy(c->packed, pack_scratch + PACK_OUT, n);
    c->packed_n = n;
    chunk_drop_storage(c);
}

void chunk_unpack(struct chunk *c)
{
    if (c->packed == NULL) return;
    chunk_unpack_blob(c, c->packed, c->packed_n);
    free(c->packed);
    c->packed = NULL;
    c->packed_n = 0;
}

void chunk_unpack_blob(struct chunk *c, const uint8_t *blob, uint32_t n)
{
    if (pack_scratch == NULL) pack_scratch = chunk_alloc(PACK_SCRATCH, 0);

    int64_t got = lzp_decompress(blob, n, pack_scratch, PACK_RAW);
    if (got < 2)
    {
        fprintf(stderr, "chunk: a packed chunk (%d,%d) does not decompress\n", c->cx, c->cz);
        abort();
    }

    uint16_t have = (uint16_t)(pack_scratch[0] | pack_scratch[1] << 8);
    size_t o = 2;
    /* the cells come back as they were packed: so does the random tick's
     * filter of them (chunk_pack leaves it; a band put since reset it) */
    uint64_t rtick[16];
    memcpy(rtick, c->rtick_mask, sizeof rtick);
    for (int s = 0; s < 16; ++s)
    {
        if (!(have & (1 << s))) continue;
        /* a packed chunk has no bands: each is made here, every byte written
         * (the zeroed take would write it twice) */
        struct chunk_sec *sec = chunk_sec_at(c, s);
        if (sec == NULL)
        {
            chunk_sec_put(c, s, sec = sec_take(0));
            memset(sec, 0, offsetof(struct chunk_sec, ids));
            for (int k = 0; k < NIB_N; ++k) sec_nib_point(sec, k, nib_const(0));
        }
        else sec = chunk_sec_make(c, s);
        uint8_t fl = pack_scratch[o++];
        memcpy(sec->ids, pack_scratch + o, SEC_CELLS); o += SEC_CELLS;
        for (int k = 0; k < NIB_N; ++k)
            if (fl >> k & 1) sec_nib_const(sec, k, 0);
            else { chunk_sec_nib_store(sec, k, pack_scratch + o); o += SEC_CELLS / 2; }
        if (fl & 8)
        {
            sec->ids_hi = chunk_alloc(SEC_CELLS / 2, 0);
            memcpy(sec->ids_hi, pack_scratch + o, SEC_CELLS / 2);
            o += SEC_CELLS / 2;
        }
    }
    memcpy(c->rtick_mask, rtick, sizeof rtick);
}

size_t chunk_bytes_resident(const struct chunk *c)
{
    size_t n = sizeof *c + c->packed_n;

    for (int s = 0; s < 16; ++s)
        if (c->band[s]) n += chunk_sec_bytes(chunk_sec_at(c, s));
    return n;
}

/* The index of Material.air in MATERIALS, looked up by name so a regenerated
 * blocks.h cannot change what it means (the same pattern features_lakes.c and
 * ticks.c use). */
static int air_material_index = -1;

/* before main: the same for every environment */
__attribute__((constructor)) static void air_material_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, "air") == 0) air_material_index = (int)i;
}

static int air_material(void)
{
    return air_material_index;
}

/* Chunk.func_150808_b. */
static int chunk_opacity(const struct chunk *c, int x, int y, int z)
{
    return BLOCKS[chunk_block(c, x, y, z) & 4095].opacity;
}

/* Block.isOpaqueCube, the test BlockChest.func_149954_e picks a facing with. */
static int opaque_cube(int id)
{
    return BLOCKS[id & 4095].opaque_cube;
}

/* Chunk.canBlockSeeTheSky: y >= heightMap[z << 4 | x]. y is not clamped. */
static int chunk_see_sky(const struct chunk *c, int x, int y, int z)
{
    return y >= c->height[z << 4 | x];
}

/* Chunk.getSavedLightValue. A cell in a section that does not exist has no
 * stored light, so vanilla falls back to the sky default when the cell is above
 * the height map and to 0 below it; the Nether reads 0 from an existing
 * section's sky map no matter what is stored, which is never anything. */
static int chunk_saved_light(const struct chunk *c, int type, int x, int y, int z)
{
    if (!section_present(c, y)) return chunk_see_sky(c, x, y, z) ? LIGHT_DEFAULT_VALUE(type) : 0;
    if (type == LIGHT_SKY && c->no_sky) return 0;
    return type == LIGHT_SKY ? chunk_cell_sky(c, CELL(x, y, z)) : chunk_cell_blocklight(c, CELL(x, y, z));
}

/* Chunk.setLightValue: a missing section is made first, which resets the whole
 * sky map, then the value is stored. */
static void chunk_set_light(struct chunk *c, int type, int x, int y, int z, int value)
{
    if (!section_present(c, y))
    {
        c->mask |= (uint16_t)(1 << (y >> 4));
        generate_skylight_map(c);
    }

    if (type == LIGHT_SKY)
    {
        /* the store is what provider.hasNoSky skips; the section above is
         * made before the check, as Java's setLightValue does */
        if (!c->no_sky) chunk_set_sky(c, CELL(x, y, z), value);
    }
    else chunk_set_blocklight(c, CELL(x, y, z), value);
}

/* Every block or light write stamps the chunk with a new world epoch, before
 * the write, and the edge it lies on; a write that makes a section (which
 * regenerates the whole sky map) and an insertion stamp every edge. Metadata
 * is not stamped: no light path reads it. The light re-check memo reads the
 * stamps (chunk_light_populate_memo). */
static inline void chunk_touch(struct world *w, struct chunk *c, int x, int y, int z)
{
    uint64_t e = ++w->epoch;

    c->stamp = e;
    c->wseq = ++w->wseq;

    if (y < 0 || !section_present(c, y))
    {
        for (int i = 0; i < 4; ++i) c->edge_stamp[i] = e;
        return;
    }

    if (x == 0) c->edge_stamp[0] = e;
    if (x == 15) c->edge_stamp[1] = e;
    if (z == 0) c->edge_stamp[2] = e;
    if (z == 15) c->edge_stamp[3] = e;
}

/* The chunk map: open addressing with linear probing, key (cx, cz), load factor
 * kept under 1/2 so the table can double. */
static size_t slot_of_cap(size_t cap, int cx, int cz)
{
    uint64_t h = (uint64_t)(uint32_t)cx * 0x9e3779b97f4a7c15ULL ^
                 (uint64_t)(uint32_t)cz * 0xc2b2ae3d27d4eb4fULL;
    h ^= h >> 29;
    return (size_t)h & (cap - 1);
}

static size_t slot_of(const struct world *w, int cx, int cz)
{
    return slot_of_cap(w->cap, cx, cz);
}

static int near_slot(int cx, int cz)
{
    return (cx & 15) | (cz & 15) << 4;
}

static chunkref chunk_find_ref(const struct world *w, int cx, int cz)
{
    int n = near_slot(cx, cz);
    int64_t key = WORLD_CKEY(cx, cz);
    chunkref r = w->near[n].ref;

    if (r != 0 && w->near[n].key == key) return r;

    size_t i = slot_of(w, cx, cz);

    for (;;)
    {
        r = w->slot[i];

        if (r == 0) return 0;

        if (w->skey[i] == key)
        {
            ((struct world *)w)->near[n].ref = r;
            ((struct world *)w)->near[n].key = key;
            return r;
        }

        i = (i + 1) & (w->cap - 1);
    }
}

static struct chunk *chunk_find(const struct world *w, int cx, int cz)
{
    return chunk_ptr(chunk_find_ref(w, cx, cz));
}

static void chunk_map_grow(struct world *w)
{
    size_t old_cap = w->cap;
    size_t cap = old_cap * 2;
    chunkref *old = w->slot;
    int64_t *oldkey = w->skey;
    chunkref *slot = w->slab_mem ? memset(slab_take(cap * sizeof *slot), 0, cap * sizeof *slot) : calloc(cap, sizeof *slot);
    int64_t *skey = w->slab_mem ? slab_take(cap * sizeof *skey) : malloc(cap * sizeof *skey);

    /* the new capacity has to be in place before the slots are recomputed:
     * slot_of masks with w->cap, so rehashing while the old cap is still
     * installed files every chunk under an index the lookups never probe */
    w->cap = cap;
    w->slot = slot;
    w->skey = skey;

    for (size_t i = 0; i < old_cap; ++i)
    {
        if (old[i] == 0) continue;

        /* the mask is the new cap's, not the world's yet */
        size_t j = slot_of_cap(cap, (int32_t)oldkey[i], (int32_t)(oldkey[i] >> 32));

        while (slot[j]) j = (j + 1) & (cap - 1);
        slot[j] = old[i];
        skey[j] = oldkey[i];
    }

    if (w->slab_mem)
    {
        slab_give(old, old_cap * sizeof *old);
        slab_give(oldkey, old_cap * sizeof *oldkey);
    }
    else
    {
        free(old);
        free(oldkey);
    }
}

static void te_registry_free(struct world *w);
static void te_registry_add(struct world *w, struct tile_entity *te);

void world_init(struct world *w, int64_t seed)
{
    memset(w, 0, sizeof *w);
    w->seed = seed;
    w->cap = 256;
    w->slot = calloc(w->cap, sizeof *w->slot);
    w->skey = malloc(w->cap * sizeof *w->skey);
}

void world_init_slab(struct world *w, int64_t seed)
{
    memset(w, 0, sizeof *w);
    w->seed = seed;
    w->cap = 256;
    w->slab_mem = 1;
    w->slot = memset(slab_take(w->cap * sizeof *w->slot), 0, w->cap * sizeof *w->slot);
    w->skey = slab_take(w->cap * sizeof *w->skey);
}

struct chunkgen *world_gen(struct world *w)
{
    if (__builtin_expect(!w->gen_made, 0))
    {
        uint8_t owed = w->gen.owed;

        chunkgen_init(&w->gen, w->seed);
        w->gen.owed = owed;
        w->gen_made = 1;
    }
    return &w->gen;
}

/* ChunkCoordIntPair.chunkXZ2Int. */
int64_t chunk_key(int cx, int cz)
{
    return (int64_t)(uint32_t)cx | (int64_t)(uint32_t)cz << 32;
}

static void load_order_push(struct world *w, int64_t key)
{
    if (w->lon == w->locap)
    {
        size_t cap = w->locap ? w->locap * 2 : 1024;
        w->load_order = w->slab_mem ? slab_grow(w->load_order, sizeof(int64_t) * w->locap, sizeof(int64_t) * cap)
                                    : realloc(w->load_order, sizeof(int64_t) * cap);
        w->locap = cap;
    }

    w->load_order[w->lon++] = key;
}

/* loadedChunks.remove(Object): the first occurrence. */
static void load_order_drop(struct world *w, int64_t key)
{
    for (size_t i = 0; i < w->lon; ++i)
    {
        if (w->load_order[i] == key)
        {
            memmove(w->load_order + i, w->load_order + i + 1, (w->lon - i - 1) * sizeof(int64_t));
            --w->lon;
            return;
        }
    }
}

void world_free(struct world *w)
{
    /* a client world's pending light calls go with it */
    struct lightdefer *ld = nw_env->light.defer;

    if (ld != NULL && ld->w == w)
    {
        ld->st.dropped += ld->n;
        ld->n = 0;
        ld->w = NULL;
    }
    if (ld != NULL && w->is_remote && ld->exec.forget != NULL) ld->exec.forget(ld->exec.ctx, w);

    /* every tile entity the world ever held, freed once: the chunk stores
     * hold only arrays from here on */
    te_registry_free(w);

    for (size_t i = 0; i < w->cap; ++i)
    {
        if (w->slot[i] == 0) continue;
        chunk_free(chunk_ptr(w->slot[i]));
    }

    if (w->slab_mem)
    {
        slab_give(w->slot, w->cap * sizeof *w->slot);
        slab_give(w->skey, w->cap * sizeof *w->skey);
        slab_give(w->load_order, w->locap * sizeof *w->load_order);
    }
    else
    {
        free(w->slot);
        free(w->skey);
        free(w->load_order);
    }
    free(w->te_list);
    free(w->te_added);
    free(w->owed);
    memset(w, 0, sizeof *w);
}

struct chunk *world_chunk(struct world *w, int cx, int cz)
{
    return chunk_find(w, cx, cz);
}

int world_chunk_loaded(struct world *w, int cx, int cz)
{
    return chunk_find(w, cx, cz) != NULL;
}

/* The raw Nether or End array: the provider (built on first use) reseeds its
 * rand from the chunk, then func_147419_a/func_147418_b and MapGenCavesHell
 * (the Nether), or func_147420_a/func_147421_b (the End). The provider's rand
 * is left as the generation leaves it. */
void world_dim_raw(struct world *w, int cx, int cz, int dim, uint16_t *blocks)
{
    world_dim_provider(w, dim);
    if (dim == -1)
    {
        /* provideChunk reseeds the provider rand before the density pass */
        jr_seed(&w->nether.rand,
                (int64_t)((uint64_t)(int64_t)cx * 341873128712ULL + (uint64_t)(int64_t)cz * 132897987541ULL));
        nether_terrain(&w->nether, cx, cz, blocks);
        nether_surface(&w->nether, cx, cz, blocks);
        nether_caves(w->seed, cx, cz, blocks);
    }
    else
    {
        jr_seed(&w->end.rand,
                (int64_t)((uint64_t)(int64_t)cx * 341873128712ULL + (uint64_t)(int64_t)cz * 132897987541ULL));
        end_terrain(&w->end, cx, cz, blocks);
        end_surface(cx, cz, blocks);
    }
}

void world_dim_provider(struct world *w, int dim)
{
    if (dim == -1 && !w->nether_ready)
    {
        nether_init(&w->nether, w->seed);
        w->nether_ready = 1;
    }
    if (dim == 1 && !w->end_ready)
    {
        end_init(&w->end, w->seed);
        w->end_ready = 1;
    }
}

/* The Chunk(World, Block[32768], cx, cz) constructor over the raw array,
 * whose sections only ever reach y 127 and whose metas all stay 0. The End's
 * provider then runs generateSkylightMap; the Nether's provider does not, so
 * its height maps stay 0 and its sky array stays empty until a write makes a
 * section. */
void world_dim_construct(struct chunk *c, int cx, int cz, int dim, const uint16_t *blocks)
{
    chunk_clear(c);
    c->cx = cx;
    c->cz = cz;
    c->no_sky = dim != 0;

    for (int i = 0; i < DIM_CELLS; ++i)
    {
        int y = i & 127;

        if (blocks[i] == BLK_AIR) continue;

        int cell = (i >> 11) << 12 | ((i >> 7) & 15) << 8 | y;

        chunk_set_id(c, cell, blocks[i]);
        c->mask |= (uint16_t)(1 << (y >> 4));

        /* ExtendedBlockStorage.func_150818_a's tickRefCount, as the Chunk
         * constructor fills the sections */
        if (BLOCKS[blocks[i] & 4095].tick_randomly) ++c->sections_ticking[y >> 4];
    }

    if (dim == -1)
    {
        /* ChunkProviderHell.provideChunk copies WorldChunkManagerHell's biome
         * (BiomeGenBase.hell) into every column, after the Chunk constructor */
        for (int i = 0; i < 256; ++i) c->biome[i] = DIM_BIOME_HELL;

        for (int i = 0; i < 256; ++i) c->precip[i] = -999;
    }
    else
    {
        /* ChunkProviderEnd.provideChunk copies loadBlockGeneratorData's biomes
         * (BiomeGenBase.sky everywhere), then generateSkylightMap. Its provider
         * never calls resetRelightChecks, so the Chunk field initializer's
         * 4096 stands (the Nether's provider resets it to 0). */
        end_biomes(c->biome);
        generate_skylight_map(c);
        c->queued_light_checks = 4096;
    }
}

/* The raw Nether or End chunk, then the constructor. */
static void load_dim_chunk(struct world *w, struct chunk *c, int cx, int cz, int dim)
{
    uint16_t *blocks = nw_scratch->dim_blocks;   /* the raw array, index x << 11 | z << 7 | y */

    world_dim_raw(w, cx, cz, dim, blocks);
    world_dim_construct(c, cx, cz, dim, blocks);
}

/* ChunkProviderServer.loadChunk with Probe.rawChunks set: safeLoadChunk finds
 * nothing on disk, so the generator provides the chunk, which is inserted, and
 * populateChunk is skipped. */
static struct chunk *load_chunk_miss(struct world *w, int cx, int cz)
{
    if (w->provide != NULL)
    {
        struct chunk *c = w->provide(w->provide_ctx, cx, cz);
        if (c != NULL) return c;
    }
    return world_chunk_request(w, CREQ_GENERATE, cx, cz, NULL, NULL);
}

struct chunk *world_load_chunk(struct world *w, int cx, int cz)
{
    struct chunk *c = chunk_find(w, cx, cz);

    if (c != NULL) return c;
    PHASE_SUB(PS_LOAD, c = load_chunk_miss(w, cx, cz));
    return c;
}

/* ------------------------------------------------------ chunk requests */

/* The C engine's providers: ChunkProviderGenerate (chunkgen.c), and the
 * Nether's and the End's (load_dim_chunk). */
static void backend_c_generate(struct world *w, int cx, int cz, struct chunk *c)
{
    if (w->dim == 0) provide_chunk(world_gen(w), cx, cz, c);
    else load_dim_chunk(w, c, cx, cz, w->dim);
}

const struct chunk_backend chunk_backend_c = {"c", backend_c_generate, NULL, NULL};

static void request_note(struct world *w, int kind, int cx, int cz)
{
    struct chunk_req *r = &w->reqs.ring[w->reqs.n % CREQ_RING];

    r->cx = cx;
    r->cz = cz;
    r->kind = kind;
    ++w->reqs.n;
    ++w->reqs.kinds[kind];
}

/* The generation into scratch: the provider state it leaves is the point. A
 * backend that holds that state applies it without the chunk (owed). */
static void generate_owed(struct world *w, const struct chunk_backend *be, int64_t key, struct chunk **scratch)
{
    int cx = (int)(uint32_t)key, cz = (int)(key >> 32);

    request_note(w, CREQ_OWED, cx, cz);
    if (PHASE_PROF_ON()) phase_sub_begin_(PS_GEN);
    if (be->owed == NULL || !be->owed(w, cx, cz))
    {
        if (*scratch == NULL) *scratch = chunk_new();
        be->generate(w, cx, cz, *scratch);
    }
    if (PHASE_PROF_ON()) phase_sub_end_(PS_GEN);
}

/* The overworld generations world_insert_chunk skipped, run into scratch in
 * their order, so the provider holds the state they would have left. */
static void pay_owed(struct world *w, const struct chunk_backend *be)
{
    if (w->nowed == 0) return;
    if (w->gen.owed)
    {
        struct chunk *scratch = NULL;

        for (size_t k = 0; k < w->nowed; ++k) generate_owed(w, be, w->owed[k], &scratch);
        chunk_free(scratch);
    }
    w->gen.owed = 0;
    w->nowed = 0;
}

void world_gen_state(struct world *w, const uint8_t *biome, const uint8_t *tops, uint64_t rand)
{
    if (w->dim == 0)
    {
        surface_tops_merge(&world_gen(w)->surface, biome, tops);
        world_gen(w)->terrain.rand.seed = rand;
    }
    else
    {
        world_dim_provider(w, w->dim);
        if (w->dim == -1) w->nether.rand.seed = rand;
        else w->end.rand.seed = rand;
    }
}

/* The chunk joins the map, the load order, the stronghold list and the
 * on_chunk hook. */
static struct chunk *put_chunk(struct world *w, struct chunk *c)
{
    cw_sync(w, LD_SYNC_WRITE);
    if (w->used * 2 >= w->cap) chunk_map_grow(w);

    size_t i = slot_of(w, c->cx, c->cz);

    while (w->slot[i]) i = (i + 1) & (w->cap - 1);
    w->slot[i] = chunk_ref(c);
    w->skey[i] = WORLD_CKEY(c->cx, c->cz);
    ++w->used;
    ++w->map_ver;
    /* a chunk from another world (the region store, the client copy) keeps
     * no memo: its epoch was another world's */
    chunk_touch(w, c, 0, -1, 0);
    c->lm_valid = 0;
    /* ChunkProviderServer.loadChunk appends to loadedChunks and then calls
     * populateChunk, so the chunk is in the load order before anything the
     * hook triggers loads */
    load_order_push(w, chunk_key(c->cx, c->cz));
    if (w->dim == 0) stronghold_note_chunk(w, c->cx, c->cz);
    if (w->on_chunk != NULL) w->on_chunk(w->on_chunk_ctx, c->cx, c->cz);
    return c;
}

struct chunk *world_chunk_request(struct world *w, int kind, int cx, int cz, struct chunk *given,
                                  const uint8_t *biome)
{
    const struct chunk_backend *be = w->backend != NULL ? w->backend : &chunk_backend_c;
    struct chunk *c;

    switch (kind)
    {
    case CREQ_GENERATE:
        c = chunk_find(w, cx, cz);
        if (c != NULL) return c;
        request_note(w, kind, cx, cz);
        if (w->dim == 0) pay_owed(w, be);
        if (PHASE_PROF_ON()) phase_sub_begin_(PS_GEN);
        c = be->take != NULL ? be->take(w, cx, cz) : NULL;
        if (c == NULL)
        {
            c = chunk_new();
            be->generate(w, cx, cz, c);
        }
        if (PHASE_PROF_ON()) phase_sub_end_(PS_GEN);
        /* the providers leave (cx, cz) set; the constructor did */
        c->cx = cx;
        c->cz = cz;
        return put_chunk(w, c);

    case CREQ_STORED:
        request_note(w, kind, given->cx, given->cz);
        return put_chunk(w, given);

    case CREQ_SUPPLIED:
        request_note(w, kind, given->cx, given->cz);
        given->no_sky = w->dim != 0;
        if (biome != NULL) memcpy(given->biome, biome, sizeof given->biome);
        else world_chunk_generated_meta(w, given);

        if (w->nowed == w->owedcap)
        {
            w->owedcap = w->owedcap ? w->owedcap * 2 : 1024;
            w->owed = realloc(w->owed, sizeof *w->owed * w->owedcap);
        }
        w->owed[w->nowed++] = chunk_key(given->cx, given->cz);
        w->gen.owed = 1;
        return put_chunk(w, given);

    case CREQ_OWED:
        /* the Nether's and the End's provider state is the last chunk's */
        if (w->dim == 0 || w->nowed == 0) return NULL;
        c = NULL;
        generate_owed(w, be, w->owed[w->nowed - 1], &c);
        chunk_free(c);
        w->gen.owed = 0;
        w->nowed = 0;
        return NULL;
    }

    abort();
}

struct chunk *world_generate_chunk(struct world *w, int cx, int cz)
{
    return world_chunk_request(w, CREQ_GENERATE, cx, cz, NULL, NULL);
}

struct chunk *world_insert_chunk(struct world *w, int cx, int cz, const uint8_t *biome)
{
    struct chunk *c = chunk_find(w, cx, cz);

    if (c != NULL) return c;

    c = chunk_new();
    c->cx = cx;
    c->cz = cz;
    return world_chunk_request(w, CREQ_SUPPLIED, cx, cz, c, biome);
}

struct chunk *world_insert_chunk_at(struct world *w, struct chunk *c, const uint8_t *biome)
{
    return world_chunk_request(w, CREQ_SUPPLIED, c->cx, c->cz, c, biome);
}

void world_chunk_generated_meta(struct world *w, struct chunk *c)
{
    if (w->dim == 0)
    {
        int b[256];

        /* provide_chunk's biomes_full, then the Chunk constructor's 4096 */
        biomes_full(&world_gen(w)->terrain.layers, c->cx * 16, c->cz * 16, 16, 16, b);
        for (int i = 0; i < 256; ++i) c->biome[i] = (uint8_t)b[i];
        c->queued_light_checks = 4096;
    }
    else if (w->dim == -1)
    {
        for (int i = 0; i < 256; ++i) c->biome[i] = DIM_BIOME_HELL;
        c->queued_light_checks = 0;
    }
    else
    {
        end_biomes(c->biome);
        c->queued_light_checks = 4096;
    }
}

void chunk_count_ticking(struct chunk *c)
{
    memset(c->sections_ticking, 0, sizeof c->sections_ticking);
    for (int s = 0; s < 16; ++s)
    {
        const struct chunk_sec *sec = chunk_sec_at(c, s);

        if (sec == NULL) continue;
        for (int k = 0; k < SEC_CELLS; ++k)
            if (BLOCKS[chunk_sec_id(sec, k)].tick_randomly) ++c->sections_ticking[s];
    }
}

void world_owed_last(struct world *w)
{
    world_chunk_request(w, CREQ_OWED, 0, 0, NULL, NULL);
}

/* The map's slot of an existing chunk. */
static size_t slot_held(const struct world *w, int cx, int cz)
{
    size_t i = slot_of(w, cx, cz);

    while (w->skey[i] != WORLD_CKEY(cx, cz)) i = (i + 1) & (w->cap - 1);
    return i;
}

/* Linear-probe removal with backward-shift re-slotting, so later probes still
 * find their chains: drop the entry, then walk the cluster, pulling every
 * following entry up when its home slot is at or before the freed one. */
struct chunk *world_take_chunk(struct world *w, int cx, int cz)
{
    if (!world_chunk_loaded(w, cx, cz)) return NULL;
    cw_sync(w, LD_SYNC_WRITE);

    size_t i = slot_held(w, cx, cz);
    struct chunk *c = chunk_ptr(w->slot[i]);
    size_t cap = w->cap;

    if (w->near[near_slot(cx, cz)].ref == w->slot[i]) w->near[near_slot(cx, cz)].ref = 0;

    w->slot[i] = 0;
    --w->used;
    ++w->map_ver;

    for (size_t j = (i + 1) & (cap - 1); w->slot[j]; j = (j + 1) & (cap - 1))
    {
        size_t k = slot_of_cap(cap, (int32_t)w->skey[j], (int32_t)(w->skey[j] >> 32));

        if ((j > i ? k <= i || k > j : k <= i && k > j))
        {
            w->slot[i] = w->slot[j];
            w->skey[i] = w->skey[j];
            w->slot[j] = 0;
            i = j;
        }
    }

    load_order_drop(w, chunk_key(cx, cz));
    return c;
}

void world_put_chunk(struct world *w, struct chunk *c)
{
    put_chunk(w, c);
}

int world_unload_chunk(struct world *w, int cx, int cz)
{
    struct chunk *c = world_take_chunk(w, cx, cz);
    if (!c) return 0;
    chunk_free(c);
    return 1;
}

/* The two chunk bits chunkgen needs. */

/* Chunk(World, Block[], byte[], int, int): a section per 16-block band that
 * holds a block that is neither null nor air, every array of a new section
 * zeroed; air cells leave the metadata alone. */
/* Block.getTickRandomly by id, 0 or 1: built before main from BLOCKS, the
 * same for every environment */
static uint8_t TICK_RANDOMLY[4096];

__attribute__((constructor)) static void tick_randomly_init(void)
{
    for (int id = 0; id < 4096; ++id) TICK_RANDOMLY[id] = BLOCKS[id].tick_randomly != 0;
}

void chunk_construct(struct chunk *c, int cx, int cz, const uint16_t *ids, const uint8_t *metas)
{
    chunk_clear(c);
    c->cx = cx;
    c->cz = cz;
    /* Chunk's constructor sets queuedLightChecks to 4096, so a chunk that has
     * never ticked schedules no relight work (enqueueRelightChecks returns at
     * once); Chunk.func_150804_b's resetRelightChecks is the only thing that
     * brings it back to 0. */
    c->queued_light_checks = 4096;

    /* one band at a time: a band holding a block that is not air is in the
     * mask and has storage, with every id (chunk_cells_in's rule: storage
     * for a band with a nonzero cell) and, for the cells that are not air,
     * the metadata (an air cell keeps the new section's zeroed metadata,
     * whatever the generator's byte[] carried); the random-tick count per
     * band covers every cell */
    for (int s = 0; s < 16; ++s)
    {
        unsigned any = 0;

        for (int col = 0; col < 256; ++col)
        {
            const uint16_t *p = ids + (col << 8 | s << 4);

            for (int y = 0; y < 16; ++y) any |= p[y];
        }

        if (any == 0)
        {
            c->sections_ticking[s] = (uint16_t)(BLOCKS[BLK_AIR].tick_randomly ? 4096 : 0);
            continue;
        }

        c->mask |= (uint16_t)(1 << s);

        struct chunk_sec *sec = chunk_sec_make(c, s);
        unsigned ticking = 0;
        /* a band whose ids all fit a byte stores them without the high
         * nibbles (a new band has none) */
        int bytes = any <= 255 && sec->ids_hi == NULL;

        for (int col = 0; col < 256; ++col)
        {
            const uint16_t *p = ids + (col << 8 | s << 4);
            const uint8_t *m = metas + (col << 8 | s << 4);
            uint8_t *dst = sec->ids + (col << 4);

            if (bytes)
                for (int y = 0; y < 16; ++y)
                {
                    dst[y] = (uint8_t)p[y];
                    ticking += TICK_RANDOMLY[p[y]];
                }
            else
                for (int y = 0; y < 16; ++y)
                {
                    int id = p[y];

                    if (id > 4095) chunk_value_range("id", id);
                    dst[y] = (uint8_t)id;
                    if (id > 255 || sec->ids_hi) chunk_sec_set_id_hi(sec, col << 4 | y, id >> 8);
                    ticking += TICK_RANDOMLY[id];
                }

            uint8_t b8[8];
            for (int k = 0; k < 8; ++k)
            {
                int lo = p[2 * k] != BLK_AIR ? m[2 * k] : 0, hi = p[2 * k + 1] != BLK_AIR ? m[2 * k + 1] : 0;

                if (lo > 15) chunk_value_range("metas", lo);
                if (hi > 15) chunk_value_range("metas", hi);
                b8[k] = (uint8_t)(lo | hi << 4);
            }
            sec_nib_put8(sec, NIB_METAS, col << 3, b8);
        }

        c->sections_ticking[s] = (uint16_t)ticking;
    }
}

/* Chunk.generateSkylightMap, over the sections the chunk already has; the sky
 * fill is the half provider.hasNoSky skips (the Nether, whose provider also
 * never calls this at construction: a raw Nether chunk keeps the 0 height maps
 * the constructor made until a write makes a section). */
/* Chunk.generateHeightMap: the height map, heightMapMinimum and the
 * precipitation map's reset, from the top band with storage */
static int chunk_height_map(struct chunk *c)
{
    int top = 0;
    for (int s = 15; s >= 0; --s)
        if (c->mask & (1 << s))
        {
            top = s * 16;
            break;
        }

    c->height_min = INT32_MAX;

    for (int z = 0; z < 16; ++z)
    {
        for (int x = 0; x < 16; ++x)
        {
            c->precip[x + (z << 4)] = -999;
            int v = top + 15, i0 = x << 8 | z << 4;

            /* the first opaque block from the top; a column of nothing leaves
             * heightMap and heightMapMinimum where they were. A band with no
             * storage is air to its bottom; one with storage is read down
             * from the cell below v */
            while (v > 0)
            {
                int s = (v - 1) >> 4, yy = (v - 1) & 15;
                const struct chunk_sec *sec = chunk_sec_at(c, s);

                v = s << 4;
                if (sec == NULL) continue;

                for (; yy >= 0; --yy)
                    if (BLOCKS[chunk_sec_id(sec, i0 | yy) & 4095].opacity != 0) break;
                if (yy < 0) continue;

                v += yy + 1;
                c->height[z << 4 | x] = v;

                if (v < c->height_min) c->height_min = v;
                break;
            }
        }
    }
    return top;
}

void chunk_generate_height_map(struct chunk *c)
{
    (void)chunk_height_map(c);
}

void generate_skylight_map(struct chunk *c)
{
    /* light.c's skylight_map over the chunk's own storage: the height-map
     * half in every dimension, the sky fill where the provider has a sky */
    int top = chunk_height_map(c);

    if (c->no_sky) return;

    for (int z = 0; z < 16; ++z)
    {
        for (int x = 0; x < 16; ++x)
        {
            int light = 15, y = top + 15, i0 = x << 8 | z << 4;
            int h = c->height[z << 4 | x], lo = h > 1 ? h : 1;
            const struct chunk_sec *ts = chunk_sec_at(c, y >> 4);

            /* every cell from the height map up to the one below the top
             * has opacity 0 (generateHeightMap reads from the top less one
             * down to the first opacity, or leaves the map when the column
             * has none, all of opacity 0), so when the top cell has none
             * either the light stays 15 there and each present band's cells
             * take 15: a whole column of a band is its eight bytes */
            if (lo <= y && BLOCKS[(ts != NULL ? chunk_sec_id(ts, i0 | (y & 15)) : 0) & 4095].opacity == 0)
            {
                for (int s = y >> 4; s >= lo >> 4; --s)
                {
                    if (!(c->mask >> s & 1)) continue;

                    int a = s << 4 > lo ? s << 4 : lo, b = (s << 4 | 15) < y ? s << 4 | 15 : y;
                    struct chunk_sec *sec = chunk_sec_at(c, s);

                    if (sec != NULL && sec->shared == 0)
                    {
                        if (a == s << 4 && b == (s << 4 | 15))
                        {
                            static const uint8_t full[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
                            sec_nib_put8(sec, NIB_SKY, i0 >> 1, full);
                        }
                        else
                            for (int yy = a; yy <= b; ++yy) chunk_sec_nib_set(sec, NIB_SKY, i0 | (yy & 15), 15);
                        continue;
                    }
                    for (int yy = b; yy >= a; --yy) chunk_set_sky(c, CELL(x, yy, z), 15);
                }
                y = lo - 1;
                if (y <= 0) continue;
            }

            /* band by band down the column: each band's storage, its bit in
             * the mask and whether it is shared looked up once; a band whose
             * store would make or copy its storage (chunk_set_sky) takes the
             * cell-by-cell loop, which looks the band up again after each
             * such store */
            do
            {
                int s = y >> 4;
                struct chunk_sec *sec = chunk_sec_at(c, s);
                int present = (c->mask >> s) & 1;

                if (sec != NULL && sec->shared == 0)
                {
                    for (int yy = y & 15;; --yy)
                    {
                        int i = i0 | yy;
                        int o = BLOCKS[chunk_sec_id(sec, i) & 4095].opacity;

                        if (o == 0 && light != 15) o = 1;
                        light -= o;
                        if (light > 0 && present) chunk_sec_nib_set(sec, NIB_SKY, i, light);
                        --y;
                        if (!(y > 0 && light > 0) || yy == 0) break;
                    }
                    continue;
                }

                do
                {
                    int i = i0 | (y & 15);
                    int o = BLOCKS[(sec ? chunk_sec_id(sec, i) : 0) & 4095].opacity;

                    if (o == 0 && light != 15) o = 1;
                    light -= o;

                    if (light > 0 && present)
                    {
                        if (sec != NULL && sec->shared == 0) chunk_sec_nib_set(sec, NIB_SKY, i, light);
                        else
                        {
                            chunk_set_sky(c, CELL(x, y, z), light);
                            sec = chunk_sec_at(c, s);
                        }
                    }
                    --y;
                }
                while (y > 0 && light > 0 && y >> 4 == s);
            }
            while (y > 0 && light > 0);
        }
    }
    /* a band the fill left one value (15 above the ground) holds it as a
     * constant */
    for (int s = 0; s < 16; ++s)
    {
        struct chunk_sec *sec = chunk_sec_at(c, s);
        if (sec != NULL && sec->shared == 0 && (sec->nib_own >> NIB_SKY & 1)) chunk_sec_nib_compact(sec);
    }
}

/* World.checkChunksExist: the y range has to bracket the world and every chunk
 * in the x/z box has to be loaded. */
static int chunks_exist_box(struct world *w, int x0, int y0, int z0, int x1, int y1, int z1)
{
    if (!(y1 >= 0 && y0 < 256)) return 0;
    /* ChunkProviderClient.chunkExists is always true (a chunk the client
     * lacks reads as the blank chunk) */
    if (w->is_remote) return 1;

    x0 >>= 4;
    z0 >>= 4;
    x1 >>= 4;
    z1 >>= 4;

    for (int cx = x0; cx <= x1; ++cx)
        for (int cz = z0; cz <= z1; ++cz)
            if (!world_chunk_loaded(w, cx, cz)) return 0;

    return 1;
}

/* World.blockExists: the chunk is loaded and the y is inside the world. The
 * collision scan passes 64 for y, which is always inside. */
/* World.doChunksNearChunkExist / checkChunksExist: the y arguments only have to
 * bracket the world, then every chunk in the x/z box must be loaded. */
int world_do_chunks_near_chunk_exist(struct world *w, int x, int y, int z, int d)
{
    int y0 = y - d, y1 = y + d;

    if (!(y1 >= 0 && y0 < 256)) return 0;

    return chunks_exist_box(w, x - d, y - d, z - d, x + d, y + d, z + d);
}

/* World.checkChunksExist. Java shifts the four x/z arguments to chunk
 * coordinates; the y test is on the raw arguments and rejects a box that
 * leaves the world vertically. */
int world_check_chunks_exist(struct world *w, int x0, int y0, int z0, int x1, int y1, int z1)
{
    if (!(y1 >= 0 && y0 < 256)) return 0;
    if (w->is_remote) return 1;   /* ChunkProviderClient.chunkExists */

    int cx0 = x0 >> 4, cz0 = z0 >> 4, cx1 = x1 >> 4, cz1 = z1 >> 4;

    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cz = cz0; cz <= cz1; ++cz)
            if (!world_chunk_loaded(w, cx, cz)) return 0;

    return 1;
}

int world_entity_area_loaded(struct world *w, int x, int z)
{
    if (w->is_remote) return 1;   /* ChunkProviderClient.chunkExists */

    /* (x + 32) >> 4 is always (x - 32) >> 4 plus 4: the corner names the area */
    int cx0 = (x - 32) >> 4, cz0 = (z - 32) >> 4;
    /* by the corner's place in a 16 by 16 chunk window: the corners of the
     * livings in a 256-block area never share a slot (a hash over 64 slots
     * had a third of the calls miss, each 25 chunk lookups) */
    int k = (cx0 & 15) | (cz0 & 15) << 4;

    if (w->amemo[k].ver == w->map_ver + 1 && w->amemo[k].cx == cx0 && w->amemo[k].cz == cz0) return w->amemo[k].ok;

    int ok = 1;

    for (int cx = cx0; cx <= cx0 + 4 && ok; ++cx)
        for (int cz = cz0; cz <= cz0 + 4 && ok; ++cz)
            if (!world_chunk_loaded(w, cx, cz)) ok = 0;
    w->amemo[k].cx = cx0;
    w->amemo[k].cz = cz0;
    w->amemo[k].ver = w->map_ver + 1;
    w->amemo[k].ok = ok;
    return ok;
}

/* World.getCollidingBoundingBoxes' block scan. The entity argument is only
 * used for the second half of the Java method (the entity list), which the
 * probe world has nothing in: ProbeEntity.getCollisionBox is Entity's, and
 * returns null, and no other entity is ever spawned. */
void world_get_colliding_bounding_boxes(struct world *w, struct aabb box, struct collide_list *l)
{
    int var3 = mh_floor(box.min_x);
    int var4 = mh_floor(box.max_x + 1.0);
    int var5 = mh_floor(box.min_y);
    int var6 = mh_floor(box.max_y + 1.0);
    int var7 = mh_floor(box.min_z);
    int var8 = mh_floor(box.max_z + 1.0);

    for (int x = var3; x < var4; ++x)
    {
        for (int z = var7; z < var8; ++z)
        {
            /* block_exists: the column's chunk, loaded */
            struct chunk *c = world_chunk(w, x >> 4, z >> 4);

            if (c != NULL)
            {
                for (int y = var5 - 1; y < var6; ++y)
                {
                    /* outside the +/-30000000 box Java substitutes stone, which
                     * for the collision scan is the same full cube as any
                     * opaque block; the shape list never reaches it. A cell
                     * of the loaded column in the world's height reads
                     * straight from its chunk (world_get_block's answer), the
                     * rest through world_get_block. */
                    if ((unsigned)y < 256u && (unsigned)x + 30000000u < 60000000u && (unsigned)z + 30000000u < 60000000u)
                        collide_add_boxes_id(w, x, y, z, chunk_xyz_id(c, x & 15, y, z & 15) & 4095, &box, l);
                    else collide_add_boxes(w, x, y, z, &box, l);
                }
            }
        }
    }
}

/* World.getCollidingBoundingBoxes(...).isEmpty(), for the sneak clamp and the
 * server handler's stance checks. */
int world_colliding_boxes_empty(struct world *w, struct aabb box)
{
    struct collide_list *l COLLIDE_SCRATCH = collide_scratch_begin();

    collide_list_clear(l);
    world_get_colliding_bounding_boxes(w, box, l);
    return l->n == 0;
}

/* World.func_147470_e: fire, flowing lava or lava anywhere in the box. */
int world_is_in_fire(struct world *w, struct aabb box)
{
    int var2 = mh_floor(box.min_x);
    int var3 = mh_floor(box.max_x + 1.0);
    int var4 = mh_floor(box.min_y);
    int var5 = mh_floor(box.max_y + 1.0);
    int var6 = mh_floor(box.min_z);
    int var7 = mh_floor(box.max_z + 1.0);

    if (!world_check_chunks_exist(w, var2, var4, var6, var3, var5, var7)) return 0;

    for (int x = var2; x < var3; ++x)
    {
        for (int y = var4; y < var5; ++y)
        {
            for (int z = var6; z < var7; ++z)
            {
                int id = world_get_block(w, x, y, z) & 4095;

                if (id == 51 || id == 10 || id == 11) return 1;
            }
        }
    }

    return 0;
}

/* World.getHeightValue. A chunk that is not loaded reports 0 rather than
 * generating, exactly as the Java guard does. */
int world_get_height_value(struct world *w, int x, int z)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z))) return 64;

    struct chunk *c = world_chunk(w, x >> 4, z >> 4);

    if (c == NULL) return 0;

    return c->height[(z & 15) << 4 | (x & 15)];
}

/* The chunk a world-coordinate accessor lands in: the loaded one, else a chunk
 * generated on demand (vanilla), else NULL when the probe turned generation off
 * (ChunkProviderServer's loadChunkOnProvideRequest = false reads an EmptyChunk,
 * whose func_150810_a is air). */
static struct chunk *chunk_for(struct world *w, int x, int z)
{
    if (!w->no_generate) return world_load_chunk(w, x >> 4, z >> 4);
    return world_chunk(w, x >> 4, z >> 4);
}

int world_get_block_slow(struct world *w, int x, int y, int z)
{
    /* World.getBlock answers air outside these bounds without touching a
     * chunk */
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z) && y >= 0 && y < 256)) return BLK_AIR;

    /* it reaches getChunkFromChunkCoords, which generates a chunk that is not
     * loaded yet (the light engine's neighbor scan gets this far); a chunk in
     * the map's near cache is loaded */
    int cx = x >> 4, cz = z >> 4;
    struct chunk *c = world_chunk_near(w, cx, cz);

    if (c == NULL) c = chunk_for(w, x, z);
    if (c == NULL) return BLK_AIR;

    return chunk_block(c, x & 15, y, z & 15);
}

/* World.getBlock down one column from ytop: the highest y <= ytop holding
 * block id, or -1. Its first read loads the column's chunk, as the first
 * getBlock of a column scan does, and the rest read that chunk. */
int world_column_find_down(struct world *w, int x, int z, int ytop, int id)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z)) || ytop < 0) return -1;
    if (ytop > 255) ytop = 255;

    struct chunk *c = chunk_for(w, x, z);

    if (c == NULL) return -1;

    for (int y = ytop; y >= 0; --y)
        if (chunk_block(c, x & 15, y, z & 15) == id) return y;

    return -1;
}

int world_get_meta_slow(struct world *w, int x, int y, int z)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z) && y >= 0 && y < 256)) return 0;

    struct chunk *c = chunk_for(w, x, z);

    return c == NULL ? 0 : chunk_meta(c, x & 15, y, z & 15);
}

/* World.getSavedLightValue: y is clamped into the world, and a chunk that is
 * not loaded reads as the type's default value instead of loading. */
int world_get_light(struct world *w, int type, int x, int y, int z)
{
    cw_sync(w, LD_SYNC_READ);
    if (y < 0) y = 0;
    if (y >= 256) y = 255;
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z))) return LIGHT_DEFAULT_VALUE(type);

    struct chunk *c = world_chunk(w, x >> 4, z >> 4);

    /* EmptyChunk.getSavedLightValue is 0, an absent chunk reads its type's
     * default value (World.getSavedLightValue with a chunk that is not
     * loaded); the two coincide only with generation off */
    if (c == NULL) return w->no_generate ? 0 : LIGHT_DEFAULT_VALUE(type);

    return chunk_saved_light(c, type, x & 15, y, z & 15);
}

int world_can_block_see_the_sky(struct world *w, int x, int y, int z)
{
    /* like Java, no bounds check: this is only reached from computeLightValue
     * for a position inside the light pass's loaded box */
    struct chunk *c = chunk_for(w, x, z);

    if (c == NULL) return 0;

    return chunk_see_sky(c, x & 15, y, z & 15);
}

/* World.getFullBlockLightValue: Chunk.getBlockLightValue with darkness 0, the
 * greater of the stored sky light (0 in a hasNoSky world) and block light. A
 * section that does not exist holds no light: 15 in a world with a sky, 0 in
 * the Nether. */
int world_full_block_light_value(struct world *w, int x, int y, int z)
{
    cw_sync(w, LD_SYNC_READ);
    if (y < 0) return 0;
    if (y >= 256) y = 255;

    struct chunk *c = world_load_chunk(w, x >> 4, z >> 4);

    if (!section_present(c, y)) return c->no_sky ? 0 : 15;

    int sky = c->no_sky ? 0 : chunk_cell_sky(c, CELL(x & 15, y, z & 15));
    int block = chunk_cell_blocklight(c, CELL(x & 15, y, z & 15));

    return sky > block ? sky : block;
}

/* The light pass's chunk lookups go through the world's window (struct
 * world lwin): the 8x8 chunks from 3 below the chunk the pass starts in, each
 * looked up in the map once. Every cell a pass reads or writes is within 18
 * blocks of its origin, so inside the window; a read further out takes the
 * map. A read that loads a chunk (World.getBlock generating it) changes
 * map_ver, which drops the window, so every answer is the map's. */
#define LW_N 8
#define LW_LO 3   /* the window's first chunk is lwin_cx - LW_LO */

static inline struct chunk *lw_chunk(struct world *w, int cx, int cz)
{
    unsigned i = (unsigned)(cx - w->lwin_cx + LW_LO), k = (unsigned)(cz - w->lwin_cz + LW_LO);

    if (__builtin_expect(w->lwin_ver != w->map_ver, 0))
    {
        w->lwin_have = 0;
        w->lwin_ver = w->map_ver;
    }

    if (i < LW_N && k < LW_N)
    {
        unsigned b = i * LW_N + k;

        if (!(w->lwin_have >> b & 1))
        {
            chunkref r = chunk_find_ref(w, cx, cz);

            w->lwin[b] = r;
            w->lwin_have |= 1ull << b;
            if (r != 0) w->lwin_loaded |= 1ull << b;
            else w->lwin_loaded &= ~(1ull << b);
        }

        return chunk_ptr(w->lwin[b]);
    }

    return chunk_find(w, cx, cz);
}

/* the window moved to (cx, cz), keeping the lookups it already holds for
 * the chunks both windows cover */
static void lw_move(struct world *w, int cx, int cz)
{
    int dx = cx - w->lwin_cx, dz = cz - w->lwin_cz;
    uint64_t have = 0, loaded = 0;
    chunkref keep[LW_N * LW_N];

    if (w->lwin_ver == w->map_ver && dx > -LW_N && dx < LW_N && dz > -LW_N && dz < LW_N)
    {
        for (int i = 0; i < LW_N; ++i)
        {
            int oi = i + dx;

            if (oi < 0 || oi >= LW_N) continue;

            for (int k = 0; k < LW_N; ++k)
            {
                int ok = k + dz, ob = oi * LW_N + ok, b = i * LW_N + k;

                if (ok < 0 || ok >= LW_N || !(w->lwin_have >> ob & 1)) continue;

                keep[b] = w->lwin[ob];
                have |= 1ull << b;
                loaded |= (w->lwin_loaded >> ob & 1u) << b;
            }
        }

        for (int b = 0; b < LW_N * LW_N; ++b)
            if (have >> b & 1) w->lwin[b] = keep[b];
    }

    w->lwin_cx = cx;
    w->lwin_cz = cz;
    w->lwin_have = have;
    w->lwin_loaded = loaded;
    w->lwin_ver = w->map_ver;
}

/* a pass from block (x, z): the window stays while it holds every chunk
 * within 18 blocks, and moves to the chunk holding (x, z) otherwise */
static inline void lw_center(struct world *w, int x, int z)
{
    if (((x - 18) >> 4) < w->lwin_cx - LW_LO || ((x + 18) >> 4) >= w->lwin_cx - LW_LO + LW_N ||
        ((z - 18) >> 4) < w->lwin_cz - LW_LO || ((z + 18) >> 4) >= w->lwin_cz - LW_LO + LW_N)
        lw_move(w, x >> 4, z >> 4);
}

static int lw_box_loaded(struct world *w, int cx0, int cz0, int cx1, int cz1);

/* World.doChunksNearChunkExist (chunks_exist_box) through the window: every
 * chunk of the box is looked up once and then read as a bit, and the last
 * box's answer holds while the map does not change */
static int lw_near_exist(struct world *w, int x, int y, int z, int d)
{
    if (!(y + d >= 0 && y - d < 256)) return 0;
    if (w->is_remote) return 1;

    int cx0 = (x - d) >> 4, cz0 = (z - d) >> 4, cx1 = (x + d) >> 4, cz1 = (z + d) >> 4;

    if (w->lbox_ver == w->map_ver + 1 && w->lbox[0] == cx0 && w->lbox[1] == cz0 && w->lbox[2] == cx1 &&
        w->lbox[3] == cz1)
        return w->lbox_ok;

    w->lbox[0] = cx0;
    w->lbox[1] = cz0;
    w->lbox[2] = cx1;
    w->lbox[3] = cz1;
    w->lbox_ver = w->map_ver + 1;
    return w->lbox_ok = lw_box_loaded(w, cx0, cz0, cx1, cz1);
}

/* every chunk of the chunk box (cx0, cz0)..(cx1, cz1) loaded */
static int lw_box_loaded(struct world *w, int cx0, int cz0, int cx1, int cz1)
{
    unsigned i0 = (unsigned)(cx0 - w->lwin_cx + LW_LO), i1 = (unsigned)(cx1 - w->lwin_cx + LW_LO);
    unsigned k0 = (unsigned)(cz0 - w->lwin_cz + LW_LO), k1 = (unsigned)(cz1 - w->lwin_cz + LW_LO);

    if (w->lwin_ver == w->map_ver && i0 < LW_N && i1 < LW_N && k0 < LW_N && k1 < LW_N)
    {
        uint64_t row = (2ull << k1) - (1ull << k0), need = 0;

        for (unsigned i = i0; i <= i1; ++i) need |= row << (i * LW_N);
        if ((need & w->lwin_have) != need)
        {
            /* every chunk of the box looked up, a missing one too (a lookup
             * changes nothing), so the next check is bits */
            for (int cx = cx0; cx <= cx1; ++cx)
                for (int cz = cz0; cz <= cz1; ++cz) (void)lw_chunk(w, cx, cz);
        }
        return (need & w->lwin_loaded) == need;
    }

    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cz = cz0; cz <= cz1; ++cz)
            if (lw_chunk(w, cx, cz) == NULL) return 0;

    return 1;
}

/* world_get_light through the window */
static int lw_get_light(struct world *w, int type, int x, int y, int z)
{
    if (y < 0) y = 0;
    if (y >= 256) y = 255;
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z))) return LIGHT_DEFAULT_VALUE(type);

    struct chunk *c = lw_chunk(w, x >> 4, z >> 4);

    if (c == NULL) return w->no_generate ? 0 : LIGHT_DEFAULT_VALUE(type);

    return chunk_saved_light(c, type, x & 15, y, z & 15);
}

/* World.setLightValue: only a loaded chunk is written, and only inside the
 * world. */
static void lw_set_light(struct world *w, int type, int x, int y, int z, int value)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z))) return;
    if (y < 0 || y >= 256) return;

    struct chunk *c = lw_chunk(w, x >> 4, z >> 4);

    if (c == NULL) return;

    chunk_touch(w, c, x & 15, y, z & 15);
    chunk_set_light(c, type, x & 15, y, z & 15, value);
}

/* world_get_block through the window: a chunk that is not loaded is loaded
 * (chunk_for) as there */
static int lw_get_block(struct world *w, int x, int y, int z)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z) && y >= 0 && y < 256)) return BLK_AIR;

    struct chunk *c = lw_chunk(w, x >> 4, z >> 4);

    if (c == NULL) c = chunk_for(w, x, z);
    if (c == NULL) return BLK_AIR;

    return chunk_block(c, x & 15, y, z & 15);
}

/* The pass's reads with the cell's chunk in hand (hc, at chunk coordinates
 * hcx, hcz; NULL when it is not loaded): a read inside it and inside y 0..255
 * goes straight to the arrays, a read in a chunk the window holds reads it
 * inline, and any other read takes the window's functions, so the answers are
 * the same. habs is 1 when hc is NULL in a world that loads nothing
 * (no_generate) and the cell and its neighbors are inside the world's x and
 * z bounds: the chunk then stays unloaded through the entry, and a read
 * inside it is 0 (lw_get_light). Specialized on the light type (LP_INLINE). */
#define LP_INLINE static inline __attribute__((always_inline))
/* Chunk.getSavedLightValue (chunk_saved_light) for local x, z and y in 0..255 */
LP_INLINE int lp_saved(const struct chunk *c, int type, int lx, int y, int lz)
{
    if (!((c->mask >> (y >> 4)) & 1)) return y >= c->height[lz << 4 | lx] ? LIGHT_DEFAULT_VALUE(type) : 0;
    if (type == LIGHT_SKY && c->no_sky) return 0;

    const struct chunk_sec *s = chunk_sec_at(c, y >> 4);

    if (s == NULL) return 0;

    return nibble_get(chunk_sec_nib(s, type == LIGHT_SKY ? NIB_SKY : NIB_BLOCK), lx << 8 | lz << 4 | (y & 15));
}

LP_INLINE int lp_light(struct world *w, int type, const struct chunk *hc, int hcx, int hcz, int habs, int x, int y,
                       int z)
{
    if ((unsigned)y < 256u)
    {
        int cx = x >> 4, cz = z >> 4;

        if (cx == hcx && cz == hcz)
        {
            if (hc != NULL) return lp_saved(hc, type, x & 15, y, z & 15);
            if (habs) return 0;
        }

        /* a chunk the window already holds (lw_chunk's answer, inline) */
        unsigned i = (unsigned)(cx - w->lwin_cx + LW_LO), k = (unsigned)(cz - w->lwin_cz + LW_LO), b = i * LW_N + k;

        if (i < LW_N && k < LW_N && w->lwin_ver == w->map_ver && (w->lwin_have >> b & 1) && XZ_IN_RANGE(x) &&
            XZ_IN_RANGE(z))
        {
            chunkref r = w->lwin[b];

            if (r == 0) return w->no_generate ? 0 : LIGHT_DEFAULT_VALUE(type);
            return lp_saved(chunk_ptr(r), type, x & 15, y, z & 15);
        }
    }

    return lw_get_light(w, type, x, y, z);
}

LP_INLINE int lp_block(struct world *w, const struct chunk *hc, int hcx, int hcz, int habs, int x, int y, int z)
{
    if ((unsigned)y < 256u && (x >> 4) == hcx && (z >> 4) == hcz)
    {
        if (hc != NULL) return chunk_block(hc, x & 15, y, z & 15);
        if (habs) return BLK_AIR;
    }

    return lw_get_block(w, x, y, z);
}

/* The spread's neighbor order (x-1, x+1, y-1, y+1, z-1, z+1: Java's six
 * ifs) and, for each Facing (the order computeLightValue and the decrease
 * read in), its place in that order. */
static const int SPREAD_X[6] = {-1, 1, 0, 0, 0, 0};
static const int SPREAD_Y[6] = {0, 0, -1, 1, 0, 0};
static const int SPREAD_Z[6] = {0, 0, 0, 0, -1, 1};
static const int FACE_SPREAD[6] = {2, 3, 4, 5, 0, 1};
/* a spread neighbor's queue entry less the cell's (its level bits dropped):
 * every field stays inside 0..63 (a cell that spreads is within 16 of the
 * origin), so adding the offset is packing the neighbor */
static const int SPREAD_D[6] = {-1, 1, -64, 64, -4096, 4096};
/* a re-light queue entry whose turn is known to do nothing (a cell of a
 * chunk the world lacks with no neighbor in another chunk), above the level
 * bits */
#define LP_DEAD (1 << 22)
/* the set bits of a 6-bit spread mask (the generic x86-64 build calls
 * libgcc for __builtin_popcount) */
#define LP_B2(n) n, n + 1, n + 1, n + 2
#define LP_B4(n) LP_B2(n), LP_B2(n + 1), LP_B2(n + 1), LP_B2(n + 2)
static const uint8_t LP_POP6[64] = {LP_B4(0), LP_B4(1), LP_B4(1), LP_B4(2)};

/* LP_MEMO: in a world that loads nothing, a queued cell of a chunk it lacks
 * reads 0 for itself and its neighbors in that chunk, so its turn (whether it
 * pushes, and which neighbors, marked LP_DEAD or not) depends only on its
 * place and its neighbors' light in the loaded chunks next to it. Within one
 * pass (its origin fixed) and while no light is written, a turn is the same
 * each time the cell comes back: nw_scratch->lp_memo keeps it by the
 * cell's 18 bits, with the stamp it was made under (the stamp moves at the
 * pass's re-light and at each write) and the pattern (the bits the labs
 * branch below names). */
static inline uint32_t lp_memo_slot(int key)
{
    return ((uint32_t)key * 0x9E3779B1u) >> 18;
}

static __attribute__((noinline, cold)) void lp_memo_wrap(void)
{
    memset(nw_scratch->lp_memo, 0, sizeof nw_scratch->lp_memo);
    nw_scratch->memo_stamp = 1;
}

static inline uint32_t lp_memo_bump(void)
{
    /* 27 bits of stamp */
    if (++nw_scratch->memo_stamp >= 1u << 27) lp_memo_wrap();
    return nw_scratch->memo_stamp;
}

/* LP_TAIL's test: (px, pz) is in a chunk the pass treats as one the world
 * lacks (the labs of LP_CHUNK), through tab, the pass's answers for the 5 by
 * 5 chunks around its origin chunk (ocx, ocz), 0 not asked yet; a place
 * outside them answers 0 (counted as loaded: the count only ever errs up) */
static int lp_absent(struct world *w, uint8_t *tab, int ocx, int ocz, int px, int pz)
{
    int cx = px >> 4, cz = pz >> 4;
    unsigned i = (unsigned)(cx - ocx + 2), k = (unsigned)(cz - ocz + 2);

    if (i > 4 || k > 4) return 0;

    uint8_t *t = &tab[i * 5 + k];

    if (*t == 0)
        *t = lw_chunk(w, cx, cz) == NULL && w->no_generate && cx > -1875000 + 1 && cx < 1875000 - 1 &&
                     cz > -1875000 + 1 && cz < 1875000 - 1
                 ? 1
                 : 2;
    return *t == 1;
}

/* World.computeLightValue: the sky shortcut, the block's own light value, and
 * the six neighbors decayed by this block's opacity; hc is the cell's chunk
 * (NULL when it is not loaded: canBlockSeeTheSky then loads it, as Java's
 * does). The light of each neighbor it read goes to nb (spread order) and
 * its bit to *nbm. */
LP_INLINE int lp_compute(struct world *w, int type, const struct chunk *hc, int hcx, int hcz, int habs, int x, int y,
                         int z, int *nb, unsigned *nbm)
{
    int id;

    *nbm = 0;
    if (hc != NULL)
    {
        if (type == LIGHT_SKY && chunk_see_sky(hc, x & 15, y, z & 15)) return 15;
        id = lp_block(w, hc, hcx, hcz, habs, x, y, z) & 4095;
    }
    else if (w->no_generate)
    {
        /* nothing loads: the cell cannot see the sky and is air */
        id = BLK_AIR;
    }
    else
    {
        if (type == LIGHT_SKY && world_can_block_see_the_sky(w, x, y, z)) return 15;
        hc = lw_chunk(w, hcx, hcz);
        id = lp_block(w, hc, hcx, hcz, habs, x, y, z) & 4095;
    }

    int level = type == LIGHT_SKY ? 0 : BLOCKS[id].light;
    int op = BLOCKS[id].opacity;

    if (op >= 15 && BLOCKS[id].light > 0) op = 1;
    if (op < 1) op = 1;

    if (op >= 15) return 0;
    if (level >= 14) return level;

    /* a cell off its chunk's x and z edges with y in 1..254: every neighbor
     * is in its own chunk, which lp_light reads through lp_saved */
    if (hc != NULL && (unsigned)((x & 15) - 1) < 14u && (unsigned)((z & 15) - 1) < 14u && (unsigned)(y - 1) < 254u)
    {
        int lx = x & 15, lz = z & 15;

        for (int i = 0; i < 6; ++i)
        {
            int v = lp_saved(hc, type, lx + FACE_X[i], y + FACE_Y[i], lz + FACE_Z[i]);

            nb[FACE_SPREAD[i]] = v;
            *nbm |= 1u << FACE_SPREAD[i];

            int var12 = v - op;

            if (var12 > level) level = var12;
            if (level >= 14) return level;
        }

        return level;
    }

    for (int i = 0; i < 6; ++i)
    {
        int v = lp_light(w, type, hc, hcx, hcz, habs, x + FACE_X[i], y + FACE_Y[i], z + FACE_Z[i]);

        nb[FACE_SPREAD[i]] = v;
        *nbm |= 1u << FACE_SPREAD[i];

        int var12 = v - op;

        if (var12 > level) level = var12;
        if (level >= 14) return level;
    }

    return level;
}

static int abs_int(int v)
{
    return v < 0 ? -v : v;
}

/* the chunk of cell (px, pz) for one queue entry: the last entry's when it is
 * in the same chunk and the map has not changed since */
#define LP_CHUNK(px, pz)                                                                          \
    do                                                                                            \
    {                                                                                             \
        if (((px) >> 4) != lcx || ((pz) >> 4) != lcz || w->map_ver != lver)                       \
        {                                                                                         \
            lcx = (px) >> 4;                                                                      \
            lcz = (pz) >> 4;                                                                      \
            lc = lw_chunk(w, lcx, lcz);                                                           \
            lver = w->map_ver;                                                                    \
            labs = lc == NULL && w->no_generate && lcx > -1875000 + 1 && lcx < 1875000 - 1 &&     \
                   lcz > -1875000 + 1 && lcz < 1875000 - 1;                                       \
        }                                                                                         \
    }                                                                                             \
    while (0)

/* The flood of a server world's pass (lt_flood): the 4 by 4 chunks that
 * hold every cell within 18 blocks of the origin, looked up once. In a world
 * that generates (no_generate 0) and is not remote, lw_near_exist has found
 * the 17-block box loaded; every cell the flood queues is within 17 of the
 * origin (Manhattan) and every block it reads is a queued cell or a
 * neighbor of one the flood spreads from (within 16), so all of them are in
 * loaded chunks; only a light read of a neighbor at 18 may fall in a chunk
 * that is not loaded, which reads the type's default as lw_get_light does.
 * Nothing in the flood loads or drops a chunk (it writes light alone), so
 * the table is the map's answer for the whole flood. The table's reads take
 * x and z less the table's first block (0..63). In a world that loads
 * nothing (ng) a chunk it lacks is NULL in the table and reads as lp_pass
 * reads it there: light 0, air, its writes dropped (lt_flood's missing
 * cells). */
struct lp_tab
{
    struct chunk *c[16];
    int ng;
};

LP_INLINE struct chunk *lt_chunk(const struct lp_tab *t, int x, int z)
{
    return t->c[(x >> 4) << 2 | z >> 4];
}

/* lw_get_light over the table: y clamped to the world, a chunk that is not
 * loaded reads the default (0 in a world that loads nothing) */
LP_INLINE int lt_light(const struct lp_tab *t, int type, int x, int y, int z)
{
    if ((unsigned)y >= 256u) y = y < 0 ? 0 : 255;

    const struct chunk *c = lt_chunk(t, x, z);

    return c != NULL ? lp_saved(c, type, x & 15, y, z & 15) : t->ng ? 0 : LIGHT_DEFAULT_VALUE(type);
}

/* lw_get_block of a loaded cell, or air in a chunk a world that loads
 * nothing lacks */
LP_INLINE int lt_block(const struct lp_tab *t, int x, int y, int z)
{
    const struct chunk *c = lt_chunk(t, x, z);

    return (unsigned)y < 256u && (!t->ng || c != NULL) ? chunk_block(c, x & 15, y, z & 15) : BLK_AIR;
}

/* lw_set_light of a loaded cell */
LP_INLINE void lt_set_light(struct world *w, struct chunk *c, int type, int x, int y, int z, int value)
{
    if ((unsigned)y >= 256u) return;
    chunk_touch(w, c, x & 15, y, z & 15);
    chunk_set_light(c, type, x & 15, y, z & 15, value);
}

/* each Facing's step in a band's cell index (x << 8 | z << 4 | y & 15) */
static const int FACE_CELL[6] = {-1, 1, -16, 16, -256, 256};

/* lp_compute's value from the block's light and opacity on: level and op
 * as lp_compute makes them, or the value itself (set *done) */
LP_INLINE int lt_level_op(int type, int id, int *op, int *done)
{
    int level = type == LIGHT_SKY ? 0 : BLOCKS[id].light;
    int o = BLOCKS[id].opacity;

    if (o >= 15 && BLOCKS[id].light > 0) o = 1;
    if (o < 1) o = 1;

    *op = o;
    *done = o >= 15 || level >= 14;
    return o >= 15 ? 0 : level;
}

/* lt_light of the cell (lx, y, lz) of c or, off its x and z edges, of the
 * chunk beside it (the table's; x and z the table's) */
LP_INLINE int lt_light_at(const struct lp_tab *t, const struct chunk *c, int type, int x, int y, int z)
{
    if ((unsigned)y >= 256u) y = y < 0 ? 0 : 255;
    return c != NULL ? lp_saved(c, type, x & 15, y, z & 15) : t->ng ? 0 : LIGHT_DEFAULT_VALUE(type);
}

/* lp_compute for any other cell of the loaded chunk c (x, z the table's):
 * the six neighbors read from c where they are in it, from the table across
 * an x or z edge, every one kept (as lt_compute_inner) */
LP_INLINE int lt_compute(const struct lp_tab *t, int type, const struct chunk *c, int x, int y, int z, int *nb,
                         unsigned *nbm)
{
    int lx = x & 15, lz = z & 15;

    *nbm = 0;
    if (type == LIGHT_SKY && chunk_see_sky(c, lx, y, lz)) return 15;

    int op, done;
    int level = lt_level_op(type, ((unsigned)y < 256u ? chunk_block(c, lx, y, lz) : BLK_AIR) & 4095, &op, &done);

    if (done) return level;

    int v0 = lt_light_at(t, c, type, x, y - 1, z), v1 = lt_light_at(t, c, type, x, y + 1, z);
    int v2 = lt_light_at(t, lz > 0 ? c : lt_chunk(t, x, z - 1), type, x, y, z - 1);
    int v3 = lt_light_at(t, lz < 15 ? c : lt_chunk(t, x, z + 1), type, x, y, z + 1);
    int v4 = lt_light_at(t, lx > 0 ? c : lt_chunk(t, x - 1, z), type, x - 1, y, z);
    int v5 = lt_light_at(t, lx < 15 ? c : lt_chunk(t, x + 1, z), type, x + 1, y, z);

    nb[2] = v0;
    nb[3] = v1;
    nb[4] = v2;
    nb[5] = v3;
    nb[0] = v4;
    nb[1] = v5;
    *nbm = 63;

    int m = v0 > v1 ? v0 : v1;

    if (v2 > m) m = v2;
    if (v3 > m) m = v3;
    if (v4 > m) m = v4;
    if (v5 > m) m = v5;
    m -= op;
    return m > level ? m : level;
}

/* lp_pass from its first cell's want on, in a server world (the conditions
 * above; lp_pass checks them): the same drain and re-light, reads, writes,
 * queue and memo stamps, over the table. Such a world never takes the
 * LP_MEMO and LP_TAIL paths (they need no_generate), so they are not here. */
/* A cell of c off its chunk's x and z edges with y in 1..254, in a band
 * that is present with storage (and, for the sky, in a chunk with one):
 * its band, the band's light of the type and the cell's index in it. Its
 * x and z neighbors are in the band; the ones above and below are too
 * unless the cell is on the band's top or bottom row. */
struct lt_cell
{
    struct chunk_sec *sec;
    const uint8_t *a;
    int ci;
};

LP_INLINE int lt_inner(struct chunk *c, int type, int lx, int y, int lz, struct lt_cell *k)
{
    if ((unsigned)(lx - 1) >= 14u || (unsigned)(lz - 1) >= 14u || (unsigned)(y - 1) >= 254u) return 0;
    if (!(c->mask >> (y >> 4) & 1) || (type == LIGHT_SKY && c->no_sky)) return 0;

    struct chunk_sec *s = chunk_sec_at(c, y >> 4);

    if (s == NULL) return 0;
    k->sec = s;
    k->a = chunk_sec_nib(s, type == LIGHT_SKY ? NIB_SKY : NIB_BLOCK);
    k->ci = lx << 8 | lz << 4 | (y & 15);
    return 1;
}

/* the light (lt_light) and the block (lt_block) of the cell above (d 1) or
 * below (d -1) an inner cell: from its band when the band holds it */
LP_INLINE int lt_inner_v(const struct chunk *c, int type, const struct lt_cell *k, int lx, int y, int lz, int d)
{
    if (d < 0 ? (y & 15) > 0 : (y & 15) < 15) return nibble_get(k->a, k->ci + d);
    return lp_saved(c, type, lx, y + d, lz);
}

LP_INLINE int lt_inner_id(const struct chunk *c, const struct lt_cell *k, int lx, int y, int lz, int d)
{
    if (d < 0 ? (y & 15) > 0 : (y & 15) < 15) return chunk_sec_id(k->sec, k->ci + d);
    return chunk_block(c, lx, y + d, lz);
}

/* lp_compute for an inner cell, every neighbor kept (the early return at 14
 * returns the same level: no neighbor's v - op is over 14) */
LP_INLINE int lt_compute_inner(const struct chunk *c, int type, int lx, int y, int lz, const struct lt_cell *k,
                               int *nb, unsigned *nbm)
{
    *nbm = 0;
    if (type == LIGHT_SKY && chunk_see_sky(c, lx, y, lz)) return 15;

    int op, done;
    int level = lt_level_op(type, chunk_sec_id(k->sec, k->ci) & 4095, &op, &done);

    if (done) return level;

    const uint8_t *a = k->a;
    int ci = k->ci;
    int v0 = lt_inner_v(c, type, k, lx, y, lz, -1), v1 = lt_inner_v(c, type, k, lx, y, lz, 1);
    int v2 = nibble_get(a, ci - 16), v3 = nibble_get(a, ci + 16), v4 = nibble_get(a, ci - 256),
        v5 = nibble_get(a, ci + 256);

    nb[2] = v0;
    nb[3] = v1;
    nb[4] = v2;
    nb[5] = v3;
    nb[0] = v4;
    nb[1] = v5;
    *nbm = 63;

    int m = v0 > v1 ? v0 : v1;

    if (v2 > m) m = v2;
    if (v3 > m) m = v3;
    if (v4 > m) m = v4;
    if (v5 > m) m = v5;
    m -= op;
    return m > level ? m : level;
}

/* lt_set_light of an inner cell: its chunk's stamps as chunk_touch makes
 * them for a cell of a present band off the edges, and the band's nibble
 * (a shared band takes lt_set_light, which makes it the chunk's own) */
LP_INLINE void lt_set_inner(struct world *w, struct chunk *c, int type, int lx, int y, int lz, struct lt_cell *k,
                            int value)
{
    if (__builtin_expect(k->sec->shared != 0, 0))
    {
        lt_set_light(w, c, type, lx, y, lz, value);
        return;
    }

    uint64_t e = ++w->epoch;

    c->stamp = e;
    c->wseq = ++w->wseq;
    /* a constant array becomes the band's own slot first (world.h) */
    int f = type == LIGHT_SKY ? NIB_SKY : NIB_BLOCK;
    chunk_sec_nib_set(k->sec, f, k->ci, value);
    k->a = chunk_sec_nib(k->sec, f);
}

/* The flood's queue entries: x and z in the table's blocks (6 bits each), y
 * + 64 (9 bits: a queued cell is within 17 of an origin at y -17..272), the
 * level from bit 21; a neighbor's entry is the cell's plus its step. The
 * queue holds lp_pass's cells in lp_pass's order, so its length and its
 * overflow point are lp_pass's. */
#define LT_X(e) ((e) & 63)
#define LT_Z(e) ((e) >> 6 & 63)
#define LT_Y(e) (((e) >> 12 & 511) - 64)
#define LT_CELL(e) ((e) & 0x1fffff)
#define LT_SPAN 37
static const int LT_FACE_D[6] = {-4096, 4096, -64, 64, -1, 1};
static const int LT_SPREAD_D[6] = {-1, 1, -4096, 4096, -64, 64};

/* lp_pass from its first cell's want on, in a server world (the conditions
 * above; lp_pass checks them): the same drain and re-light, reads, writes,
 * queue and memo stamps, over the table. Such a world never takes the
 * LP_MEMO and LP_TAIL paths (they need no_generate), so they are not here.
 * An inner cell (lt_inner) reads and writes its band at once. */
LP_INLINE void lt_flood(struct world *w, int type, int ng, int x, int y, int z, int here, int want)
{
    int32_t *list = nw_scratch->light_list;
    int head = 0, tail = 0;
    int nb[6];
    unsigned nbm;
    struct lp_tab t;
    struct lt_cell k;
    int bx = (x - 18) >> 4, bz = (z - 18) >> 4, ox = x - bx * 16, oz = z - bz * 16;

    t.ng = ng;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            t.c[i << 2 | j] = bx + i <= (x + 18) >> 4 && bz + j <= (z + 18) >> 4 ? lw_chunk(w, bx + i, bz + j) : NULL;

    int origin = ox | oz << 6 | (y + 64) << 12;

    if (want > here)
    {
        list[tail++] = origin;
    }
    else
    {
        list[tail++] = origin | here << 21;

        while (head < tail)
        {
            int e = list[head++];
            int lxx = LT_X(e), lzz = LT_Z(e), py = LT_Y(e), level = e >> 21 & 15;
            struct chunk *c = lt_chunk(&t, lxx, lzz);
            int lx = lxx & 15, lz = lzz & 15;
            int cell = LT_CELL(e);

            /* a missing cell reads 0: at level 0 its write is dropped and
             * it spreads nothing */
            if (ng && c == NULL) continue;

            if (lt_inner(c, type, lx, py, lz, &k))
            {
                if (nibble_get(k.a, k.ci) != level) continue;

                lt_set_inner(w, c, type, lx, py, lz, &k, 0);

                if (level <= 0) continue;
                if (abs_int(lxx - ox) + abs_int(py - y) + abs_int(lzz - oz) >= 17) continue;

                /* the band as it is now (a shared one was copied) */
                (void)lt_inner(c, type, lx, py, lz, &k);

                for (int i = 0; i < 6; ++i)
                {
                    int ni = k.ci + FACE_CELL[i], id, v;

                    if (i < 2)
                    {
                        int d = i == 0 ? -1 : 1;

                        id = lt_inner_id(c, &k, lx, py, lz, d);
                        v = lt_inner_v(c, type, &k, lx, py, lz, d);
                    }
                    else
                    {
                        id = chunk_sec_id(k.sec, ni);
                        v = nibble_get(k.a, ni);
                    }

                    int op = BLOCKS[id & 4095].opacity;
                    int nlevel = level - (op < 1 ? 1 : op);

                    if (nlevel >= 0 && v == nlevel)
                    {
                        if (tail < 32768) list[tail++] = (cell + LT_FACE_D[i]) | nlevel << 21;
                        else nw_env->light.overflow = 1;
                    }
                }
                continue;
            }

            int got = lp_saved(c, type, lx, (unsigned)py < 256u ? py : py < 0 ? 0 : 255, lz);

            if (got != level) continue;

            lt_set_light(w, c, type, lx, py, lz, 0);

            if (level <= 0) continue;

            if (abs_int(lxx - ox) + abs_int(py - y) + abs_int(lzz - oz) >= 17) continue;

            for (int i = 0; i < 6; ++i)
            {
                int nx = lxx + FACE_X[i], ny = py + FACE_Y[i], nz = lzz + FACE_Z[i];
                int op = BLOCKS[lt_block(&t, nx, ny, nz) & 4095].opacity;
                int nlevel = level - (op < 1 ? 1 : op);

                if (nlevel >= 0 && lt_light(&t, type, nx, ny, nz) == nlevel)
                {
                    if (tail < 32768) list[tail++] = (cell + LT_FACE_D[i]) | nlevel << 21;
                    else nw_env->light.overflow = 1;
                }
            }
        }

        head = 0;
    }

    (void)lp_memo_bump();

    /* A queued cell's turn only reads: its light against computeLightValue
     * of its block, its column's height and its six neighbors' light, and
     * writes when they differ. Within the re-light, blocks and heights hold
     * (it writes light alone; a write that makes a section regenerates the
     * sky map, and then nothing is taken as known), so a cell found equal
     * (or just written) at write number E is equal again while neither it
     * nor a neighbor has been written since: its turn is skipped. The
     * stamps: the evaluation's number high, the last write's low, numbers
     * of earlier floods below base. */
    uint64_t *st = nw_scratch->lt_stamp;

    if (nw_scratch->flood_seq > 0xffffffffu - 100000u)
    {
        memset(st, 0, sizeof nw_scratch->lt_stamp);
        nw_scratch->flood_seq = 0;
    }

    uint32_t seq = ++nw_scratch->flood_seq, base = seq;
    int sx = LT_SPAN / 2 - ox, sz = LT_SPAN / 2 - oz, sy = LT_SPAN / 2 - y;
    /* ng: lp_pass's LP_TAIL, the queued cells of loaded chunks (nl): once
     * the queue is full and the overflow flagged, a queue holding none of
     * them ends the flood (a missing cell's turn could only push) */
    int nl = 0;

    for (int i = head; ng && i < tail; ++i)
        if (lt_chunk(&t, LT_X(list[i]), LT_Z(list[i])) != NULL) ++nl;

    while (head < tail)
    {
        if (ng && nl == 0 && nw_env->light.overflow && tail >= 32768 - 6) break;

        int e = list[head++];
        int lxx = LT_X(e), lzz = LT_Z(e), py = LT_Y(e);

        if (ng && lt_chunk(&t, lxx, lzz) == NULL)
        {
            /* lp_pass's missing cell: it reads 0 and is air, its write is
             * dropped, so its computeLightValue is its brightest neighbor in
             * another chunk less 1 (its own chunk's and the ones above and
             * below read 0), and while that is over 0 it pushes every
             * neighbor darker than it, each turn (nothing it does is known
             * from stamps). One off its chunk's x and z edges reads 0 all
             * round and does nothing. lp_pass's memo and LP_DEAD marks skip
             * turns that come out the same, LP_TAIL (above) ones that could
             * only push onto a full queue. */
            int lx = lxx & 15, lz = lzz & 15;

            if ((unsigned)(lx - 1) < 14u && (unsigned)(lz - 1) < 14u) continue;

            /* a turn depends on the neighbors' light alone, which holds
             * until the next write: the memo keeps it under the memo stamp
             * (bumped at each write), bits 0 to 5 the pushes, 6 to 11 the
             * ones into a loaded chunk, 12 that it pushes at all */
            int cell = LT_CELL(e);
            uint64_t *mt = &nw_scratch->lp_memo[lp_memo_slot(cell)];
            uint32_t stamp = nw_scratch->memo_stamp;

            if ((uint32_t)(*mt >> 37) == stamp && (int)(*mt >> 16 & 0x1fffff) == cell)
            {
                int pat = (int)(*mt & 0x1fff);

                if (!(pat >> 12)) continue;
                if (tail >= 32768 - 6)
                {
                    nw_env->light.overflow = 1;
                    continue;
                }
                for (int i = 0; i < 6; ++i)
                    if (pat >> i & 1) list[tail++] = cell + LT_SPREAD_D[i];
                nl += LP_POP6[pat >> 6 & 63];
                continue;
            }

            int v[6] = {0, 0, 0, 0, 0, 0};

            if (lx == 0) v[0] = lt_light(&t, type, lxx - 1, py, lzz);
            if (lx == 15) v[1] = lt_light(&t, type, lxx + 1, py, lzz);
            if (lz == 0) v[4] = lt_light(&t, type, lxx, py, lzz - 1);
            if (lz == 15) v[5] = lt_light(&t, type, lxx, py, lzz + 1);

            int m = v[0] > v[1] ? v[0] : v[1];

            if (v[4] > m) m = v[4];
            if (v[5] > m) m = v[5];

            int want2 = m - 1, pat = 0;

            if (want2 > 0 && abs_int(lxx - ox) + abs_int(py - y) + abs_int(lzz - oz) < 17)
            {
                /* only a neighbor across the chunk's x or z edge can be in
                 * a loaded chunk */
                pat = 1 << 12;
                for (int i = 0; i < 6; ++i)
                {
                    if (v[i] >= want2) continue;
                    pat |= 1 << i;
                    if (((i == 0 && lx == 0) || (i == 1 && lx == 15) || (i == 4 && lz == 0) || (i == 5 && lz == 15)) &&
                        lt_chunk(&t, lxx + SPREAD_X[i], lzz + SPREAD_Z[i]) != NULL)
                        pat |= 1 << (6 + i);
                }
            }
            *mt = (uint64_t)stamp << 37 | (uint64_t)cell << 16 | (uint64_t)pat;

            if (!pat) continue;
            if (tail >= 32768 - 6)
            {
                nw_env->light.overflow = 1;
                continue;
            }
            for (int i = 0; i < 6; ++i)
                if (pat >> i & 1) list[tail++] = cell + LT_SPREAD_D[i];
            nl += LP_POP6[pat >> 6 & 63];
            continue;
        }
        if (ng) --nl;

        /* the cell is within 17 of the origin on each axis, its neighbors 18 */
        int si = ((lxx + sx) * LT_SPAN + (lzz + sz)) * LT_SPAN + (py + sy);
        uint64_t s0 = st[si];
        uint32_t ev = (uint32_t)(s0 >> 32);

        if (ev >= base && (uint32_t)s0 <= ev && (uint32_t)st[si - 1] <= ev && (uint32_t)st[si + 1] <= ev &&
            (uint32_t)st[si - LT_SPAN] <= ev && (uint32_t)st[si + LT_SPAN] <= ev &&
            (uint32_t)st[si - LT_SPAN * LT_SPAN] <= ev && (uint32_t)st[si + LT_SPAN * LT_SPAN] <= ev)
            continue;

        struct chunk *c = lt_chunk(&t, lxx, lzz);
        int lx = lxx & 15, lz = lzz & 15;
        int got, want2;

        if (lt_inner(c, type, lx, py, lz, &k))
        {
            got = nibble_get(k.a, k.ci);
            want2 = lt_compute_inner(c, type, lx, py, lz, &k, nb, &nbm);

            if (want2 == got)
            {
                st[si] = (uint64_t)seq << 32 | (uint32_t)s0;
                continue;
            }

            /* the band is present: the write makes no section */
            lt_set_inner(w, c, type, lx, py, lz, &k, want2);
            (void)lp_memo_bump();
            ++seq;
            st[si] = (uint64_t)seq << 32 | seq;
        }
        else
        {
            got = lp_saved(c, type, lx, (unsigned)py < 256u ? py : py < 0 ? 0 : 255, lz);
            want2 = lt_compute(&t, type, c, lxx, py, lzz, nb, &nbm);

            if (want2 == got)
            {
                st[si] = (uint64_t)seq << 32 | (uint32_t)s0;
                continue;
            }

            uint16_t mask0 = c->mask;

            lt_set_light(w, c, type, lx, py, lz, want2);
            (void)lp_memo_bump();
            ++seq;
            st[si] = (uint64_t)seq << 32 | seq;
            if (c->mask != mask0)
            {
                base = ++seq;
                nbm = 0;
            }
        }

        if (want2 <= got) continue;
        if (abs_int(lxx - ox) + abs_int(py - y) + abs_int(lzz - oz) >= 17) continue;
        if (tail >= 32768 - 6)
        {
            nw_env->light.overflow = 1;
            continue;
        }

        int cell = LT_CELL(e);

        for (int i = 0; i < 6; ++i)
        {
            int v = nbm >> i & 1 ? nb[i] : lt_light(&t, type, lxx + SPREAD_X[i], py + SPREAD_Y[i], lzz + SPREAD_Z[i]);

            if (v >= want2) continue;
            list[tail++] = cell + LT_SPREAD_D[i];
            if (ng && lt_chunk(&t, lxx + SPREAD_X[i], lzz + SPREAD_Z[i]) != NULL) ++nl;
        }
    }

    nw_scratch->flood_seq = seq;
    nw_env->light.tail = tail;
}

/* every chunk of lt_flood's table loaded: then every cell the flood reads
 * (within 18 of the origin) is in a loaded chunk, which lp_pass reads and
 * writes as lt_flood does in any world (a cell of a missing chunk, its
 * LP_MEMO and LP_TAIL paths, never arises), so a client world's pass (or
 * any that loads nothing) floods through it too */
static int lt_table_loaded(struct world *w, int x, int z)
{
    int bx = (x - 18) >> 4, bz = (z - 18) >> 4, ex = (x + 18) >> 4, ez = (z + 18) >> 4;

    for (int cx = bx; cx <= ex; ++cx)
        for (int cz = bz; cz <= ez; ++cz)
            if (lw_chunk(w, cx, cz) == NULL) return 0;
    return 1;
}

/* lt_flood_ng's table: every chunk of it is one lp_pass tells a missing
 * one by being NULL (LP_CHUNK's labs, lp_absent: inside the world's chunk
 * bounds) */
static inline int lt_ng_ok(int x, int z)
{
    return (x - 18) >> 4 > -1875000 + 1 && (x + 18) >> 4 < 1875000 - 1 && (z - 18) >> 4 > -1875000 + 1 &&
           (z + 18) >> 4 < 1875000 - 1;
}

/* the flood outside lp_pass's body, one per type: its loops get registers of
 * their own */
static __attribute__((noinline)) void lt_flood_sky(struct world *w, int x, int y, int z, int here, int want)
{
    lt_flood(w, LIGHT_SKY, 0, x, y, z, here, want);
}

static __attribute__((noinline)) void lt_flood_block(struct world *w, int x, int y, int z, int here, int want)
{
    lt_flood(w, LIGHT_BLOCK, 0, x, y, z, here, want);
}

/* the flood of a world that loads nothing (lt_flood's missing cells) */
static __attribute__((noinline)) void lt_flood_ng(struct world *w, int type, int x, int y, int z, int here, int want)
{
    if (type == LIGHT_SKY) lt_flood(w, LIGHT_SKY, 1, x, y, z, here, want);
    else lt_flood(w, LIGHT_BLOCK, 1, x, y, z, here, want);
}

/* World.updateLightByType: the queued light pass. Its list is a plain array of
 * packed coordinates, offset 32 per axis and six bits of light level, and the
 * pass returns false outright when the 17 block box around (x, y, z) is not
 * fully loaded. */
LP_INLINE int lp_pass(struct world *w, int type, int x, int y, int z)
{
    lw_center(w, x, z);
    if (!lw_near_exist(w, x, y, z, 17)) return 0;

    int32_t *list = nw_scratch->light_list;
    int head = 0, tail = 0;
    int nb[6];
    unsigned nbm;
    const struct chunk *oc = lw_chunk(w, x >> 4, z >> 4);
    int here = lp_light(w, type, oc, x >> 4, z >> 4, 0, x, y, z);
    int want = lp_compute(w, type, oc, x >> 4, z >> 4, 0, x, y, z, nb, &nbm);
    const struct chunk *lc = NULL;
    int lcx = 0, lcz = 0, labs = 0;
    uint32_t lver = w->map_ver - 1;

    if (want != here && x - 18 >= -30000000 && x + 18 < 30000000 && z - 18 >= -30000000 && z + 18 < 30000000)
    {
        if ((!w->no_generate && !w->is_remote) || lt_table_loaded(w, x, z))
        {
            if (type == LIGHT_SKY) lt_flood_sky(w, x, y, z, here, want);
            else lt_flood_block(w, x, y, z, here, want);
            return 1;
        }
        if (w->no_generate && lt_ng_ok(x, z))
        {
            lt_flood_ng(w, type, x, y, z, here, want);
            return 1;
        }
    }

    /* nothing queued: the re-light's stamp and an empty queue */
    if (want == here)
    {
        (void)lp_memo_bump();
        nw_env->light.tail = 0;
        return 1;
    }

    if (want > here)
    {
        list[tail++] = 133152; /* x, y, z at offset 32, light level 0 */
    }
    else if (want < here)
    {
        list[tail++] = 133152 | here << 18;

        /* drain the level: every cell that still holds the level we are
         * removing goes to 0, and the ones an opacity step below it follow */
        while (head < tail)
        {
            int entry = list[head++];
            int px = (entry & 63) - 32 + x;
            int py = (entry >> 6 & 63) - 32 + y;
            int pz = (entry >> 12 & 63) - 32 + z;
            int level = entry >> 18 & 15;

            LP_CHUNK(px, pz);

            int got = lp_light(w, type, lc, lcx, lcz, labs, px, py, pz);

            if (got != level) continue;

            if (!labs) lw_set_light(w, type, px, py, pz, 0);

            if (level <= 0) continue;

            if (abs_int(px - x) + abs_int(py - y) + abs_int(pz - z) >= 17) continue;

            for (int i = 0; i < 6; ++i)
            {
                int nx = px + FACE_X[i], ny = py + FACE_Y[i], nz = pz + FACE_Z[i];
                int op = BLOCKS[lp_block(w, lc, lcx, lcz, labs, nx, ny, nz) & 4095].opacity;
                int nlevel = level - (op < 1 ? 1 : op);

                /* Java compares the unclamped var13 - max(1, opacity) against
                 * the neighbor's stored light, so a level that would go below 0
                 * never queues the neighbor; the stored light is never negative */
                if (nlevel >= 0 && lp_light(w, type, lc, lcx, lcz, labs, nx, ny, nz) == nlevel)
                {
                    if (tail < 32768) list[tail++] = (nx - x + 32) | (ny - y + 32) << 6 | (nz - z + 32) << 12 | nlevel << 18;
                    else nw_env->light.overflow = 1;
                }
            }
        }

        head = 0;
    }

    /* re-light: every queued cell takes computeLightValue, and when it grew,
     * its six neighbors that are darker join the queue */
    uint64_t *memo = nw_scratch->lp_memo;
    uint32_t stamp = lp_memo_bump();
    /* LP_TAIL: in a world that loads nothing, the queued cells of loaded
     * chunks (nl) are counted, so that once the queue is full and the
     * overflow is flagged, a queue holding none of them ends the pass */
    int track = w->no_generate != 0, nl = 0, ocx = x >> 4, ocz = z >> 4;
    uint8_t ctab[25];

    memset(ctab, 0, sizeof ctab);
    for (int k = head; track && k < tail; ++k)
        if (!(list[k] & LP_DEAD) && !lp_absent(w, ctab, ocx, ocz, (list[k] & 63) - 32 + x, (list[k] >> 12 & 63) - 32 + z))
            ++nl;

    while (head < tail)
    {
        if (track && nl == 0 && nw_env->light.overflow && tail >= 32768 - 6) break;

        int entry = list[head++];

        if (entry & LP_DEAD) continue;

        /* a cell of a chunk the world lacks whose turn is known: what it
         * pushes (LP_MEMO) */
        uint64_t *mt = &memo[lp_memo_slot(entry & 0x3ffff)];

        if ((uint32_t)(*mt >> 37) == stamp && (int)(*mt >> 19 & 0x3ffff) == (entry & 0x3ffff))
        {
            int pat = (int)(*mt & 0x7ffff);

            if (!(pat >> 18)) continue;
            if (tail >= 32768 - 6)
            {
                nw_env->light.overflow = 1;
                continue;
            }
            for (int i = 0; i < 6; ++i)
                if (pat >> i & 1) list[tail++] = ((entry & 0x3ffff) + SPREAD_D[i]) | (pat >> (6 + i) & 1 ? LP_DEAD : 0);
            nl += LP_POP6[pat >> 12 & 63];
            continue;
        }

        int px = (entry & 63) - 32 + x;
        int py = (entry >> 6 & 63) - 32 + y;
        int pz = (entry >> 12 & 63) - 32 + z;
        int want2;

        LP_CHUNK(px, pz);

        if (!labs) --nl;
        if (labs)
        {
            /* a cell of a chunk that is not loaded, in a world that loads
             * nothing: it reads 0 and is air (opacity 1, no light), so
             * computeLightValue is its brightest neighbor less 1 (the early
             * return at 14 gives the same), a neighbor in its own chunk or
             * above or below the world reads 0, and its write is dropped */
            int lx = px & 15, lz = pz & 15;
            int v[6] = {0, 0, 0, 0, 0, 0};

            if (lx == 0) v[0] = lp_light(w, type, lc, lcx, lcz, labs, px - 1, py, pz);
            if (lx == 15) v[1] = lp_light(w, type, lc, lcx, lcz, labs, px + 1, py, pz);
            if (lz == 0) v[4] = lp_light(w, type, lc, lcx, lcz, labs, px, py, pz - 1);
            if (lz == 15) v[5] = lp_light(w, type, lc, lcx, lcz, labs, px, py, pz + 1);

            int m = v[0] > v[1] ? v[0] : v[1];

            if (v[4] > m) m = v[4];
            if (v[5] > m) m = v[5];
            want2 = m - 1;

            /* the turn's pushes, from the neighbors' light alone: bit 18 it
             * pushes, bits 0 to 5 the neighbors, 6 to 11 their LP_DEAD, 12 to
             * 17 the ones in a loaded chunk (LP_TAIL) */
            int pat = 0;

            if (want2 > 0 && abs_int(px - x) + abs_int(py - y) + abs_int(pz - z) < 17)
            {
                /* a neighbor inside this chunk off its x and z edges reads 0
                 * all round: its turn in the queue ends at once, so it is
                 * marked */
                int inner = lx > 0 && lx < 15 && lz > 0 && lz < 15;

                pat = 1 << 18;
                for (int i = 0; i < 6; ++i)
                {
                    if (v[i] >= want2) continue;

                    int ix = lx + SPREAD_X[i], iz = lz + SPREAD_Z[i];
                    int dead = i < 2 ? ix > 0 && ix < 15 && lz > 0 && lz < 15
                             : i >= 4 ? iz > 0 && iz < 15 && lx > 0 && lx < 15 : inner;
                    /* a neighbor in this chunk is in a chunk the world lacks */
                    int loaded = (ix < 0 || ix > 15 || iz < 0 || iz > 15) &&
                                 !lp_absent(w, ctab, ocx, ocz, px + SPREAD_X[i], pz + SPREAD_Z[i]);

                    pat |= 1 << i | dead << (6 + i) | loaded << (12 + i);
                }
            }
            *mt = (uint64_t)stamp << 37 | (uint64_t)(entry & 0x3ffff) << 19 | (uint64_t)pat;

            if (!pat) continue;
            if (tail >= 32768 - 6)
            {
                nw_env->light.overflow = 1;
                continue;
            }
            for (int i = 0; i < 6; ++i)
                if (pat >> i & 1) list[tail++] = ((entry & 0x3ffff) + SPREAD_D[i]) | (pat >> (6 + i) & 1 ? LP_DEAD : 0);
            nl += LP_POP6[pat >> 12 & 63];
            continue;
        }

        int got = lp_light(w, type, lc, lcx, lcz, labs, px, py, pz);
        want2 = lp_compute(w, type, lc, lcx, lcz, labs, px, py, pz, nb, &nbm);

        if (want2 == got) continue;

        /* the write makes the cell's section when it is missing, which
         * regenerates its chunk's sky map: the neighbors are read again then
         * (a cell of a chunk that is not loaded is not written) */
        uint16_t mask0 = lc != NULL ? lc->mask : 0;

        if (!labs)
        {
            lw_set_light(w, type, px, py, pz, want2);
            stamp = lp_memo_bump();
        }

        /* the spread's neighbor order matches Java's six ifs (x-1, x+1,
         * y-1, y+1, z-1, z+1), NOT the Facing table the decrease uses: the
         * queue's overflow point depends on the order */
        if (want2 <= got) continue;
        if (abs_int(px - x) + abs_int(py - y) + abs_int(pz - z) >= 17) continue;
        if (tail >= 32768 - 6)
        {
            nw_env->light.overflow = 1;
            continue;
        }

        if (lc != NULL ? lc->mask != mask0 : !labs) nbm = 0;

        for (int i = 0; i < 6; ++i)
        {
            int nx = px + SPREAD_X[i], ny = py + SPREAD_Y[i], nz = pz + SPREAD_Z[i];
            int v = nbm >> i & 1 ? nb[i] : lp_light(w, type, lc, lcx, lcz, labs, nx, ny, nz);

            if (v >= want2) continue;
            list[tail++] = (entry & 0x3ffff) + SPREAD_D[i];
            if (track && !lp_absent(w, ctab, ocx, ocz, nx, nz)) ++nl;
        }
    }

    nw_env->light.tail = tail;
    return 1;
}
#undef LP_CHUNK

int world_light_update_raw(struct world *w, int type, int x, int y, int z)
{
    return type == LIGHT_SKY ? lp_pass(w, LIGHT_SKY, x, y, z) : lp_pass(w, LIGHT_BLOCK, x, y, z);
}

/* The client world's deferred light (lightdefer.h): the environment's
 * lightdefer when w is a client world and the list is not being run. */
static inline struct lightdefer *cw_defer(const struct world *w)
{
    struct lightdefer *d = nw_env->light.defer;

    return d != NULL && w->is_remote && !d->in_run ? d : NULL;
}

/* the operation appended to the pending list; 0 when w's light is not
 * deferred (the caller runs it) */
static int cw_defer_op(struct world *w, int what, int type, int x, int y, int z)
{
    struct lightdefer *d = cw_defer(w);

    if (d == NULL) return 0;
    if (d->n > 0 && d->w != w) lightdefer_sync_cause(d, LD_CAUSE_WORLD);
    if (d->n == LD_CAP) lightdefer_sync_cause(d, LD_CAUSE_FULL);
    d->w = w;
    d->ops[d->n++] = (struct ld_op){what, type, x, y, z};
    if (d->n > d->st.max_pending) d->st.max_pending = d->n;
    if (what == LC_O_UPDATE) d->st.updates++;
    else d->st.relights++;
    return 1;
}

/* the pending list run before the host reads or writes what it touches */
static void cw_sync(struct world *w, int kind)
{
    struct lightdefer *d = cw_defer(w);

    if (d == NULL || d->n == 0 || !(d->syncs & kind)) return;
    lightdefer_sync_cause(d, kind == LD_SYNC_WRITE ? LD_CAUSE_WRITE : LD_CAUSE_READ);
}

void world_light_sync(struct world *w)
{
    cw_sync(w, LD_SYNC_WRITE);
}

/* the light engine, one sub-mark for the profiler (phase.h); a capture
 * (lightcap.h) sees every call; a client world's is deferred (lightdefer.h) */
static int update_light_by_type(struct world *w, int type, int x, int y, int z)
{
    if (__builtin_expect(nw_env->light.defer != NULL, 0) && cw_defer_op(w, LC_O_UPDATE, type, x, y, z)) return 0;
    if (__builtin_expect(nw_env->light.cap != NULL, 0)) return lightcap_update(nw_env->light.cap, w, type, x, y, z);
    if (!PHASE_PROF_ON()) return world_light_update_raw(w, type, x, y, z);
    phase_sub_begin_(PS_LIGHT);
    int r = world_light_update_raw(w, type, x, y, z);
    phase_sub_end_(PS_LIGHT);
    return r;
}

/* lp_memo_bump n times: the stamp as n bumps leave it, and the memo
 * cleared if one of them wrapped (nothing reads the memo between them) */
static void lp_memo_bump_n(uint32_t n)
{
    uint32_t s = nw_scratch->memo_stamp;

    if (n == 0) return;
    if (s + n >= 1u << 27)
    {
        memset(nw_scratch->lp_memo, 0, sizeof nw_scratch->lp_memo);
        nw_scratch->memo_stamp = 1 + (s + n - (1u << 27));
        return;
    }
    nw_scratch->memo_stamp = s + n;
}

/* lp_light of a cell beside a loaded chunk's x or z edge, y in 0..255 and
 * x and z inside the world: its chunk through the window */
LP_INLINE int lw_light_beside(struct world *w, int type, int x, int y, int z)
{
    const struct chunk *c = lw_chunk(w, x >> 4, z >> 4);

    return c != NULL ? lp_saved(c, type, x & 15, y, z & 15) : w->no_generate ? 0 : LIGHT_DEFAULT_VALUE(type);
}

/* lp_compute for a cell of the loaded chunk c at y 1..254 off the inner
 * cells (on an x or z edge, or in a band without storage), every neighbor
 * kept (as lt_compute_inner): the ones in c read from it, the others
 * beside it through the window */
LP_INLINE int lt_compute_edge(struct world *w, int type, const struct chunk *c, int x, int y, int z, int *nb,
                              unsigned *nbm)
{
    int lx = x & 15, lz = z & 15;

    *nbm = 0;
    if (type == LIGHT_SKY && chunk_see_sky(c, lx, y, lz)) return 15;

    int op, done;
    int level = lt_level_op(type, chunk_block(c, lx, y, lz) & 4095, &op, &done);

    if (done) return level;

    int v0 = lp_saved(c, type, lx, y - 1, lz), v1 = lp_saved(c, type, lx, y + 1, lz);
    int v2 = lz > 0 ? lp_saved(c, type, lx, y, lz - 1) : lw_light_beside(w, type, x, y, z - 1);
    int v3 = lz < 15 ? lp_saved(c, type, lx, y, lz + 1) : lw_light_beside(w, type, x, y, z + 1);
    int v4 = lx > 0 ? lp_saved(c, type, lx - 1, y, lz) : lw_light_beside(w, type, x - 1, y, z);
    int v5 = lx < 15 ? lp_saved(c, type, lx + 1, y, lz) : lw_light_beside(w, type, x + 1, y, z);

    nb[2] = v0;
    nb[3] = v1;
    nb[4] = v2;
    nb[5] = v3;
    nb[0] = v4;
    nb[1] = v5;
    *nbm = 63;

    int m = v0 > v1 ? v0 : v1;

    if (v2 > m) m = v2;
    if (v3 > m) m = v3;
    if (v4 > m) m = v4;
    if (v5 > m) m = v5;
    m -= op;
    return m > level ? m : level;
}

/* lp_pass after its box test, in a server world (the conditions lp_pass
 * checks before lt_flood): the first cell's light and computeLightValue
 * (from its band at once for an inner cell, lt_compute_inner), then the
 * flood or, when they agree, the memo stamp and the queue's tail as lp_pass
 * leaves them */
LP_INLINE void pass_boxed(struct world *w, int type, int x, int y, int z)
{
    struct chunk *oc = lw_chunk(w, x >> 4, z >> 4);
    int nb[6];
    unsigned nbm;
    int here, want;
    struct lt_cell k;

    if (oc != NULL && lt_inner(oc, type, x & 15, y, z & 15, &k))
    {
        here = nibble_get(k.a, k.ci);
        want = lt_compute_inner(oc, type, x & 15, y, z & 15, &k, nb, &nbm);
    }
    else if (oc != NULL && (unsigned)(y - 1) < 254u)
    {
        here = lp_saved(oc, type, x & 15, y, z & 15);
        want = lt_compute_edge(w, type, oc, x, y, z, nb, &nbm);
    }
    else
    {
        here = lp_light(w, type, oc, x >> 4, z >> 4, 0, x, y, z);
        want = lp_compute(w, type, oc, x >> 4, z >> 4, 0, x, y, z, nb, &nbm);
    }

    if (want != here)
    {
        if (type == LIGHT_SKY) lt_flood_sky(w, x, y, z, here, want);
        else lt_flood_block(w, x, y, z, here, want);
        return;
    }
    (void)lp_memo_bump();
    nw_env->light.tail = 0;
}

static void sky_pass_boxed(struct world *w, int x, int y, int z)
{
    pass_boxed(w, LIGHT_SKY, x, y, z);
}

static void block_pass_boxed(struct world *w, int x, int y, int z)
{
    pass_boxed(w, LIGHT_BLOCK, x, y, z);
}

/* World.func_147451_t: the sky pass (a world with a sky) then the block
 * pass at one cell, 1 when either got past its box test. In a server world
 * (lp_pass's lt_flood conditions) with no light capture or profile the two
 * share the test (the same cell, and the sky pass loads nothing). */
static inline int light_pair_server(const struct world *w, int x, int z)
{
    return !(nw_env->light.cap != NULL || PHASE_PROF_ON() || w->no_generate || w->is_remote ||
             !(x - 18 >= -30000000 && x + 18 < 30000000 && z - 18 >= -30000000 && z + 18 < 30000000));
}

static int light_pair(struct world *w, int x, int y, int z)
{
    if (!light_pair_server(w, x, z))
    {
        int changed = 0;

        if (w->dim == 0) changed |= update_light_by_type(w, LIGHT_SKY, x, y, z);
        changed |= update_light_by_type(w, LIGHT_BLOCK, x, y, z);
        return changed;
    }

    lw_center(w, x, z);
    if (!lw_near_exist(w, x, y, z, 17)) return 0;
    if (w->dim == 0) sky_pass_boxed(w, x, y, z);
    block_pass_boxed(w, x, y, z);
    return 1;
}

/* updateLightByType(Sky) at (x, y, z) for y from y0 up to y1 (exclusive),
 * low to high. In a server world (lp_pass's lt_flood conditions) with no
 * light capture or profile, the passes share their box test: for y 0..272
 * lw_near_exist's answer depends on x and z alone, and nothing in a pass
 * loads or drops a chunk, so it is taken once for the column; a column
 * whose box is not loaded makes every pass return at once. */
static inline int sky_column_server(const struct world *w, int x, int z, int y0, int y1)
{
    return !(nw_env->light.cap != NULL || PHASE_PROF_ON() || w->no_generate || w->is_remote || y0 < 0 ||
             y1 > 273 || !(x - 18 >= -30000000 && x + 18 < 30000000 && z - 18 >= -30000000 && z + 18 < 30000000));
}

static void sky_column(struct world *w, int x, int z, int y0, int y1)
{
    if (y0 >= y1) return;
    if (!sky_column_server(w, x, z, y0, y1))
    {
        for (int y = y0; y < y1; ++y) update_light_by_type(w, LIGHT_SKY, x, y, z);
        return;
    }

    lw_center(w, x, z);
    if (!lw_near_exist(w, x, y0, z, 17)) return;

    /* a cell that sees the sky computes 15 (lp_compute); one that already
     * holds 15 (lp_saved: a band with no storage holds 15 there) is a pass
     * that bumps the memo stamp and leaves the tail 0, nothing more. The
     * column's chunk is loaded (the box) and stays (a pass loads nothing);
     * its height is read again at each cell (a flood that makes a section
     * regenerates the height map) */
    const struct chunk *c = lw_chunk(w, x >> 4, z >> 4);
    int lx = x & 15, lz = z & 15, y = y0;
    uint32_t bumps = 0;

    for (; y < y1 && y < 256; ++y)
    {
        if (y >= c->height[lz << 4 | lx] && lp_saved(c, LIGHT_SKY, lx, y, lz) == 15)
        {
            ++bumps;
            continue;
        }
        lp_memo_bump_n(bumps);
        bumps = 0;
        sky_pass_boxed(w, x, y, z);
    }
    lp_memo_bump_n(bumps);
    if (bumps > 0) nw_env->light.tail = 0;
    for (; y < y1; ++y) sky_pass_boxed(w, x, y, z);
}

/* Chunk.propagateSkylightOcclusion */
static void propagate_skylight_occlusion(struct chunk *c, int x, int z)
{
    c->update_skylight_columns[x + z * 16] = 1;
    c->gap_lighting_updated = 1;
}

/* World.markBlocksDirtyVertical: the sky light pass for every block of the
 * column band, low to high; only the render half is left out. The Nether's
 * provider.hasNoSky makes the whole pass a no-op. */
static void mark_blocks_dirty_vertical(struct world *w, int x, int z, int y0, int y1)
{
    if (w->dim != 0) return;

    if (y0 > y1)
    {
        int t = y0;
        y0 = y1;
        y1 = t;
    }

    sky_column(w, x, z, y0, y1 + 1);
}

/* Chunk.updateSkylightNeighborHeight */
static void update_skylight_neighbor_height(struct world *w, int x, int z, int y0, int y1)
{
    if (w->dim != 0) return;
    if (y1 <= y0) return;
    /* on sky_column's server path its 17-block box holds this 16-block one,
     * and a pass that fails the box writes nothing: that box is the test */
    if (sky_column_server(w, x, z, y0, y1))
    {
        sky_column(w, x, z, y0, y1);
        return;
    }
    lw_center(w, x, z);
    if (lw_near_exist(w, x, 0, z, 16)) sky_column(w, x, z, y0, y1);
}

/* Chunk.relightBlock: recompute one column's height and sky light after its
 * block changed, then hand the band that moved to the light engine. */
static void relight_block(struct world *w, struct chunk *c, int x, int y, int z)
{
    int old = c->height[z << 4 | x] & 255;
    int top = old;

    if (y > old) top = y;

    while (top > 0 && chunk_opacity(c, x, top - 1, z) == 0) --top;

    if (top == old) return;

    mark_blocks_dirty_vertical(w, x + c->cx * 16, z + c->cz * 16, top, old);
    /* the column's calls run before its height and sky are written
     * (lightdefer.h) */
    cw_sync(w, LD_SYNC_WRITE);
    c->height[z << 4 | x] = top;

    int wx = c->cx * 16 + x, wz = c->cz * 16 + z;

    /* the sky half of relightBlock is the part provider.hasNoSky skips; the
     * height map write and heightMapMinimum run in every dimension */
    if (!c->no_sky)
    {
        if (top < old)
            for (int i = top; i < old; ++i)
            {
                if (section_present(c, i)) chunk_set_sky(c, CELL(x, i, z), 15);
            }
        else
            for (int i = old; i < top; ++i)
            {
                if (section_present(c, i)) chunk_set_sky(c, CELL(x, i, z), 0);
            }

        int light = 15;

        while (top > 0 && light > 0)
        {
            --top;

            int op = chunk_opacity(c, x, top, z);

            if (op == 0) op = 1;
            light -= op;
            if (light < 0) light = 0;

            if (section_present(c, top)) chunk_set_sky(c, CELL(x, top, z), light);
        }
    }

    int height = c->height[z << 4 | x];
    int y0 = old, y1 = height;

    if (height < old)
    {
        y0 = height;
        y1 = old;
    }

    if (height < c->height_min) c->height_min = height;

    update_skylight_neighbor_height(w, wx - 1, wz, y0, y1);
    update_skylight_neighbor_height(w, wx + 1, wz, y0, y1);
    update_skylight_neighbor_height(w, wx, wz - 1, y0, y1);
    update_skylight_neighbor_height(w, wx, wz + 1, y0, y1);
    update_skylight_neighbor_height(w, wx, wz, y0, y1);
}

/* BlockLeaves.breakBlock and BlockLog.breakBlock: when a leaf or a log is
 * replaced, every leaf around it is marked persistent (metadata bit 8) so it
 * does not decay once the tree is gone. BlockLeaves walks 1 block out over
 * every leaf, BlockLog 4 blocks out over the leaves that are not marked yet;
 * both guard on checkChunksExist and both write through
 * setBlockMetadataWithNotify. */
static void block_break_tail(struct world *w, int x, int y, int z, int old_id)
{
    int radius, marked_only;

    /* BlockRailBase.breakBlock and the others blockcb.c ports ran first, as
     * the SET_BLOCK step's own call (bwl_on_broken) */
    if (old_id == BLK_LEAVES_ID || old_id == BLK_LEAVES2_ID)
    {
        radius = 1;
        marked_only = 0;
    }
    else if (old_id == BLK_LOG_ID || old_id == BLK_LOG2_ID)
    {
        radius = 4;
        marked_only = 1;
    }
    else if (old_id == 144)
    {
        /* BlockSkull.breakBlock: a skull without the bit-8 meta drops its
         * item at the tile entity's type. The tile entity is still here: the
         * removal runs after this, and the old metadata is still stored. */
        int m = world_get_meta(w, x, y, z);

        if ((m & 8) == 0)
        {
            struct tile_entity *te = world_tile_entity(w, x, y, z);

            place_skull_break_drop(w, x, y, z, te != NULL && te->kind == TE_SKULL ? te->skull_type : 0);
        }

        return;
    }
    else
    {
        return;
    }

    int out = radius + 1;

    if (!chunks_exist_box(w, x - out, y - out, z - out, x + out, y + out, z + out)) return;

    for (int dx = -radius; dx <= radius; ++dx)
    {
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                int px = x + dx, py = y + dy, pz = z + dz;
                int id = world_get_block(w, px, py, pz);

                /* Material.leaves, shared by both leaf blocks */
                if (BLOCKS[id & 4095].material != BLOCKS[BLK_LEAVES_ID].material) continue;

                int meta = world_get_meta(w, px, py, pz);

                if (marked_only && (meta & 8) != 0) continue;
                world_set_meta_quiet(w, px, py, pz, meta | 8);
            }
        }
    }
}

/* BlockChest.func_149954_e, through BlockChest.onBlockAdded: the facing a new
 * chest takes from its neighbours, written as a metadata change with flag 3.
 * Chunk.func_150807_a calls the new block's onBlockAdded after its light work;
 * only the chest has a non-empty one. Vanilla compares the neighbours against
 * `this`, so a chest only pairs with its own kind (chest with chest, trapped
 * chest with trapped chest): the own block id rides in as self. */
static void chest_facing(struct world *w, int x, int y, int z, int self)
{
    int back = world_get_block(w, x, y, z - 1);
    int front = world_get_block(w, x, y, z + 1);
    int side1 = world_get_block(w, x - 1, y, z);
    int side2 = world_get_block(w, x + 1, y, z);
    int facing;

    if (back != self && front != self)
    {
        if (side1 != self && side2 != self)
        {
            facing = 3;

            if (opaque_cube(back) && !opaque_cube(front)) facing = 3;
            if (opaque_cube(front) && !opaque_cube(back)) facing = 2;
            if (opaque_cube(side1) && !opaque_cube(side2)) facing = 5;
            if (opaque_cube(side2) && !opaque_cube(side1)) facing = 4;
        }
        else
        {
            int bx = side1 == self ? x - 1 : x + 1;
            int bz1 = world_get_block(w, bx, y, z - 1);
            int fz1 = world_get_block(w, bx, y, z + 1);

            facing = 3;

            int meta = side1 == self ? world_get_meta(w, x - 1, y, z) : world_get_meta(w, x + 1, y, z);

            if (meta == 2) facing = 2;
            if ((opaque_cube(back) || opaque_cube(bz1)) && !opaque_cube(front) && !opaque_cube(fz1)) facing = 3;
            if ((opaque_cube(front) || opaque_cube(fz1)) && !opaque_cube(back) && !opaque_cube(bz1)) facing = 2;
        }
    }
    else
    {
        int bz = back == self ? z - 1 : z + 1;
        int bx1 = world_get_block(w, x - 1, y, bz);
        int bx2 = world_get_block(w, x + 1, y, bz);

        facing = 5;

        int meta = back == self ? world_get_meta(w, x, y, z - 1) : world_get_meta(w, x, y, z + 1);

        if (meta == 4) facing = 4;
        if ((opaque_cube(side1) || opaque_cube(bx1)) && !opaque_cube(side2) && !opaque_cube(bx2)) facing = 5;
        if ((opaque_cube(side2) || opaque_cube(bx2)) && !opaque_cube(side1) && !opaque_cube(bx1)) facing = 4;
    }

    BWL_EMIT(BWL_SET_META, w, x, y, z, facing, 3);
}

/* BlockChest.onBlockAdded: the new chest's own facing, then the facing of every
 * chest beside it, in vanilla's order (z - 1, z + 1, x - 1, x + 1), each of
 * which writes its own metadata again. Chunk.func_150807_a calls this for the
 * block it just stored; only the chest has a non-empty onBlockAdded. */
static int chest_added_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z, self = f->op.a[0];
    int *side = f->u.l;

    if (f->pc == 0)
    {
        chest_facing(w, x, y, z, self);
        f->pc = 1;
        return 0;
    }

    if (f->pc == 1)
    {
        side[0] = world_get_block(w, x, y, z - 1);
        side[1] = world_get_block(w, x, y, z + 1);
        side[2] = world_get_block(w, x - 1, y, z);
        side[3] = world_get_block(w, x + 1, y, z);
        f->pc = 2;
    }

    static const int off[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};

    while (f->i < 4)
    {
        int k = f->i++;

        if (side[k] == self)
        {
            chest_facing(w, x + off[k][0], y, z + off[k][1], self);
            return 0;
        }
    }

    return 1;
}

/* Chunk.func_150807_a: the block write itself, with its effect on the section,
 * the height map and the sky light, and the callbacks vanilla runs inside it:
 * the old block's breakBlock between the store and the metadata, then after
 * the relight the old block's tile entity check, the new block's
 * onBlockAdded (blockcb.c, and the chest here) and its own tile entity. */
/* Chunk.func_150807_a's head, up to the old block's breakBlock: 0 when
 * nothing is written, 1 when the id is stored (the SET_BLOCK step then calls
 * breakBlock and continues in chunk_set_block_mid). */
static int chunk_set_block_head(struct world *w, struct chunk *c, int x, int y, int z, int id, int meta,
                                int *was_height_out, int *made_section_out)
{
    int col = z << 4 | x;

    if (y >= c->precip[col] - 1) c->precip[col] = -999;

    int was_height = c->height[col];
    int was_id = chunk_block(c, x, y, z);
    int was_meta = chunk_meta(c, x, y, z);

    if (was_id == id && was_meta == meta) return 0;

    chunk_touch(w, c, x, y, z);
    int made_section = 0;

    if (!section_present(c, y))
    {
        if (id == BLK_AIR) return 0;

        c->mask |= (uint16_t)(1 << (y >> 4));
        made_section = y >= was_height;
    }

    /* ExtendedBlockStorage.func_150818_a's tickRefCount: the needsRandomTick
     * gate WorldServer.func_147456_g reads. The counter, not a flag: replacing
     * one of several ticking blocks in a section leaves it above 0. */
    if (BLOCKS[was_id & 4095].tick_randomly) --c->sections_ticking[y >> 4];
    if (BLOCKS[id & 4095].tick_randomly) ++c->sections_ticking[y >> 4];

    chunk_set_id(c, CELL(x, y, z), id);
    *was_height_out = was_height;
    *made_section_out = made_section;
    return 1;
}

/* Chunk.func_150807_a's height map and sky light after the store: a new
 * section regenerates the sky map, otherwise the column relights where the
 * height moved and the occlusion spreads when the opacity changed. */
static void chunk_write_relight(struct world *w, struct chunk *c, int x, int y, int z, int id, int was_id,
                                int was_height, int made_section)
{
    if (made_section)
    {
        generate_skylight_map(c);
    }
    else
    {
        int new_op = BLOCKS[id & 4095].opacity;
        int old_op = BLOCKS[was_id & 4095].opacity;

        if (new_op > 0)
        {
            if (y >= was_height) relight_block(w, c, x, y + 1, z);
        }
        else if (y == was_height - 1)
        {
            relight_block(w, c, x, y, z);
        }

        /* the light test reads what relight_block's calls wrote
         * (lightdefer.h) */
        if (new_op > old_op) cw_sync(w, LD_SYNC_READ);
        if (new_op != old_op &&
            (new_op < old_op || chunk_saved_light(c, LIGHT_SKY, x, y, z) > 0 ||
             chunk_saved_light(c, LIGHT_BLOCK, x, y, z) > 0))
            propagate_skylight_occlusion(c, x, z);
    }
}

/* Chunk.func_150807_a after breakBlock: 0 when breakBlock replaced the cell
 * (the write aborts), 1 when the new block's onBlockAdded is next. */
static int chunk_set_block_mid(struct world *w, struct chunk *c, int x, int y, int z, int id, int meta, int was_id,
                               int was_height, int made_section)
{
    /* Chunk.func_150807_a stores the block, then runs the old block's
     * breakBlock, and only then stores the metadata (a nibble array, so
     * vanilla keeps meta & 15; a placement can pass wider values, the
     * anvil's damage bits) */
    block_break_tail(w, c->cx * 16 + x, y, c->cz * 16 + z, was_id);
    /* the block that was there gets its breakBlock: BlockContainer.breakBlock
     * removes the tile entity (BlockChest.breakBlock also drops the chest's
     * contents as entities, through the chest block's own Random); the world
     * path invalidates it and takes it off the world lists, so the walk never
     * sees it again. It is breakBlock's tail, so it runs before the abort
     * re-check and the metadata store below, while the cell holds the new
     * block and the old metadata. BlockFurnace.breakBlock skips only the item drops while
     * the lit/unlit swap is in progress (field_149934_M); its
     * removeTileEntity still runs, as the Java tail does. */
    if (BLOCKS[was_id & 4095].tile_entity)
        world_remove_tile_entity(w, c->cx * 16 + x, y, c->cz * 16 + z);

    /* var10.func_150819_a(...) != p_150807_4_ -> return false: breakBlock that
     * replaced the cell (a powered button's neighbour check pops it through the
     * sweep it makes) aborts the rest of this write; the caller's light pass
     * still runs, the metadata store and everything after does not */
    if (chunk_block(c, x, y, z) != id) return 0;

    chunk_set_meta_cell(c, CELL(x, y, z), meta & 15);
    world_note_write(w, c);

    int wx = c->cx * 16 + x, wz = c->cz * 16 + z;

    chunk_write_relight(w, c, x, y, z, id, was_id, was_height, made_section);

    /* the tail of func_150807_a: the old block's ITileEntityProvider check (the
     * standing entity gets updateContainingBlockInfo), then the new block's
     * onBlockAdded, then the new block's own ITileEntityProvider check, which
     * demand-creates the entity when the lookup came up empty (the furnace
     * swap's breakBlock removed the old one, so a fresh one is made and
     * TileEntityFurnace.func_149931_a puts the saved one back over it). A
     * callback that writes (lava meeting water, a chest's facing) lands in
     * the write stream before this write, as in vanilla. */
    if (BLOCKS[was_id & 4095].tile_entity)
    {
        struct tile_entity *old_te = world_tile_entity(w, wx, y, wz);
        if (old_te != NULL) old_te->block_metadata = -1;
    }

    return 1;
}

/* Chunk.func_150807_a's tail after onBlockAdded (and the chest's): the new
 * block's own tile entity check. */
static void chunk_set_block_te(struct world *w, int wx, int y, int wz, int id)
{
    if (BLOCKS[id & 4095].tile_entity)
    {
        struct tile_entity *new_te = world_tile_entity(w, wx, y, wz);

        if (new_te != NULL) new_te->block_metadata = -1;

        /* TileEntityDispenser.field_146021_j (the dropper inherits it): a
         * Det.newRandom per instance, spent when the constructor runs */
        if (id == 23 || id == 158) place_dispenser_seeder();
    }
}

/* World.func_147451_t: the sky pass (skipped in a hasNoSky world), then the
 * block light pass. */
static int world_check_light(struct world *w, int x, int y, int z)
{
    return light_pair(w, x, y, z);
}

/* World.notifyBlockChange (and World.func_147441_b with a side skipped): the
 * six neighbours' onNeighborBlockChange, in the order
 * World.notifyBlocksOfNeighborChange visits them, each block and metadata read
 * as its turn comes. source is the block the write replaced. getBlock
 * generates a missing chunk, as Java's does. The step runs one neighbour per
 * call and resumes after that neighbour's callbacks. */
static const int NOTIFY_OFF[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

static int notify_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;

    /* World.func_147460_e runs onNeighborBlockChange only when !isRemote:
     * a client's own write (a bucket's water beside its lava) notifies no
     * neighbour */
    if (w->is_remote) return 1;

    while (f->i < 6)
    {
        int i = f->i++;

        if (i == f->op.a[1]) continue;

        int nx = f->op.x + NOTIFY_OFF[i][0], ny = f->op.y + NOTIFY_OFF[i][1], nz = f->op.z + NOTIFY_OFF[i][2];

        bwl_on_neighbor(w, world_get_block(w, nx, ny, nz) & 4095, world_get_meta(w, nx, ny, nz), nx, ny, nz,
                        f->op.a[0]);

        if (bwl_pending()) return 0;
    }

    return 1;
}

/* World.notifyBlocksOfNeighborChange: the same six-neighbour sweep, called
 * directly by code that wants it without a write (a liquid updateTick telling
 * its neighbours its meta moved). The client half notifyBlockChange also runs
 * (markBlockForUpdate) is not ported, so the two are one body here. */
void world_notify_neighbors(struct world *w, int x, int y, int z, int source)
{
    BWL_CALL(BWL_NOTIFY, w, x, y, z, source, -1);
}

/* Chunk.setBlockMetadata: false when the section is missing or the metadata is
 * already the value (the compare is against the raw argument, the store is the
 * nibble array's meta & 15). A tile entity at the position gets
 * updateContainingBlockInfo and the fresh metadata, as the Java method runs on
 * it. */
static int chunk_set_meta(struct world *w, struct chunk *c, int x, int y, int z, int meta)
{
    if (!section_present(c, y)) return 0;
    if (chunk_cell_meta(c, CELL(x, y, z)) == (uint8_t)meta) return 0;

    chunk_set_meta_cell(c, CELL(x, y, z), meta & 15);
    world_note_write(w, c);

    if (BLOCKS[chunk_block(c, x, y, z) & 4095].tile_entity)
    {
        struct tile_entity *te = world_tile_entity(w, c->cx * 16 + x, y, c->cz * 16 + z);

        if (te != NULL)
        {
            te->block_metadata = -1; /* updateContainingBlockInfo */
            te->block_metadata = meta;
        }
    }

    return 1;
}

/* setBlockMetadataWithNotify up to the notification: the store and the write
 * listener; *old_id is the block there, the notification's source. */
static int set_meta_store(struct world *w, int x, int y, int z, int meta, int *old_id)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z))) return 0;
    if (y < 0 || y >= 256) return 0;
    cw_sync(w, LD_SYNC_WRITE);

    struct chunk *c = chunk_for(w, x, z);

    if (c == NULL) return 0;

    *old_id = chunk_block(c, x & 15, y, z & 15);
    int changed = chunk_set_meta(w, c, x & 15, y, z & 15, meta);

    /* Rows.onBlock gets id -1 for a metadata-only write (the probes' 16-bit id
     * field holds it as 0xffff) */
    if (changed && w->on_block != NULL) w->on_block(w->on_block_ctx, x, y, z, 0xffff, meta);

    return changed;
}

int world_set_meta_quiet(struct world *w, int x, int y, int z, int meta)
{
    int old_id;

    /* setBlockMetadataWithNotify(.., 2): sent, no neighbour notified */
    w->write_flags = 2;
    return set_meta_store(w, x, y, z, meta, &old_id);
}

int world_set_meta(struct world *w, int x, int y, int z, int meta, int flags)
{
    int old_id;
    w->write_flags = flags;
    int changed = set_meta_store(w, x, y, z, meta, &old_id);

    /* World.setBlockMetadataWithNotify notifies only when it stored the value,
     * and never on a client world; the flag 2 render half is not ported */
    if (changed && (flags & 1) && !w->is_remote)
    {
        BWL_CALL(BWL_NOTIFY, w, x, y, z, old_id, -1);
        /* hasComparatorInputOverride: World.func_147453_f */
        if (comparator_has_override(old_id)) comparator_notify(w, x, y, z, old_id);
    }

    return changed;
}

/* SET_PLAIN[id]: bit 1, the block's breakBlock is Block's empty body
 * (blockcb.c bwl_on_broken returns without an effect); bit 2, its
 * onBlockAdded is too (bwl_on_added's default) and it is neither a chest nor
 * a tile entity. A list of the blocks population and the tick write most,
 * each checked against both callbacks: an id left out takes the worklist. */
static uint8_t SET_PLAIN[4096];

__attribute__((constructor)) static void set_plain_init(void)
{
    /* air, stone, grass, dirt, cobblestone, bedrock, the ores, logs, leaves,
     * sandstone, the plants, mossy cobblestone, snow, ice, cactus, clay,
     * reeds, pumpkin, netherrack, soul sand, monster egg, stone brick, the
     * mushroom blocks, melon, vine, mycelium, lily pad, end stone, quartz
     * ore, the clays, packed ice, the double plants */
    static const uint16_t both[] = {0,  1,  2,  3,  4,  7,  14, 15, 16, 17, 18, 21,  24,  31,  32,  37,  38,  39,
                                    40, 48, 56, 73, 78, 79, 80, 81, 82, 83, 86, 87,  88,  97,  98,  99,  100, 103,
                                    106, 110, 111, 121, 153, 159, 161, 162, 172, 174, 175};
    /* the liquids, sand and gravel: an empty breakBlock, an onBlockAdded */
    static const uint16_t old_only[] = {8, 9, 10, 11, 12, 13};

    for (size_t i = 0; i < sizeof both / sizeof both[0]; ++i) SET_PLAIN[both[i]] = 3;
    for (size_t i = 0; i < sizeof old_only / sizeof old_only[0]; ++i) SET_PLAIN[old_only[i]] = 1;
    for (int id = 0; id < 4096; ++id)
        if (BLOCKS[id].tile_entity) SET_PLAIN[id] = 0;
}

/* set_block_step for a write that calls nothing: a flag without 1 (no
 * neighbour notification), the block replaced and the block written both
 * SET_PLAIN, the chunk in the near cache. The same calls in the same order,
 * without a worklist frame: the step's breakBlock and onBlockAdded are empty
 * for these, and nothing else in it emits. -1 (having done nothing) for any
 * other write. */
static int set_block_plain(struct world *w, int x, int y, int z, int id, int meta, int flags)
{
    struct chunk *c = world_chunk_near(w, x >> 4, z >> 4);

    if (c == NULL || w->no_generate || nw_env->bwl.nemit != 0) return -1;

    int lx = x & 15, lz = z & 15, old_id = chunk_block(c, lx, y, lz);

    if (!(SET_PLAIN[old_id & 4095] & 1)) return -1;

    int was_height, made_section, changed = 0;

    w->write_flags = flags;
    cw_sync(w, LD_SYNC_WRITE);
    if (w->on_attempt != NULL) w->on_attempt(w->on_block_ctx, x, y, z, id, meta);
    if (chunk_set_block_head(w, c, lx, y, lz, id, meta, &was_height, &made_section) &&
        chunk_set_block_mid(w, c, lx, y, lz, id, meta, old_id, was_height, made_section))
    {
        chunk_set_block_te(w, x, y, z, id);
        changed = 1;
    }
    w->write_flags = flags;
    if (changed && w->on_block != NULL) w->on_block(w->on_block_ctx, x, y, z, id, meta);
    world_check_light(w, x, y, z);
    if (w->on_attempt_end != NULL) w->on_attempt_end(w->on_block_ctx, x, y, z, id, meta);
    nw_env->bwl.ret = changed;
    return changed;
}

int world_set_block(struct world *w, int x, int y, int z, int id, int meta, int flags)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z))) return 0;
    if (y < 0 || y >= 256) return 0;

    if (!(flags & 1) && !w->is_remote && (SET_PLAIN[id & 4095] & 2))
    {
        int r = set_block_plain(w, x, y, z, id, meta, flags);

        if (r >= 0) return r;
    }
    BWL_CALL(BWL_SET_BLOCK, w, x, y, z, id, meta, flags);
    return nw_env->bwl.ret;
}

/* World.setBlock as a worklist frame: the chunk write with the old block's
 * breakBlock and the new block's onBlockAdded as calls, then the light pass
 * and the flag-1 notification. The locals: */
enum { SB_OLD_ID, SB_WAS_HEIGHT, SB_MADE_SECTION, SB_CHANGED };
/* the resume points */
enum { SB_START, SB_BROKEN, SB_ADDED, SB_TE, SB_TAIL, SB_END };

static int set_block_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z;
    int id = f->op.a[0], meta = f->op.a[1], flags = f->op.a[2];
    f->op.w->write_flags = flags;
    int *L = f->u.l;
    struct chunk *c = f->op.p;

    switch (f->pc)
    {
    case SB_START:
        if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z)) || y < 0 || y >= 256)
        {
            nw_env->bwl.ret = 0;
            return 1;
        }
        cw_sync(w, LD_SYNC_WRITE);

        c = chunk_for(w, x, z);
        f->op.p = c;

        /* netherite.oracle.Rows.onAttempt: before the chunk decides anything,
         * so a write that changes nothing is still reported (it runs the light
         * pass) */
        if (w->on_attempt != NULL) w->on_attempt(w->on_block_ctx, x, y, z, id, meta);

        /* ChunkProviderServer's defaultEmptyChunk: EmptyChunk.func_150807_a
         * returns true, so a write outside the loaded set is still reported
         * (the write listener records it) and still runs the light pass and
         * the neighbour notification; nothing is stored. Chunk.setBlockMetadata
         * returns false, so a metadata-only write is not reported. */
        if (c == NULL)
        {
            w->write_flags = flags;
            if (w->on_block != NULL) w->on_block(w->on_block_ctx, x, y, z, id, meta);

            world_check_light(w, x, y, z);
            L[SB_CHANGED] = 1;
            f->pc = SB_END;

            if (flags & 1) BWL_EMIT(BWL_NOTIFY, w, x, y, z, BLK_AIR, -1);

            return 0;
        }

        L[SB_OLD_ID] = chunk_block(c, x & 15, y, z & 15);

        if (!chunk_set_block_head(w, c, x & 15, y, z & 15, id, meta, &L[SB_WAS_HEIGHT], &L[SB_MADE_SECTION]))
        {
            L[SB_CHANGED] = 0;
            goto tail;
        }

        /* the old block's breakBlock (blockcb.c); the step goes on in place
         * when it called nothing. A client world (isRemote) runs neither
         * breakBlock nor onBlockAdded (Chunk.func_150807_a) */
        if (!w->is_remote) bwl_on_broken(w, x, y, z, L[SB_OLD_ID]);
        f->pc = SB_BROKEN;

        if (bwl_pending()) return 0;
        /* fall through */

    case SB_BROKEN:
        if (!chunk_set_block_mid(w, c, x & 15, y, z & 15, id, meta, L[SB_OLD_ID], L[SB_WAS_HEIGHT],
                                 L[SB_MADE_SECTION]))
        {
            L[SB_CHANGED] = 0;
            goto tail;
        }

        /* the new block's onBlockAdded (blockcb.c) */
        if (!w->is_remote) bwl_on_added(w, id, meta, x, y, z);
        f->pc = SB_ADDED;

        if (bwl_pending()) return 0;
        /* fall through */

    case SB_ADDED:
        /* BlockChest.onBlockAdded, here beside the chest's facing rules */
        if ((id == BLK_CHEST || id == 146 /* trapped_chest */) && !w->is_remote)
        {
            BWL_EMIT(BWL_CHEST_ADDED, w, x, y, z, id);
            f->pc = SB_TE;
            return 0;
        }
        /* fall through */

    case SB_TE:
        chunk_set_block_te(w, x, y, z, id);
        L[SB_CHANGED] = 1;
        /* fall through */

    case SB_TAIL:
    tail:
        /* netherite.oracle.Rows.onBlock: the tape digest and the probe write
         * listener, both fed only by a write that changed the block */
        w->write_flags = flags;
        if (L[SB_CHANGED] && w->on_block != NULL) w->on_block(w->on_block_ctx, x, y, z, id, meta);

        world_check_light(w, x, y, z);
        f->pc = SB_END;

        if (L[SB_CHANGED] && (flags & 1) && !w->is_remote)
        {
            BWL_EMIT(BWL_NOTIFY, w, x, y, z, L[SB_OLD_ID], -1);
            /* hasComparatorInputOverride: World.func_147453_f after the
             * neighbours' round */
            if (comparator_has_override(id)) comparator_notify_emit(w, x, y, z, id);
            return 0;
        }
        /* fall through */

    case SB_END:
    default:
        if (w->on_attempt_end != NULL) w->on_attempt_end(w->on_block_ctx, x, y, z, id, meta);

        nw_env->bwl.ret = L[SB_CHANGED];
        return 1;
    }
}

int world_client_set_block(struct world *w, int x, int y, int z, int id, int meta)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z)) || y < 0 || y >= 256) return 0;
    cw_sync(w, LD_SYNC_WRITE);

    struct chunk *c = world_chunk(w, x >> 4, z >> 4);
    if (c == NULL) return 0;

    int lx = x & 15, lz = z & 15, was_height, made_section, changed = 0;
    int was_id = chunk_block(c, lx, y, lz);

    if (chunk_set_block_head(w, c, lx, y, lz, id, meta, &was_height, &made_section))
    {
        /* a client world runs no breakBlock: a tile entity block replaced by
         * another block only loses its entity */
        if (BLOCKS[was_id & 4095].tile_entity && (was_id & 4095) != (id & 4095))
            world_remove_tile_entity(w, x, y, z);
        chunk_set_meta_cell(c, CELL(lx, y, lz), meta & 15);
        world_note_write(w, c);
        chunk_write_relight(w, c, lx, y, lz, id, was_id, was_height, made_section);
        changed = 1;
    }

    /* World.setBlock's light pass runs whether or not the chunk changed */
    world_check_light(w, x, y, z);
    return changed;
}

int world_bwl_step(struct bwl_frame *f)
{
    switch (f->op.kind)
    {
    case BWL_SET_BLOCK:
        return set_block_step(f);

    case BWL_SET_META:
    {
        int old_id;
        struct world *w = f->op.w;

        w->write_flags = f->op.a[1];
        if (set_meta_store(w, f->op.x, f->op.y, f->op.z, f->op.a[0], &old_id) && (f->op.a[1] & 1) &&
            !w->is_remote)
        {
            BWL_EMIT(BWL_NOTIFY, w, f->op.x, f->op.y, f->op.z, old_id, -1);
            if (comparator_has_override(old_id)) comparator_notify_emit(w, f->op.x, f->op.y, f->op.z, old_id);
        }

        return 1;
    }

    case BWL_NOTIFY:
        return notify_step(f);

    case BWL_NEIGHBOR:
    {
        /* World.func_147460_e: one position's onNeighborBlockChange, on a
         * server world only */
        struct world *w = f->op.w;

        if (w->is_remote) return 1;
        if (f->op.a[1]) bwl_on_neighbor(w, f->op.a[2], f->op.a[3], f->op.x, f->op.y, f->op.z, f->op.a[0]);
        else
            bwl_on_neighbor(w, world_get_block(w, f->op.x, f->op.y, f->op.z) & 4095,
                            world_get_meta(w, f->op.x, f->op.y, f->op.z), f->op.x, f->op.y, f->op.z, f->op.a[0]);
        return 1;
    }

    case BWL_CHEST_ADDED:
        return chest_added_step(f);

    default:
        return 1;
    }
}

/* BlockFlowerPot.createNewTileEntity: the Item and Data a flower pot's
 * metadata calls for. Item.getItemFromBlock(block) resolves to the block's own
 * item id, and the default case's null block gives 0. */
static void flower_pot_contents(int meta, int *item, int *data)
{
    switch (meta)
    {
        case 1: *item = 38; *data = 0; break;   /* red_flower */
        case 2: *item = 37; *data = 0; break;   /* yellow_flower */
        case 3: *item = 6; *data = 0; break;    /* sapling */
        case 4: *item = 6; *data = 1; break;
        case 5: *item = 6; *data = 2; break;
        case 6: *item = 6; *data = 3; break;
        case 7: *item = 40; *data = 0; break;   /* red_mushroom */
        case 8: *item = 39; *data = 0; break;   /* brown_mushroom */
        case 9: *item = 81; *data = 0; break;   /* cactus */
        case 10: *item = 32; *data = 0; break;  /* deadbush */
        case 11: *item = 31; *data = 2; break;  /* tallgrass */
        case 12: *item = 6; *data = 4; break;
        case 13: *item = 6; *data = 5; break;
        default: *item = -1; *data = 0; break;
    }
}

/* World.getBiomeGenForCoords(x, z): blockExists decides between the chunk's
 * blockBiomeArray and the biome layer, as the Java method does. */
int world_get_biome(struct world *w, int x, int z)
{
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);

    if (c != NULL) return c->biome[(z & 15) << 4 | (x & 15)];

    int id[1];
    biomes_full(&world_gen(w)->terrain.layers, x, z, 1, 1, id);
    return id[0];
}

/* World.getChunkHeightMapMinimum. */
int world_get_height_map_minimum(struct world *w, int x, int z)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z))) return 0;

    struct chunk *c = world_chunk(w, x >> 4, z >> 4);

    return c == NULL ? 0 : c->height_min;
}

/* Chunk.getTopFilledSegment: the y base of the highest section that exists, 0
 * when the chunk has none. */
static int top_filled_segment(const struct chunk *c)
{
    for (int s = 15; s >= 0; --s)
        if (section_present(c, s << 4)) return s << 4;

    return 0;
}

/* Chunk.getPrecipitationHeight, memoized in precipitationHeightMap. */
int world_get_precipitation_height(struct world *w, int x, int z)
{
    if (!(XZ_IN_RANGE(x) && XZ_IN_RANGE(z))) return 64;

    struct chunk *c = chunk_for(w, x, z);

    if (c == NULL) return 0;

    int lx = x & 15, lz = z & 15, k = lz << 4 | lx;
    int got = c->precip[k];

    if (got == -999)
    {
        int y = top_filled_segment(c) + 15;
        got = -1;

        while (y > 0 && got == -1)
        {
            int id = chunk_block(c, lx, y, lz);
            const struct material_def *m = &MATERIALS[BLOCKS[id & 4095].material];

            if (!m->blocks_movement && !m->is_liquid) --y;
            else got = y + 1;
        }

        c->precip[k] = got;
    }

    return got;
}

/* World.getFullBlockLightValue -> Chunk.getBlockLightValue. */
int world_get_full_block_light_value(struct world *w, int x, int y, int z, int skylight_subtracted)
{
    cw_sync(w, LD_SYNC_READ);
    if (y < 0) return 0;
    if (y >= 256) y = 255;

    struct chunk *c = chunk_for(w, x, z);

    if (c == NULL) return 0;

    int sky = chunk_saved_light(c, LIGHT_SKY, x & 15, y, z & 15) - skylight_subtracted;
    int block = chunk_saved_light(c, LIGHT_BLOCK, x & 15, y, z & 15);

    return sky > block ? sky : block;
}

/* World.func_147451_t: the sky pass, then the block light pass. Java skips
 * the sky pass when the provider hasNoSky (the Nether, the End). */
int world_check_light_at(struct world *w, int x, int y, int z)
{
    return light_pair(w, x, y, z);
}

/* lightdefer.h's C executor: each operation as its hook would have run it
 * (lightdefer's in_run lets the hooks through) */
int world_light_defer_run_c(struct world *w, const struct ld_op *ops, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        const struct ld_op *o = &ops[i];

        if (o->what == LC_O_UPDATE) (void)update_light_by_type(w, o->type, o->x, o->y, o->z);
        else
        {
            struct chunk *c = world_chunk(w, o->x, o->z);

            if (c == NULL) return -1;   /* an unload syncs first: never */
            chunk_enqueue_relight_checks(w, c);
        }
    }
    return 0;
}

/* Chunk.checkSkylightNeighborHeight. */
static void check_skylight_neighbor_height(struct world *w, int x, int z, int max_y)
{
    int h = world_get_height_value(w, x, z);

    if (h > max_y) update_skylight_neighbor_height(w, x, z, max_y, h + 1);
    else if (h < max_y) update_skylight_neighbor_height(w, x, z, h, max_y + 1);
}

/* Chunk.recheckGaps: every column flagged by propagateSkylightOcclusion gets
 * its neighbours' sky light re-checked against the lowest height around it. */
void chunk_recheck_gaps(struct world *w, struct chunk *c, int is_client)
{
    if (!world_do_chunks_near_chunk_exist(w, c->cx * 16 + 8, 0, c->cz * 16 + 8, 16)) return;

    for (int x = 0; x < 16; ++x)
    {
        for (int z = 0; z < 16; ++z)
        {
            if (!c->update_skylight_columns[x + z * 16]) continue;

            c->update_skylight_columns[x + z * 16] = 0;
            int h = c->height[z << 4 | x];
            int wx = c->cx * 16 + x, wz = c->cz * 16 + z;
            int m = world_get_height_map_minimum(w, wx - 1, wz);
            int e = world_get_height_map_minimum(w, wx + 1, wz);
            int s1 = world_get_height_map_minimum(w, wx, wz - 1);
            int s2 = world_get_height_map_minimum(w, wx, wz + 1);

            if (e < m) m = e;
            if (s1 < m) m = s1;
            if (s2 < m) m = s2;

            check_skylight_neighbor_height(w, wx, wz, m);
            check_skylight_neighbor_height(w, wx - 1, wz, h);
            check_skylight_neighbor_height(w, wx + 1, wz, h);
            check_skylight_neighbor_height(w, wx, wz - 1, h);
            check_skylight_neighbor_height(w, wx, wz + 1, h);

            /* the client half returns after the first flagged column and
             * leaves isGapLightingUpdated set */
            if (is_client) return;
        }
    }

    c->gap_lighting_updated = 0;
}

/* Chunk.enqueueRelightChecks: eight cursor steps per tick, each sweeping the
 * column it names and re-lighting any cell next to a light source. */
static void chunk_enqueue_relight_checks_(struct world *w, struct chunk *c);

void chunk_enqueue_relight_checks(struct world *w, struct chunk *c)
{
    if (__builtin_expect(nw_env->light.defer != NULL, 0) && cw_defer_op(w, LC_O_RELIGHT, 0, c->cx, 0, c->cz)) return;

    struct lightcap *cap = nw_env->light.cap;
    int t = cap != NULL ? lightcap_driver_begin(cap, w, c, LC_O_RELIGHT, 0) : -1;

    chunk_enqueue_relight_checks_(w, c);
    if (t >= 0) lightcap_driver_end(cap, w, c, t, 0);
}

static void chunk_enqueue_relight_checks_(struct world *w, struct chunk *c)
{
    for (int i = 0; i < 8; ++i)
    {
        if (c->queued_light_checks >= 4096) return;

        int s = c->queued_light_checks % 16;
        int x = c->queued_light_checks / 16 % 16;
        int z = c->queued_light_checks / 256;
        ++c->queued_light_checks;
        int wx = (c->cx << 4) + x, wz = (c->cz << 4) + z;
        int present = section_present(c, s << 4);

        for (int y = 0; y < 16; ++y)
        {
            int wy = (s << 4) + y;

            if (present)
            {
                int id = chunk_block(c, x, wy, z);

                if (BLOCKS[id & 4095].material != air_material()) continue;
            }
            else if (!(y == 0 || y == 15 || x == 0 || x == 15 || z == 0 || z == 15)) continue;

            if (BLOCKS[world_get_block(w, wx, wy - 1, wz) & 4095].light > 0) world_check_light_at(w, wx, wy - 1, wz);
            if (BLOCKS[world_get_block(w, wx, wy + 1, wz) & 4095].light > 0) world_check_light_at(w, wx, wy + 1, wz);
            if (BLOCKS[world_get_block(w, wx - 1, wy, wz) & 4095].light > 0) world_check_light_at(w, wx - 1, wy, wz);
            if (BLOCKS[world_get_block(w, wx + 1, wy, wz) & 4095].light > 0) world_check_light_at(w, wx + 1, wy, wz);
            if (BLOCKS[world_get_block(w, wx, wy, wz - 1) & 4095].light > 0) world_check_light_at(w, wx, wy, wz - 1);
            if (BLOCKS[world_get_block(w, wx, wy, wz + 1) & 4095].light > 0) world_check_light_at(w, wx, wy, wz + 1);
            world_check_light_at(w, wx, wy, wz);
        }
    }
}

/* ------------------------------------------------- the server tick's reads */

/* World.getBiomeGenForCoords for a position whose chunk is loaded, and
 * BiomeGenBase.getFloatTemperature: features_lakes.c holds both (the lakes
 * feature needs them too) and servertick.c reads them through features_lakes.h.
 * The ice, snow and lightning branches of WorldServer.func_147456_g are their
 * other caller. */

/* Chunk.func_150811_f: one column's sky re-check, the pass func_150809_p runs
 * over a chunk that is terrain-populated but not light-populated. Every cell
 * the column's sky has reached is re-lit; a gap under it fails the chunk. */
static int chunk_populate_column_slow(struct world *w, struct chunk *c, int x, int z);

/* COLUMN_EMIT[b]: some id whose low byte is b emits light (Block.getLightValue
 * > 0), so a column's cells of a band with no such byte hold no light source
 * (lane/popfeat's test). Built before main from BLOCKS, the same for every
 * environment. */
static uint8_t COLUMN_EMIT[256];

__attribute__((constructor)) static void column_emit_init(void)
{
    for (int id = 0; id < (int)(sizeof BLOCKS / sizeof BLOCKS[0]); ++id)
        if (BLOCKS[id].light > 0) COLUMN_EMIT[id & 255] = 1;
}

static int chunk_populate_column(struct world *w, struct chunk *c, int x, int z)
{
    return world_light_column_check(w, c, x, z);
}

/* The same walk in a server world with no light capture or profile
 * (light_pair_server): every check is light_pair at this column's x and z,
 * whose box test (y 1..255 inside it) is the column's alone and holds for
 * the whole walk (a pass loads nothing), so it is taken once; each band's
 * storage is looked up once (a pass writes light alone: a band it makes
 * reads as the air it replaced, and one is looked up again after a check). */
int world_light_column_check(struct world *w, struct chunk *c, int x, int z)
{
    int wx = c->cx * 16 + x, wz = c->cz * 16 + z;

    if (w->dim != 0 || !light_pair_server(w, wx, wz)) return chunk_populate_column_slow(w, c, x, z);

    lw_center(w, wx, wz);

    int box = lw_near_exist(w, wx, 128, wz, 17);
    int top = top_filled_segment(c);
    int reached = 0, sealed = 0, y, sb = -1, col = x << 8 | z << 4;
    const struct chunk_sec *sec = NULL;

    for (y = top + 16 - 1; y > 63 || (y > 0 && !sealed); --y)
    {
        if (y >> 4 != sb)
        {
            sb = y >> 4;
            sec = (c->mask >> sb & 1) ? chunk_sec_at(c, sb) : NULL;
        }

        int op = BLOCKS[sec != NULL ? chunk_sec_id(sec, col | (y & 15)) & 4095 : 0].opacity;

        if (op == 255 && y < 63) sealed = 1;

        if (!reached && op > 0)
        {
            reached = 1;
        }
        else if (reached && op == 0)
        {
            if (!box) return 0;
            sky_pass_boxed(w, wx, y, wz);
            block_pass_boxed(w, wx, y, wz);
            sb = -1;
        }
    }

    /* below it only a light source is checked: a band's cells of the column
     * with no byte a light source can have (COLUMN_EMIT) are passed at once */
    while (y > 0)
    {
        if (y >> 4 != sb)
        {
            sb = y >> 4;
            sec = (c->mask >> sb & 1) ? chunk_sec_at(c, sb) : NULL;

            int y0 = y & ~15, any = 0;

            if (sec != NULL)
                for (int k = y0 > 0 ? 0 : 1; k <= (y & 15); ++k) any |= COLUMN_EMIT[sec->ids[col | k]];
            if (!any)
            {
                y = y0 - 1;
                continue;
            }
        }
        if (sec != NULL && BLOCKS[chunk_sec_id(sec, col | (y & 15)) & 4095].light > 0 && box)
        {
            sky_pass_boxed(w, wx, y, wz);
            block_pass_boxed(w, wx, y, wz);
            sb = -1;
            sec = (c->mask >> (y >> 4) & 1) ? chunk_sec_at(c, y >> 4) : NULL;
        }
        --y;
    }

    return 1;
}

static int chunk_populate_column_slow(struct world *w, struct chunk *c, int x, int z)
{
    int top = top_filled_segment(c);
    int reached = 0, sealed = 0, y;

    for (y = top + 16 - 1; y > 63 || (y > 0 && !sealed); --y)
    {
        int op = chunk_opacity(c, x, y, z);

        if (op == 255 && y < 63) sealed = 1;

        if (!reached && op > 0)
        {
            reached = 1;
        }
        else if (reached && op == 0 && !world_check_light_at(w, c->cx * 16 + x, y, c->cz * 16 + z))
        {
            return 0;
        }
    }

    for (; y > 0; --y)
    {
        if (BLOCKS[chunk_block(c, x, y, z) & 4095].light > 0)
            world_check_light_at(w, c->cx * 16 + x, y, c->cz * 16 + z);
    }

    return 1;
}

/* Chunk.func_150801_a: the neighbours of a chunk that just became light
 * populated get the column next to it re-checked. */
static void chunk_light_populate_neighbor_(struct world *w, struct chunk *c, int side)
{
    if (!c->terrain_populated) return;

    for (int i = 0; i < 16; ++i)
    {
        if (side == 3) chunk_populate_column(w, c, 15, i);
        else if (side == 1) chunk_populate_column(w, c, 0, i);
        else if (side == 0) chunk_populate_column(w, c, i, 15);
        else if (side == 2) chunk_populate_column(w, c, i, 0);
    }
}

/* the same under the light capture (lightcap.h), which may take it whole */
static void chunk_light_populate_neighbor(struct world *w, struct chunk *c, int side)
{
    struct lightcap *cap = nw_env->light.cap;
    int t = cap != NULL ? lightcap_driver_begin(cap, w, c, LC_O_SIDE, side) : -1;

    chunk_light_populate_neighbor_(w, c, side);
    if (t >= 0) lightcap_driver_end(cap, w, c, t, 0);
}

/* func_150809_p's World.getChunkFromBlockCoords for a neighbour: the +-1
 * block existence test covers the west and north ones only, so the east or
 * south one may not be loaded. With loadChunkOnProvideRequest on (a world
 * that generates, whose provider hook loads through the replay) the call
 * loads it, from the region store or by generation with the populate rule;
 * off, Java gets ChunkProviderServer.defaultEmptyChunk, whose
 * isTerrainPopulated is false, so nothing happens. */
static struct chunk *light_populate_neighbor_chunk(struct world *w, int cx, int cz)
{
    struct chunk *n = world_chunk(w, cx, cz);

    if (n == NULL && !w->no_generate && w->provide != NULL) n = world_load_chunk(w, cx, cz);
    return n;
}

/* Chunk.func_150809_p. Java skips the whole pass when the provider hasNoSky
 * (the Nether, the End): both flags stay set, nothing else runs. */
static void chunk_light_populate(struct world *w, struct chunk *c)
{
    c->terrain_populated = 1;
    c->light_populated = 1;

    if (w->dim != 0) return;

    if (!world_check_chunks_exist(w, c->cx * 16 - 1, 0, c->cz * 16 - 1, c->cx * 16 + 1, 63, c->cz * 16 + 1))
    {
        c->light_populated = 0;
        return;
    }

    struct lightcap *cap = nw_env->light.cap;
    int t = cap != NULL ? lightcap_driver_begin(cap, w, c, LC_O_COLUMNS, 0) : -1;

    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            if (!chunk_populate_column(w, c, x, z))
            {
                c->light_populated = 0;
                break;
            }
    if (t >= 0) lightcap_driver_end(cap, w, c, t, c->light_populated);

    if (c->light_populated)
    {
        struct chunk *n;

        n = light_populate_neighbor_chunk(w, c->cx - 1, c->cz);
        if (n != NULL) chunk_light_populate_neighbor(w, n, 3);

        n = light_populate_neighbor_chunk(w, c->cx + 1, c->cz);
        if (n != NULL) chunk_light_populate_neighbor(w, n, 1);

        n = light_populate_neighbor_chunk(w, c->cx, c->cz - 1);
        if (n != NULL) chunk_light_populate_neighbor(w, n, 0);

        n = light_populate_neighbor_chunk(w, c->cx, c->cz + 1);
        if (n != NULL) chunk_light_populate_neighbor(w, n, 2);
    }
}

/* The light re-check memo. func_150809_p fails when a checked cell's 17-block
 * box reaches an unloaded chunk, and a chunk one ring inside the loaded edge
 * retries every tick. An attempt that fails and makes no light write (the
 * world epoch did not move) reads only:
 *  - which of the 5x5 chunks around it are loaded (the +-1 block existence
 *    test and each cell's 17-block box test, chunks cx - 2 .. cx + 2);
 *  - for each cell it checks, updateLightByType's getSavedLightValue and
 *    computeLightValue: the cell's block, the column height (canBlockSeeTheSky)
 *    and the saved light of the six neighbours, which lie in this chunk or on
 *    the facing edge column of the four chunks beside it. A pass whose
 *    computed light equals the stored light queues nothing and writes
 *    nothing; any other pass writes (both branches pop their first entry into
 *    a setLightValue), so the attempt would not be memoised;
 *  - the chunk's own opacities, sections and light sources (func_150811_f).
 * No metadata is read (opacity and light value are per block id). So while
 * the 5x5 loaded mask is the same, this chunk has no later stamp and none of
 * the four neighbours has a later stamp on its facing edge, a retry reads the
 * same values, takes the same branches, fails at the same cell and writes
 * nothing again: skipping it is exact. The light queue's 32,768 cap cannot be
 * reached by such a retry, since it queues no entry at all. */
static uint32_t light_memo_loaded(struct world *w, const struct chunk *c, uint64_t epoch, int *newer)
{
    uint32_t mask = 0;

    *newer = c->stamp > epoch;

    for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx)
        {
            const struct chunk *n = world_chunk(w, c->cx + dx, c->cz + dz);

            if (n == NULL) continue;

            mask |= 1u << ((dz + 2) * 5 + dx + 2);

            /* the edge of the neighbour that faces this chunk: x = 15 of the
             * one at dx = -1, x = 0 at dx = +1, z = 15 at dz = -1, z = 0 at
             * dz = +1 */
            if (dz == 0 && dx == -1 && n->edge_stamp[1] > epoch) *newer = 1;
            if (dz == 0 && dx == 1 && n->edge_stamp[0] > epoch) *newer = 1;
            if (dx == 0 && dz == -1 && n->edge_stamp[3] > epoch) *newer = 1;
            if (dx == 0 && dz == 1 && n->edge_stamp[2] > epoch) *newer = 1;
        }

    return mask;
}

#ifdef NETHERITE_LIGHT_MEMO_CHECK
/* The check build (make -C csrc lightmemo-check): every skipped retry also
 * runs the full attempt and compares the sky and block light bytes of the
 * whole 5x5 before and after, and that the attempt failed and wrote nothing. */
static uint8_t *light_memo_scratch;
static long light_memo_checked;

static void light_memo_report(void)
{
    fprintf(stderr, "light memo check: %ld skipped retries re-run, all identical\n", light_memo_checked);
}

static void light_memo_check(struct world *w, struct chunk *c)
{
    const size_t per = 2 * (size_t)CHUNK_CELLS;

    if (light_memo_scratch == NULL)
    {
        light_memo_scratch = malloc(25 * per);
        if (light_memo_scratch == NULL) abort();
        atexit(light_memo_report);
    }

    for (int k = 0; k < 25; ++k)
    {
        const struct chunk *n = world_chunk(w, c->cx + k % 5 - 2, c->cz + k / 5 - 2);

        if (n == NULL) continue;
        chunk_cells_out(n, NULL, NULL, light_memo_scratch + k * per, light_memo_scratch + k * per + CHUNK_CELLS);
    }

    uint64_t e = w->epoch;
    int bad = 0;

    chunk_light_populate(w, c);

    if (c->light_populated || w->epoch != e) bad = 1;

    static uint8_t now[2 * CHUNK_CELLS];

    for (int k = 0; k < 25 && !bad; ++k)
    {
        const struct chunk *n = world_chunk(w, c->cx + k % 5 - 2, c->cz + k / 5 - 2);

        if (n == NULL) continue;
        chunk_cells_out(n, NULL, NULL, now, now + CHUNK_CELLS);
        if (memcmp(light_memo_scratch + k * per, now, 2 * CHUNK_CELLS) != 0) bad = 1;
    }

    if (bad)
    {
        fprintf(stderr, "light memo check: chunk (%d,%d) skipped a retry that %s\n", c->cx, c->cz,
                c->light_populated ? "succeeds" : "writes light");
        exit(3);
    }

    ++light_memo_checked;
}
#endif

static void chunk_light_populate_memo(struct world *w, struct chunk *c)
{
    int newer;

    if (c->lm_valid && light_memo_loaded(w, c, c->lm_epoch, &newer) == c->lm_loaded && !newer)
    {
#ifdef NETHERITE_LIGHT_MEMO_CHECK
        light_memo_check(w, c);
#endif
        return;
    }

    uint64_t e = w->epoch;

    PHASE_SUB(PS_NEWLIGHT, chunk_light_populate(w, c));

    c->lm_valid = !c->light_populated && w->epoch == e;
    if (c->lm_valid)
    {
        c->lm_epoch = e;
        c->lm_loaded = light_memo_loaded(w, c, e, &newer);
    }
}

/* Chunk.func_150804_b(false): the gap re-check, then the light-populated pass
 * over a terrain-populated chunk. */
void chunk_tick_flags(struct world *w, struct chunk *c)
{
    if (c->gap_lighting_updated) chunk_recheck_gaps(w, c, 0);

    c->populated = 1;

    if (!c->light_populated && c->terrain_populated) chunk_light_populate_memo(w, c);
}

/* World.canBlockFreeze: water at the position, no light, cold enough. The
 * naturally flag also demands one neighbouring cell that is not water. */
int world_can_block_freeze(struct world *w, int x, int y, int z, int naturally)
{
    int biome = biome_at(w, x, z);
    float t = biome_temperature(biome, x, y, z);

    if (t > 0.15F) return 0;

    if (y >= 0 && y < 256 && world_get_light(w, LIGHT_BLOCK, x, y, z) < 10)
    {
        int id = world_get_block(w, x, y, z);

        if ((id == 9 || id == 8) && world_get_meta(w, x, y, z) == 0)
        {
            if (!naturally) return 1;

            int water = BLOCKS[9].material;
            int open = 1;

            /* each read only while the ones before found water: a read can
             * load (and populate) the chunk it lands in */
            if (open && BLOCKS[world_get_block(w, x - 1, y, z) & 4095].material != water) open = 0;
            if (open && BLOCKS[world_get_block(w, x + 1, y, z) & 4095].material != water) open = 0;
            if (open && BLOCKS[world_get_block(w, x, y, z - 1) & 4095].material != water) open = 0;
            if (open && BLOCKS[world_get_block(w, x, y, z + 1) & 4095].material != water) open = 0;

            if (!open) return 1;
        }
    }

    return 0;
}

/* BlockSnow.canPlaceBlockAt, used by world_can_snow_at below and by the snow
 * layer's own placement. */
static int snow_layer_placeable(struct world *w, int x, int y, int z);

/* World.func_147478_e: cold enough for snow, and (with the check flag) an air
 * cell a snow layer can sit on. */
int world_can_snow_at(struct world *w, int x, int y, int z, int check)
{
    int biome = biome_at(w, x, z);
    float t = biome_temperature(biome, x, y, z);

    if (t > 0.15F) return 0;
    if (!check) return 1;

    if (y >= 0 && y < 256 && world_get_light(w, LIGHT_BLOCK, x, y, z) < 10)
    {
        if (BLOCKS[world_get_block(w, x, y, z) & 4095].material == air_material() &&
            snow_layer_placeable(w, x, y, z))
            return 1;
    }

    return 0;
}

/* BlockSnow.canPlaceBlockAt. */
static int snow_layer_placeable(struct world *w, int x, int y, int z)
{
    int below = world_get_block(w, x, y - 1, z) & 4095;

    if (below == 79 || below == 174) return 0;   /* ice, packed_ice */

    if (BLOCKS[below].material == BLOCKS[18].material) return 1;   /* leaves */

    if (below == 78 && (world_get_meta(w, x, y - 1, z) & 7) == 7) return 1;

    return BLOCKS[below].opaque_cube && MATERIALS[BLOCKS[below].material].blocks_movement;
}

/* Block.fillWithRain over the blocks that override it: only BlockCauldron
 * does, and it draws one nextInt(20) whether or not it fills. */
void world_fill_with_rain(struct world *w, jrand *rand, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;

    if (id != 118) return;

    if (jr_int_n(rand, 20) == 1)
    {
        int meta = world_get_meta(w, x, y, z);

        if (meta < 3) world_set_meta(w, x, y, z, meta + 1, 2);
    }
}

/* Chunk.func_150806_e through World.getTileEntity: the entity the block at the
 * position calls for, made and put when the store has none (World.setTileEntity
 * -> Chunk.func_150812_a, which invalidates whatever was there). The type comes
 * from the block id (and the flower pot's contents from the metadata its
 * createNewTileEntity reads); an entity found invalid comes off the map, as
 * the Java lookup does. */
struct tile_entity *world_tile_entity(struct world *w, int x, int y, int z)
{
    if (y < 0 || y >= 256) return NULL;

    struct chunk *c = chunk_for(w, x, z);

    if (c == NULL) return NULL;

    struct tile_entity *te = te_find(&c->tes, x, y, z);

    if (te == NULL)
    {
        int kind = te_kind_of_block(chunk_block(c, x & 15, y, z & 15));

        if (kind == 0) return NULL;

        struct tile_entity *made = te_put(&c->tes, x, y, z, kind);

        /* the block the entity sits at: the dropper's dispenser entity
         * writes "Dropper" from it */
        made->block = chunk_block(c, x & 15, y, z & 15) & 4095;

/* the flower pot's Item and Data, from the metadata
         * BlockFlowerPot.createNewTileEntity reads */
        if (kind == TE_FLOWER_POT)
            flower_pot_contents(chunk_meta(c, x & 15, y, z & 15), &made->u.pot.item, &made->u.pot.data);

        /* Chunk.func_150806_e registers a newly created tile entity with
         * World as well as the chunk, so updateEntities visits it next tick
         * (world_set_tile_entity also enters it in the registry). */
        world_set_tile_entity(w, x, y, z, made);
        return made;
    }

    if (te->invalid)
    {
        /* it leaves the map; the world lists still own it, so it stays in
         * the registry */
        te_remove(&c->tes, x, y, z);
        return NULL;
    }

    return te;
}

/* ------------------------------------------------ the world lists (field_147482_g) */

/* Every tile entity this world has ever held, in creation order. Java's map
 * only holds references: an entity taken off a chunk map (func_150806_e's
 * invalid branch, func_150812_a's replace, removeTileEntity) is still owned by
 * whatever world list points at it, can come back into a map (the furnace
 * swap puts the same object back), and is never freed twice. The registry
 * gives the same guarantee in C: an entity is registered when it first
 * enters the world and freed exactly once, at world_free. It lives on the
 * world: another world's world_free must not free this world's entities. */
static void te_registry_add(struct world *w, struct tile_entity *te)
{
    for (int i = 0; i < w->te_reg_n; ++i)
        if (w->te_reg[i] == te) return;

    if (w->te_reg_n == w->te_reg_cap)
    {
        w->te_reg_cap = w->te_reg_cap ? w->te_reg_cap * 2 : 64;
        w->te_reg = realloc(w->te_reg, (size_t)w->te_reg_cap * sizeof *w->te_reg);
    }

    w->te_reg[w->te_reg_n++] = te;
}

/* Chunks handed from one world's store to another take their tile entities
 * along: the receiving world owns (and at its world_free frees) them. */
void world_te_registry_move(struct world *dst, struct world *src)
{
    for (int i = 0; i < src->te_reg_n; ++i) te_registry_add(dst, src->te_reg[i]);
    src->te_reg_n = 0;
}

void world_te_register(struct world *w, struct tile_entity *te)
{
    te_registry_add(w, te);
}

int world_te_unregister(struct world *w, struct tile_entity *te)
{
    for (int i = 0; i < w->te_reg_n; ++i)
        if (w->te_reg[i] == te)
        {
            memmove(&w->te_reg[i], &w->te_reg[i + 1], (size_t)(w->te_reg_n - i - 1) * sizeof *w->te_reg);
            --w->te_reg_n;
            return 1;
        }
    return 0;
}

int world_te_listed(const struct world *w, const struct tile_entity *te)
{
    for (int i = 0; i < w->te_n; ++i)
        if (w->te_list[i] == te) return 1;
    for (int i = 0; i < w->te_added_n; ++i)
        if (w->te_added[i] == te) return 1;
    return 0;
}

/* The registry is the world's own: freeing one world (the replay discards its
 * fresh world for the snapshot's) must leave another world's entities alive. */
static void te_registry_free(struct world *w)
{
    for (int i = 0; i < w->te_reg_n; ++i) te_free(w->te_reg[i]);

    free(w->te_reg);
    w->te_reg = NULL;
    w->te_reg_n = w->te_reg_cap = 0;
}

/* World.setTileEntity: the walk branch scans field_147484_a for the position
 * and invalidates it; the ordinary branch appends to field_147482_g and runs
 * Chunk.func_150812_a, which validates and replaces. */
void world_set_tile_entity(struct world *w, int x, int y, int z, struct tile_entity *te)
{
    if (te == NULL || te->invalid) return;

    te_registry_add(w, te);
    te->x = x;
    te->y = y;
    te->z = z;

    if (w->te_ticking)
    {
        for (int i = 0; i < w->te_added_n; ++i)
        {
            if (w->te_added[i]->x == x && w->te_added[i]->y == y && w->te_added[i]->z == z)
            {
                w->te_added[i]->invalid = 1;

                for (int j = i + 1; j < w->te_added_n; ++j) w->te_added[j - 1] = w->te_added[j];

                --w->te_added_n;
                break;
            }
        }

        if (w->te_added_n == w->te_added_cap)
        {
            w->te_added_cap = w->te_added_cap ? w->te_added_cap * 2 : 8;
            w->te_added = realloc(w->te_added, (size_t)w->te_added_cap * sizeof *w->te_added);
        }

        w->te_added[w->te_added_n++] = te;
        return;
    }

    if (w->te_n == w->te_cap)
    {
        w->te_cap = w->te_cap ? w->te_cap * 2 : 64;
        w->te_list = realloc(w->te_list, (size_t)w->te_cap * sizeof *w->te_list);
    }

    w->te_list[w->te_n++] = te;

    struct chunk *c = world_load_chunk(w, x >> 4, z >> 4);
    struct tile_entity *old = te_find(&c->tes, x, y, z);

    if (old != NULL) old->invalid = 1;

    te->invalid = 0;
    /* whatever sat there (a fresh demand entity, or the same pointer again)
     * leaves the store; the caller's entity takes its place directly, as
     * Java's map put does */
    struct tile_entity *sat = te_find(&c->tes, x, y, z);

    if (sat == te) return;

    te_remove(&c->tes, x, y, z);

    if (c->tes.n == c->tes.cap)
    {
        c->tes.cap = c->tes.cap ? c->tes.cap * 2 : 4;
        c->tes.v = realloc(c->tes.v, (size_t)c->tes.cap * sizeof *c->tes.v);
    }

    c->tes.v[c->tes.n++] = te;
}

/* World.removeTileEntity. */
void world_remove_tile_entity(struct world *w, int x, int y, int z)
{
    struct tile_entity *te = world_tile_entity(w, x, y, z);

    if (te != NULL && w->te_ticking)
    {
        te->invalid = 1;

        for (int i = 0; i < w->te_added_n; ++i)
        {
            if (w->te_added[i] == te)
            {
                for (int j = i + 1; j < w->te_added_n; ++j) w->te_added[j - 1] = w->te_added[j];

                --w->te_added_n;
                break;
            }
        }

        return;
    }

    if (te != NULL)
    {
        for (int i = 0; i < w->te_added_n; ++i)
        {
            if (w->te_added[i] == te)
            {
                for (int j = i + 1; j < w->te_added_n; ++j) w->te_added[j - 1] = w->te_added[j];

                --w->te_added_n;
                break;
            }
        }

        for (int i = 0; i < w->te_n; ++i)
        {
            if (w->te_list[i] == te)
            {
                for (int j = i + 1; j < w->te_n; ++j) w->te_list[j - 1] = w->te_list[j];

                --w->te_n;
                break;
            }
        }
    }

    struct chunk *c = world_chunk(w, x >> 4, z >> 4);

    if (c != NULL) te_remove(&c->tes, x, y, z);
}

/* World.func_147457_a, the chunk-unload mark. */
void world_mark_tile_entity_gone(struct world *w, struct tile_entity *te)
{
    te->invalid = 1;
    (void)w;
}

/* WorldServer.func_147452_c: a BlockEventData joins field_147490_S unless an
 * equal one (the position, the block, the event and the parameter) is
 * queued. Only a live server world queues; only the chest's and the ender
 * chest's event 1 have a server-side effect (the rest reach the clients
 * alone), so only they are kept. */
void world_block_event(struct world *w, int x, int y, int z, int block, int id, int param)
{
    if (!w->bev_on || id != 1 || (block != 54 && block != 146 && block != 130)) return;

    for (int i = 0; i < w->bev_n; ++i)
    {
        const int32_t *v = w->bev[i];
        if (v[0] == x && v[1] == y && v[2] == z && v[3] == block && v[4] == id && v[5] == param) return;
    }

    if (w->bev_n == WORLD_BEV_MAX)
    {
        fprintf(stderr, "world: over %d distinct chest block events in one tick\n", WORLD_BEV_MAX);
        abort();
    }

    int32_t *v = w->bev[w->bev_n++];
    v[0] = x; v[1] = y; v[2] = z; v[3] = block; v[4] = id; v[5] = param;
}

/* WorldServer.func_147488_Z at the end of WorldServer.tick: each queued
 * event, in order, reaches func_147485_a when the block it names still
 * stands there; BlockContainer.onBlockEventReceived hands it to the tile
 * (getTileEntity), whose receiveClientEvent(1, n) sets numPlayersUsing
 * (TileEntityChest.field_145987_o, TileEntityEnderChest.field_145973_j) to
 * n. The S24 each sends reaches no state the rows read. */
void world_block_events_run(struct world *w)
{
    for (int i = 0; i < w->bev_n; ++i)
    {
        const int32_t *v = w->bev[i];

        if ((world_get_block(w, v[0], v[1], v[2]) & 4095) != v[3]) continue;

        struct tile_entity *te = world_tile_entity(w, v[0], v[1], v[2]);

        if (te != NULL && (te->kind == TE_CHEST || te->kind == TE_ENDER_CHEST))
            te->u.chest.players_using = v[5];
    }

    w->bev_n = 0;
}
