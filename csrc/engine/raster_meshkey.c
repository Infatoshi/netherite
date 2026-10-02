/* The key of a section's mesh: raster_meshkey.h. */
#include "raster_meshkey.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "render_blocks.h"
#include "world.h"

/* Four 64-bit lanes over 8-byte words, word i to lane i mod 4 (xxh64's
 * round: acc = rotl(acc + w * P2, 31) * P1, a bijection of the lane for any
 * word): four independent chains, so the multiplies overlap. The key is
 * two 64-bit halves, each a bijection of one lane for the other fixed. */
struct mkh { uint64_t l0, l1, l2, l3; };

static const uint64_t MK_P1 = 0x9e3779b185ebca87ull, MK_P2 = 0xc2b2ae3d27d4eb4full;

static inline uint64_t rotl64(uint64_t x, int r) { return x << r | x >> (64 - r); }

static inline uint64_t mk_round(uint64_t acc, uint64_t w) { return rotl64(acc + w * MK_P2, 31) * MK_P1; }

/* one word: the lanes turn, so the next word goes to the next lane */
static inline void mk_word(struct mkh *h, uint64_t w)
{
    uint64_t t = mk_round(h->l0, w);
    h->l0 = h->l1;
    h->l1 = h->l2;
    h->l2 = h->l3;
    h->l3 = t;
}

static inline void mk_bytes(struct mkh *h, const uint8_t *p, size_t n)
{
    mk_word(h, n);
    /* four words at a time: each lane one, as four turns would give them */
    for (; n >= 32; p += 32, n -= 32) {
        uint64_t w[4];
        memcpy(w, p, 32);
        h->l0 = mk_round(h->l0, w[0]);
        h->l1 = mk_round(h->l1, w[1]);
        h->l2 = mk_round(h->l2, w[2]);
        h->l3 = mk_round(h->l3, w[3]);
    }
    for (; n >= 8; p += 8, n -= 8) {
        uint64_t w;
        memcpy(&w, p, 8);
        mk_word(h, w);
    }
    if (n) {
        uint64_t w = 0;
        memcpy(&w, p, n);
        mk_word(h, w);
    }
}

static inline uint64_t mk_fmix(uint64_t k)
{
    k ^= k >> 33; k *= 0xff51afd7ed558ccdull;
    k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ull;
    return k ^ (k >> 33);
}

/* the window's record of (cx, cz), as render_blocks.c's chunk_in reads it:
 * *in 0 outside the window (every read 0), else the record (NULL: zeros) */
static const struct chunk *window_rec(const struct rb_world *w, int cx, int cz, int *in)
{
    int dx = cx - (w->origin_cx - w->margin), dz = cz - (w->origin_cz - w->margin);
    *in = dx >= 0 && dx < w->rows && dz >= 0 && dz < w->rows;
    return *in ? w->chunks[(size_t)dx * (size_t)w->rows + (size_t)dz] : NULL;
}

/* the passes band B's cells are drawn in */
static int band_passes(const struct chunk_sec *b, const uint8_t *pass)
{
    int m = 0;
    if (b->ids_hi) {
        for (int i = 0; i < SEC_CELLS && m != 3; ++i) m |= pass[chunk_sec_id(b, i)];
        return m;
    }
    /* eight cells at a time, a run of the eight before (air, stone) once */
    uint64_t last = 0;
    for (int i = 0; i < SEC_CELLS && m != 3; i += 8) {
        uint64_t v;
        memcpy(&v, b->ids + i, 8);
        if (v == last) continue;
        last = v;
        for (int j = 0; j < 8; ++j) m |= pass[b->ids[i + j]];
    }
    return m;
}

/* band B's cells in columns [x0, x1] x [z0, z1], every y (the band's index
 * is x << 8 | z << 4 | y, so a column range at one x is one run) */
static void band_full(struct mkh *h, const struct chunk_sec *b, int x0, int x1, int z0, int z1)
{
    if (!b) { mk_word(h, 0); return; }
    mk_word(h, b->ids_hi ? 3 : 1);
    if (x0 == 0 && x1 == 15 && z0 == 0 && z1 == 15) {
        mk_bytes(h, b->ids, SEC_CELLS);
        mk_bytes(h, chunk_sec_metas(b), SEC_CELLS / 2);
        mk_bytes(h, chunk_sec_sky(b), SEC_CELLS / 2);
        mk_bytes(h, chunk_sec_blocklight(b), SEC_CELLS / 2);
        if (b->ids_hi) mk_bytes(h, b->ids_hi, SEC_CELLS / 2);
        return;
    }
    int n = (z1 - z0 + 1) << 4;
    for (int x = x0; x <= x1; ++x) {
        int c = x << 8 | z0 << 4;
        mk_bytes(h, b->ids + c, (size_t)n);
        mk_bytes(h, chunk_sec_metas(b) + (c >> 1), (size_t)n / 2);
        mk_bytes(h, chunk_sec_sky(b) + (c >> 1), (size_t)n / 2);
        mk_bytes(h, chunk_sec_blocklight(b) + (c >> 1), (size_t)n / 2);
        if (b->ids_hi) mk_bytes(h, b->ids_hi + (c >> 1), (size_t)n / 2);
    }
}

/* band B's cells y0 and y0 + 1 (y0 even: one byte of each nibble array)
 * in columns [x0, x1] x [z0, z1] */
static void band_rows(struct mkh *h, const struct chunk_sec *b, int y0, int x0, int x1, int z0, int z1)
{
    if (!b) { mk_word(h, 0); return; }
    mk_word(h, b->ids_hi ? 3 : 1);
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            int c = x << 8 | z << 4 | y0;
            uint64_t v = (uint64_t)b->ids[c] | (uint64_t)b->ids[c + 1] << 8 |
                         (uint64_t)chunk_sec_metas(b)[c >> 1] << 16 | (uint64_t)chunk_sec_sky(b)[c >> 1] << 24 |
                         (uint64_t)chunk_sec_blocklight(b)[c >> 1] << 32 |
                         (uint64_t)(b->ids_hi ? b->ids_hi[c >> 1] : 0) << 40;
            mk_word(h, v);
        }
}

/* a band's 27 kinds: where it sits (0 the section's own y, 1 the band
 * below it, 2 above) times the x and z columns read (0: 13..15, 1: 0..15,
 * 2: 0..2) */
enum { MK_KINDS = 27, MK_PAGE = 512 };
static const int MK_LO[3] = {13, 0, 0}, MK_HI[3] = {15, 15, 2};

static uint64_t mk_fin64(const struct mkh *h)
{
    return mk_fmix(h->l0 ^ mk_fmix(h->l1 ^ mk_fmix(h->l2 ^ mk_fmix(h->l3 + MK_P1))));
}

/* the hash of what a key reads from band B in KIND */
static uint64_t band_kind_hash(const struct chunk_sec *b, int kind)
{
    struct mkh h = {0x452821e638d01377ull, 0xbe5466cf34e90c6cull, 0xc0ac29b7c97c50ddull, 0x3f84d5b5b5470917ull};
    int y = kind / 9, x0 = MK_LO[kind / 3 % 3], x1 = MK_HI[kind / 3 % 3], z0 = MK_LO[kind % 3], z1 = MK_HI[kind % 3];
    if (y == 0) band_full(&h, b, x0, x1, z0, z1);
    else band_rows(&h, b, y == 1 ? 14 : 0, x0, x1, z0, z1);
    return mk_fin64(&h);
}

/* a band slot's kept hashes: valid for generation gen1 - 1 of the slot,
 * kind k made when bit k of have is set */
struct mk_band {
    uint32_t gen1, have;
    uint8_t info;                 /* MK_INFO set: the passes its cells are drawn in (bits 0-1), MK_POT: a flower pot */
    uint64_t cid;
    uint64_t h[MK_KINDS];
};
enum { MK_POT = 4, MK_INFO = 0x80 };
struct meshkey_cache {
    struct mk_band **page;        /* ARENA_BANDS / MK_PAGE of them, made when first used */
    int verify;
    uint64_t checked, bad;
    uint64_t serial;              /* the last content serial given */
};

struct meshkey_cache *meshkey_cache_new(void)
{
    struct meshkey_cache *mc = calloc(1, sizeof *mc);
    if (mc) mc->page = calloc(ARENA_BANDS / MK_PAGE, sizeof *mc->page);
    if (!mc || !mc->page) { fprintf(stderr, "meshkey: out of memory\n"); exit(1); }
    return mc;
}

void meshkey_cache_free(struct meshkey_cache *mc)
{
    if (!mc) return;
    for (int i = 0; i < ARENA_BANDS / MK_PAGE; ++i) free(mc->page[i]);
    free(mc->page);
    free(mc);
}

void meshkey_cache_verify(struct meshkey_cache *mc, int on) { mc->verify = on; }

/* B's entry, made current for its slot's generation (NULL outside the pool) */
static struct mk_band *band_entry(struct meshkey_cache *mc, const struct chunk_sec *b, int32_t *slot)
{
    const struct slot_pool *pool = &nw_arena()->bands;
    if (!mc || !b || !pool_holds(pool, b)) return NULL;
    int32_t i = pool_index(pool, b);
    struct mk_band **pg = &mc->page[i / MK_PAGE];
    if (!*pg) {
        *pg = calloc(MK_PAGE, sizeof **pg);
        if (!*pg) { fprintf(stderr, "meshkey: out of memory\n"); exit(1); }
    }
    struct mk_band *e = &(*pg)[i % MK_PAGE];
    uint32_t gen1 = pool->gen[i] + 1;
    if (e->gen1 != gen1) { e->gen1 = gen1; e->have = 0; e->info = 0; e->cid = ++mc->serial; }
    *slot = i;
    return e;
}

void meshkey_cache_taken(struct meshkey_cache *mc, const struct chunk_sec *b)
{
    int32_t i;
    struct mk_band *e = band_entry(mc, b, &i);
    if (e) { e->have = 0; e->info = 0; e->cid = ++mc->serial; }
}

void meshkey_cache_same(struct meshkey_cache *mc, const struct chunk_sec *from, const struct chunk_sec *b)
{
    int32_t i, j;
    struct mk_band *f = band_entry(mc, from, &i), *e = band_entry(mc, b, &j);
    if (!e) return;
    e->have = f ? f->have : 0;
    e->info = f ? f->info : 0;
    e->cid = f ? f->cid : ++mc->serial;
    if (e->have) memcpy(e->h, f->h, sizeof e->h);
}

uint64_t meshkey_band_id(struct meshkey_cache *mc, const struct chunk_sec *b)
{
    int32_t i;
    struct mk_band *e = band_entry(mc, b, &i);
    return e ? e->cid : 0;
}

void meshkey_cache_count(struct meshkey_cache *mc, int bad)
{
    ++mc->checked;
    if (bad) ++mc->bad;
}

int meshkey_cache_verifying(const struct meshkey_cache *mc) { return mc && mc->verify; }

void meshkey_cache_stats(const struct meshkey_cache *mc, uint64_t *checked, uint64_t *bad)
{
    *checked = mc->checked;
    *bad = mc->bad;
}

/* the hash of band B (not NULL) in KIND, kept in MC when B is a band of the
 * environment's pool */
static uint64_t band_hash(struct meshkey_cache *mc, const struct chunk_sec *b, int kind)
{
    int32_t i;
    struct mk_band *e = band_entry(mc, b, &i);
    if (!e) return band_kind_hash(b, kind);
    if (e->have >> kind & 1) {
        if (mc->verify) {
            ++mc->checked;
            if (band_kind_hash(b, kind) != e->h[kind]) {
                ++mc->bad;
                fprintf(stderr, "meshkey: band slot %d's kept hash (kind %d) differs from its bytes' now\n", i, kind);
                e->h[kind] = band_kind_hash(b, kind);
            }
        }
        return e->h[kind];
    }
    e->h[kind] = band_kind_hash(b, kind);
    e->have |= 1u << kind;
    return e->h[kind];
}

/* the passes band B's cells are drawn in, and MK_POT when it holds a
 * flower pot */
static int band_info_of(const struct chunk_sec *b, const uint8_t *pass)
{
    return band_passes(b, pass) | (memchr(b->ids, 140, SEC_CELLS) ? MK_POT : 0);
}

/* band_info_of, kept with B's hashes (the pass table is the renderer's,
 * fixed for its life, as the cache is) */
static int band_info(struct meshkey_cache *mc, const struct chunk_sec *b, const uint8_t *pass)
{
    int32_t i;
    struct mk_band *e = band_entry(mc, b, &i);
    if (!e) return band_info_of(b, pass);
    if (e->info & MK_INFO) {
        if (mc->verify) {
            ++mc->checked;
            if ((band_info_of(b, pass) | MK_INFO) != e->info) {
                ++mc->bad;
                fprintf(stderr, "meshkey: band slot %d's kept passes differ from its bytes' now\n", i);
            }
        }
        return e->info & ~MK_INFO;
    }
    e->info = (uint8_t)(band_info_of(b, pass) | MK_INFO);
    return e->info & ~MK_INFO;
}

const struct chunk_sec *meshkey_band(const struct rb_world *w, int cx, int s, int cz)
{
    int in;
    const struct chunk *c = w->chunks && s >= 0 && s < 16 ? window_rec(w, cx, cz, &in) : NULL;
    return c ? chunk_sec_at(c, s) : NULL;
}

int meshkey_drawn(const struct rb_world *w, const uint8_t *pass, struct meshkey_cache *mc, int cx, int s, int cz)
{
    if (!w->chunks || s < 0 || s > 15) return 3;
    int in;
    const struct chunk *own = window_rec(w, cx, cz, &in);
    const struct chunk_sec *sec = own ? chunk_sec_at(own, s) : NULL;
    int info = sec ? band_info(mc, sec, pass) : 0;
    return info & MK_POT ? 3 : info & 3;
}

int meshkey_section(const struct rb_world *w, const uint8_t *pass, struct meshkey_cache *mc, int cx, int s, int cz,
                    int px, int py, int pz, struct meshkey *k, int *drawn)
{
    *drawn = 3;
    if (!w->chunks || s < 0 || s > 15) return MESHKEY_NONE;
    int in;
    const struct chunk *own = window_rec(w, cx, cz, &in);
    const struct chunk_sec *sec = own ? chunk_sec_at(own, s) : NULL;
    /* rb_mesh_section draws only the section's own cells, each in its pass
     * (a cell outside the window or its record reads air) */
    int info = sec ? band_info(mc, sec, pass) : 0;
    *drawn = info & 3;
    if (!*drawn) return MESHKEY_EMPTY;
    /* a flower pot draws its tile entity's plant */
    if (info & MK_POT) {
        *drawn = 3;
        return MESHKEY_NONE;
    }

    struct mkh h = {0x243f6a8885a308d3ull, 0x13198a2e03707344ull, 0xa4093822299f31d0ull, 0x082efa98ec4e6c89ull};
    mk_word(&h, (uint64_t)(uint32_t)cx | (uint64_t)(uint32_t)cz << 32);
    mk_word(&h, (uint64_t)s | (uint64_t)(w->no_sky != 0) << 8);
    int rx = px - (cx << 4), ry = py - (s << 4), rz = pz - (cz << 4);
    int inside = rx >= 0 && rx < 16 && ry >= 0 && ry < 16 && rz >= 0 && rz < 16;
    mk_word(&h, inside ? (uint64_t)(rx << 8 | ry << 4 | rz) : ~0ull);

    int lo = s > 0 ? s - 1 : 0, hi = s < 15 ? s + 1 : 15;
    unsigned full = (1u << (hi - lo + 1)) - 1;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            /* x and z within 3 of the section: the chunk's columns */
            int x0 = i == 0 ? 13 : 0, x1 = i == 2 ? 2 : 15;
            int z0 = j == 0 ? 13 : 0, z1 = j == 2 ? 2 : 15;
            const struct chunk *c = window_rec(w, cx - 1 + i, cz - 1 + j, &in);
            if (!in) { mk_word(&h, 2); continue; }
            if (!c) { mk_word(&h, 1); continue; }
            unsigned bits = ((unsigned)c->mask >> lo) & full;
            mk_word(&h, 4 | (uint64_t)bits << 8);
            /* y within 2 of the section: its band whole, two rows of each
             * neighbour (a y past the world's reads a constant or, above,
             * row 255, which is the band's) */
            for (int t = lo; t <= hi; ++t) {
                const struct chunk_sec *b = chunk_sec_at(c, t);
                mk_word(&h, b ? band_hash(mc, b, (t == s ? 0 : t < s ? 9 : 18) + i * 3 + j) : 0);
            }
            /* the sky light of a section the mask does not hold */
            if (bits != full)
                for (int z = z0; z <= z1; ++z)
                    mk_bytes(&h, (const uint8_t *)&c->height[z << 4 | x0], (size_t)(x1 - x0 + 1) * sizeof c->height[0]);
            for (int z = z0; z <= z1; ++z) mk_bytes(&h, &c->biome[z << 4 | x0], (size_t)(x1 - x0 + 1));
        }
    k->h[0] = mk_fmix(h.l0 ^ mk_fmix(h.l1 + MK_P1));
    k->h[1] = mk_fmix(h.l2 ^ mk_fmix(h.l3 + MK_P2));
    return MESHKEY_OK;
}
