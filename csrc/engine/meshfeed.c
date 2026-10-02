/* The device mesher's feed (meshfeed.h). */
#include "meshfeed.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raster_meshkey.h"
#include "world.h"

typedef char mf_band_bytes_fit[sizeof(struct chunk_sec_flat) + 2048 == MF_BAND_BYTES ? 1 : -1];
/* a band image's granules, the last one shorter (a multiple of 8 bytes) */
enum { MF_GRANULES = (MF_BAND_BYTES + MF_GRANULE - 1) / MF_GRANULE };
typedef char mf_granule_words[MF_BAND_BYTES % 8 == 0 && MF_GRANULE % 8 == 0 ? 1 : -1];

/* a band image's header: ids_hi 1 when hi, the nibble arrays inline (world.h
 * struct chunk_sec_flat), the rest 0 */
static void band_image_head(unsigned char *p, uint64_t hi)
{
    int32_t nib[NIB_N];
    for (int k = 0; k < NIB_N; ++k) nib[k] = SEC_FLAT_NIB(k);
    memset(p, 0, sizeof(struct chunk_sec));
    memcpy(p, &hi, sizeof hi);
    memcpy(p + offsetof(struct chunk_sec, nib), nib, sizeof nib);
}

/* a chunk the device holds: its slots, the stale marks since its last
 * upload, and what its record was sent with */
struct mf_chunk {
    int32_t cx, cz, slot;
    int32_t band[16];
    /* each band slot's content serial (meshkey_band_id) when last sent
     * whole or by runs (0: not known) */
    uint64_t sent_id[16];
    uint32_t dirty;
    uint64_t last;
    uint16_t mask;
    int32_t height[256];
    uint8_t biome[256];
};

/* a growable array */
struct mf_arr { void *p; size_t n, cap, size; };

static void *arr_add(struct mf_arr *a, size_t k)
{
    if (a->n + k > a->cap)
    {
        size_t cap = a->cap ? a->cap : 64;
        while (cap < a->n + k) cap *= 2;
        void *p = realloc(a->p, cap * a->size);
        if (!p) abort();
        a->p = p;
        a->cap = cap;
    }
    void *r = (unsigned char *)a->p + a->n * a->size;
    a->n += k;
    return r;
}

/* a slot's ops in the feed being built (first index and count, for the
 * feed gen: a later op for the slot in the same feed replaces them); a band
 * slot's image as the device holds it is its shadow, a hash of each granule
 * (0 for a granule of zeros: every one at an epoch's start, as the
 * device's). The bytes themselves were 12,320 a slot, 6 MB an env at the
 * speedrun start (lane/heapprof); the hashes are 1,544. */
struct mf_slot {
    uint32_t gen;
    int32_t first, n;
};

struct meshfeed {
    struct mf_chunk *ch;          /* the held chunks */
    int nch, capch;
    int32_t *hash;                /* index + 1 into ch, linear probing; 0 empty */
    int hcap;
    struct mf_arr free_chunk, free_band;
    int chunk_slots, band_slots;
    uint64_t frame;
    uint32_t epoch, seq;
    int taken;                    /* the last feed went out: the next frame starts a new one */
    struct mf_arr req, te, trig, op, payload;
    struct meshfeed_out out;
    uint32_t gen;                 /* the feed being built (one more at each start) */
    /* the band slots' shadows and records, the chunk slots' records */
    uint64_t *shadow;
    struct mf_slot *bslot, *cslot;
    int cap_b, cap_c;
    struct meshkey_cache *mc;     /* meshfeed_band_ids */
};

static uint32_t mf_hash(int cx, int cz)
{
    uint64_t k = (uint64_t)(uint32_t)cx << 32 | (uint32_t)cz;
    k *= 0x9e3779b97f4a7c15ull;
    return (uint32_t)(k >> 32);
}

static void rehash(struct meshfeed *f)
{
    int need = 64;
    while (need < 2 * f->nch + 2) need *= 2;
    if (need != f->hcap)
    {
        free(f->hash);
        f->hash = malloc((size_t)need * sizeof *f->hash);
        if (!f->hash) abort();
        f->hcap = need;
    }
    memset(f->hash, 0, (size_t)f->hcap * sizeof *f->hash);
    for (int i = 0; i < f->nch; ++i)
    {
        uint32_t h = mf_hash(f->ch[i].cx, f->ch[i].cz) & (uint32_t)(f->hcap - 1);
        while (f->hash[h]) h = (h + 1) & (uint32_t)(f->hcap - 1);
        f->hash[h] = i + 1;
    }
}

static struct mf_chunk *find(struct meshfeed *f, int cx, int cz)
{
    if (!f->hcap) return NULL;
    uint32_t h = mf_hash(cx, cz) & (uint32_t)(f->hcap - 1);
    for (; f->hash[h]; h = (h + 1) & (uint32_t)(f->hcap - 1))
    {
        struct mf_chunk *c = &f->ch[f->hash[h] - 1];
        if (c->cx == cx && c->cz == cz) return c;
    }
    return NULL;
}

static int take_slot(struct mf_arr *free_list, int *count)
{
    if (free_list->n) return ((int32_t *)free_list->p)[--free_list->n];
    return (*count)++;
}

static void give_slot(struct mf_arr *free_list, int slot)
{
    *(int32_t *)arr_add(free_list, 1) = slot;
}

struct meshfeed *meshfeed_new(void)
{
    struct meshfeed *f = calloc(1, sizeof *f);
    if (!f) abort();
    f->free_chunk.size = f->free_band.size = sizeof(int32_t);
    f->req.size = sizeof(struct meshfeed_req);
    f->te.size = sizeof(struct rb_te);
    f->trig.size = sizeof(struct rb_trig);
    f->op.size = sizeof(struct meshfeed_op);
    f->payload.size = 1;
    f->taken = 1;
    return f;
}

void meshfeed_band_ids(struct meshfeed *f, struct meshkey_cache *mc) { f->mc = mc; }

void meshfeed_free(struct meshfeed *f)
{
    if (!f) return;
    free(f->ch);
    free(f->hash);
    free(f->free_chunk.p);
    free(f->free_band.p);
    free(f->req.p);
    free(f->te.p);
    free(f->trig.p);
    free(f->op.p);
    free(f->payload.p);
    free(f->shadow);
    free(f->bslot);
    free(f->cslot);
    free(f);
}

void meshfeed_reset(struct meshfeed *f)
{
    /* the device holds nothing: its band slots read zero */
    if (f->shadow) memset(f->shadow, 0, (size_t)f->cap_b * MF_GRANULES * sizeof *f->shadow);
    f->nch = 0;
    f->free_chunk.n = f->free_band.n = 0;
    f->chunk_slots = f->band_slots = 0;
    ++f->epoch;
    f->seq = 0;
    f->taken = 1;
    rehash(f);
}

void meshfeed_dirty(struct meshfeed *f, int cx, int s, int cz)
{
    struct mf_chunk *c = find(f, cx, cz);
    if (c && s >= 0 && s < 16) c->dirty |= 1u << s;
}

void meshfeed_dirty_all(struct meshfeed *f)
{
    for (int i = 0; i < f->nch; ++i) f->ch[i].dirty = 0xffff;
}

void meshfeed_begin(struct meshfeed *f)
{
    if (f->taken) { f->req.n = f->te.n = f->trig.n = f->op.n = f->payload.n = 0; ++f->gen; }
    f->taken = 0;
    ++f->frame;
}

const struct meshfeed_req *meshfeed_requests(const struct meshfeed *f, int *n)
{
    *n = (int)f->req.n;
    return f->req.p;
}

void meshfeed_keep(struct meshfeed *f, const unsigned char *keep)
{
    struct meshfeed_req *q = f->req.p;
    size_t k = 0;
    for (size_t i = 0; i < f->req.n; ++i) if (keep[i]) q[k++] = q[i];
    f->req.n = k;
}

void meshfeed_take(struct meshfeed *f)
{
    f->taken = 1;
    ++f->seq;
}

/* the window's record of (cx, cz): 1 and *rec (NULL: a record of zeros)
 * when the window holds the chunk's place, 0 outside it (render_blocks.c
 * chunk_in) */
static int window_record(const struct rb_world *w, int cx, int cz, const struct chunk **rec)
{
    int dx = cx - (w->origin_cx - w->margin), dz = cz - (w->origin_cz - w->margin);
    if (dx < 0 || dx >= w->rows || dz < 0 || dz >= w->rows) return 0;
    *rec = w->chunks[(size_t)dx * (size_t)w->rows + (size_t)dz];
    return 1;
}

/* slot I's record (the arrays grown to hold it) */
static struct mf_slot *slot_rec(struct mf_slot **a, int *cap, uint64_t **shadow, int i)
{
    if (i >= *cap)
    {
        int n = *cap ? *cap : 64;
        while (n <= i) n *= 2;
        *a = realloc(*a, (size_t)n * sizeof **a);
        if (!*a) abort();
        memset(*a + *cap, 0, (size_t)(n - *cap) * sizeof **a);
        if (shadow)
        {
            *shadow = realloc(*shadow, (size_t)n * MF_GRANULES * sizeof **shadow);
            if (!*shadow) abort();
            memset(*shadow + (size_t)*cap * MF_GRANULES, 0, (size_t)(n - *cap) * MF_GRANULES * sizeof **shadow);
        }
        *cap = n;
    }
    return &(*a)[i];
}

/* a new op for slot record R: an op of R's already in this feed is
 * replaced (kind MF_SKIP); the new op's index */
static struct meshfeed_op *slot_op(struct meshfeed *f, struct mf_slot *r, int whole)
{
    struct meshfeed_op *ops = f->op.p;
    if (r->gen == f->gen && r->n > 0 && whole)
    {
        for (int i = r->first; i < r->first + r->n; ++i) ops[i].kind = MF_SKIP;
        r->n = 0;
    }
    if (r->gen != f->gen || r->n == 0)
    {
        r->gen = f->gen;
        r->first = (int32_t)f->op.n;
        r->n = 0;
    }
    ++r->n;
    struct meshfeed_op *op = arr_add(&f->op, 1);
    memset(op, 0, sizeof *op);
    return op;
}

/* a granule's hash: 0 for zeros (a slot's state at an epoch's start),
 * else two lanes over its words, mixed (raster_meshkey.c's keys trust a
 * 64-bit content hash the same way) */
static uint64_t gran_fmix(uint64_t k)
{
    k ^= k >> 33; k *= 0xff51afd7ed558ccdull;
    k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ull;
    return k ^ (k >> 33);
}

static uint64_t gran_hash(const unsigned char *p, int len)
{
    uint64_t a = 0x452821e638d01377ull ^ (uint64_t)len, b = 0xbe5466cf34e90c6cull, any = 0;
    for (int i = 0; i < len; i += 8)
    {
        uint64_t w;
        memcpy(&w, p + i, 8);
        any |= w;
        a = (a ^ w) * 0x9e3779b97f4a7c15ull;
        a ^= a >> 29;
        b = (b + w) * 0xc2b2ae3d27d4eb4full;
        b = b << 31 | b >> 33;
    }
    return any ? gran_fmix(a ^ gran_fmix(b + 0x3f84d5b5b5470917ull)) : 0;
}

static int gran_len(int g)
{
    int o = g * MF_GRANULE;
    return MF_BAND_BYTES - o < MF_GRANULE ? MF_BAND_BYTES - o : MF_GRANULE;
}

/* band SEC's image as the device's copy is compared (the header
 * normalized), at the payload's end: its offset */
static size_t band_image(struct meshfeed *f, const struct chunk_sec *sec)
{
    size_t at = f->payload.n;
    unsigned char *p = arr_add(&f->payload, MF_BAND_BYTES);
    uint64_t hi = sec->ids_hi != NULL;
    band_image_head(p, hi);
    memcpy(p + offsetof(struct chunk_sec, ids), sec->ids, sizeof sec->ids);
    for (int k = 0; k < NIB_N; ++k) memcpy(p + SEC_FLAT_NIB(k), chunk_sec_nib(sec, k), SEC_NIB_BYTES);
    if (sec->ids_hi) memcpy(p + sizeof(struct chunk_sec_flat), sec->ids_hi, 2048);
    else memset(p + sizeof(struct chunk_sec_flat), 0, 2048);
    return at;
}

/* band SEC into band slot SLOT: the granules that differ from what the
 * slot holds; the whole image when the slot was sent earlier in this feed
 * (runs of both would overlap in one launch) */
static void send_band(struct meshfeed *f, int slot, const struct chunk_sec *sec)
{
    struct mf_slot *r = slot_rec(&f->bslot, &f->cap_b, &f->shadow, slot);
    uint64_t *sh = f->shadow + (size_t)slot * MF_GRANULES;
    size_t at = band_image(f, sec);
    const unsigned char *p = (const unsigned char *)f->payload.p + at;
    uint64_t hi = sec->ids_hi != NULL;
    int whole = r->gen == f->gen && r->n > 0;
    if (whole)
    {
        struct meshfeed_op *op = slot_op(f, r, 1);
        op->kind = MF_BAND;
        op->slot = slot;
        op->hi = (int32_t)hi;
        op->off = at;
        for (int g = 0; g < MF_GRANULES; ++g) sh[g] = gran_hash(p + g * MF_GRANULE, gran_len(g));
        return;
    }
    /* the runs of changed granules, each moved down from the image to the
     * payload's end as it goes (a run never lands past where it is read) */
    int runs[2 * (MF_BAND_BYTES / MF_GRANULE + 1)], nrun = 0;
    for (int g = 0; g < MF_GRANULES; ++g)
    {
        int o = g * MF_GRANULE, len = gran_len(g);
        uint64_t h = gran_hash(p + o, len);
        if (h == sh[g]) continue;
        sh[g] = h;
        if (nrun && runs[2 * nrun - 2] + runs[2 * nrun - 1] == o) runs[2 * nrun - 1] += len;
        else { runs[2 * nrun] = o; runs[2 * nrun + 1] = len; ++nrun; }
    }
    f->payload.n = at;
    for (int i = 0; i < nrun; ++i)
    {
        size_t off = f->payload.n;
        unsigned char *dst = arr_add(&f->payload, (size_t)runs[2 * i + 1]);
        memmove(dst, (unsigned char *)f->payload.p + at + runs[2 * i], (size_t)runs[2 * i + 1]);
        struct meshfeed_op *op = slot_op(f, r, 0);
        op->kind = MF_BAND_RUN;
        op->slot = slot;
        op->hi = (int32_t)hi;
        op->off = off;
        op->band[0] = runs[2 * i];
        op->band[1] = runs[2 * i + 1];
    }
}

/* 1 when band SEC's image differs from what band slot SLOT's shadow holds
 * (the check of a band not sent again) */
static int band_differs(struct meshfeed *f, int slot, const struct chunk_sec *sec)
{
    const uint64_t *sh = f->shadow + (size_t)slot * MF_GRANULES;
    size_t at = band_image(f, sec);
    const unsigned char *p = (const unsigned char *)f->payload.p + at;
    int bad = 0;
    for (int g = 0; g < MF_GRANULES && !bad; ++g) bad = gran_hash(p + g * MF_GRANULE, gran_len(g)) != sh[g];
    f->payload.n = at;
    if (bad) fprintf(stderr, "meshfeed: band slot %d's unsent band differs from what the device holds\n", slot);
    return bad;
}

static void drop(struct meshfeed *f, struct mf_chunk *c)
{
    give_slot(&f->free_chunk, c->slot);
    for (int s = 0; s < 16; ++s) if (c->band[s] >= 0) give_slot(&f->free_band, c->band[s]);
    *c = f->ch[--f->nch];
    rehash(f);
}

/* the device's copy of REC made current: its stale bands and, when its
 * bands' slots or its header moved, its record */
static int32_t hold(struct meshfeed *f, int cx, int cz, const struct chunk *rec)
{
    struct mf_chunk *c = find(f, cx, cz);
    int header = 0;
    /* made current earlier in this frame, with no stale mark since: the
     * record has not moved (the window changes only between frames) */
    if (c && c->last == f->frame && !c->dirty) return c->slot;
    if (!c)
    {
        if (f->nch == f->capch)
        {
            f->capch = f->capch ? 2 * f->capch : 64;
            f->ch = realloc(f->ch, (size_t)f->capch * sizeof *f->ch);
            if (!f->ch) abort();
        }
        c = &f->ch[f->nch++];
        memset(c, 0, sizeof *c);
        c->cx = cx;
        c->cz = cz;
        c->slot = take_slot(&f->free_chunk, &f->chunk_slots);
        for (int s = 0; s < 16; ++s) c->band[s] = -1;
        c->dirty = 0xffff;
        header = 1;
        if (2 * f->nch + 2 > f->hcap) rehash(f);
        else
        {
            uint32_t h = mf_hash(cx, cz) & (uint32_t)(f->hcap - 1);
            while (f->hash[h]) h = (h + 1) & (uint32_t)(f->hcap - 1);
            f->hash[h] = f->nch;
        }
    }
    for (int s = 0; c->dirty && s < 16; ++s)
    {
        if (!(c->dirty & (1u << s))) continue;
        const struct chunk_sec *sec = chunk_sec_at(rec, s);
        if (!sec)
        {
            if (c->band[s] >= 0) { give_slot(&f->free_band, c->band[s]); c->band[s] = -1; header = 1; }
            c->sent_id[s] = 0;
            continue;
        }
        if (c->band[s] < 0) { c->band[s] = take_slot(&f->free_band, &f->band_slots); header = 1; c->sent_id[s] = 0; }
        /* the bytes sent last (records held them since): the slot holds them */
        uint64_t id = meshkey_band_id(f->mc, sec);
        if (id && c->sent_id[s] == id) {
            if (meshkey_cache_verifying(f->mc))
                meshkey_cache_count(f->mc, band_differs(f, c->band[s], sec));
            continue;
        }
        send_band(f, c->band[s], sec);
        c->sent_id[s] = id;
    }
    c->dirty = 0;
    if (header || c->mask != rec->mask || memcmp(c->height, rec->height, sizeof c->height) ||
        memcmp(c->biome, rec->biome, sizeof c->biome))
    {
        c->mask = rec->mask;
        memcpy(c->height, rec->height, sizeof c->height);
        memcpy(c->biome, rec->biome, sizeof c->biome);
        struct meshfeed_op *op = slot_op(f, slot_rec(&f->cslot, &f->cap_c, NULL, c->slot), 1);
        op->kind = MF_CHUNK;
        op->slot = c->slot;
        memcpy(op->band, c->band, sizeof op->band);
        op->off = f->payload.n;
        struct chunk *img = arr_add(&f->payload, sizeof(struct chunk));
        memset(img, 0, sizeof *img);
        img->cx = cx;
        img->cz = cz;
        img->mask = rec->mask;
        img->no_sky = rec->no_sky;
        memcpy(img->height, rec->height, sizeof img->height);
        memcpy(img->precip, rec->precip, sizeof img->precip);
        memcpy(img->biome, rec->biome, sizeof img->biome);
    }
    c->last = f->frame;
    return c->slot;
}

static int trig_order(const void *a, const void *b)
{
    return rb_trig_cmp(a, b);
}

void meshfeed_request(struct meshfeed *f, const struct rb_world *w, int cx, int s, int cz, int pass,
                      int px, int py, int pz, uint32_t key, uint32_t version)
{
    struct meshfeed_req *q = arr_add(&f->req, 1);
    memset(q, 0, sizeof *q);
    q->cx = cx; q->s = s; q->cz = cz; q->pass = pass;
    q->px = px; q->py = py; q->pz = pz;
    q->no_sky = w->no_sky;
    q->key = key;
    q->version = version;
    const struct chunk *own = NULL;
    for (int i = 0; i < 9; ++i)
    {
        int x = cx - 1 + i / 3, z = cz - 1 + i % 3;
        const struct chunk *rec = NULL;
        if (!window_record(w, x, z, &rec)) q->chunk[i] = MF_OUTSIDE;
        else if (!rec)
        {
            struct mf_chunk *c = find(f, x, z);
            if (c) drop(f, c);
            q->chunk[i] = MF_NULL;
        }
        else q->chunk[i] = hold(f, x, z, rec);
        if (i == 4) own = rec;
    }
    /* the section's flower pots (their plants: ChunkCache.getTileEntity) and
     * double plants (a sunflower head's libm values) */
    q->te_first = (int32_t)f->te.n;
    q->trig_first = (int32_t)f->trig.n;
    const struct chunk_sec *sec = own ? chunk_sec_at(own, s) : NULL;
    q->empty = sec == NULL;
    /* the cells of each id in cell order (each list keeps that order); most
     * sections hold neither */
    for (int k = 0; sec && k < 2; ++k)
    {
        const int id = k ? 175 : 140;
        const uint8_t *end = sec->ids + SEC_CELLS;
        for (const uint8_t *at = memchr(sec->ids, id, SEC_CELLS); at; at = at + 1 < end ? memchr(at + 1, id, (size_t)(end - at - 1)) : NULL)
        {
            int i = (int)(at - sec->ids);
            if (sec->ids_hi && nibble_get(sec->ids_hi, i)) continue;
            /* the band's index is x << 8 | z << 4 | y within the section (CELL_IN_SEC) */
            int x = cx * 16 + (i >> 8), z = cz * 16 + ((i >> 4) & 15), y = s * 16 + (i & 15);
            if (id == 140)
            {
                int block = 0, data = 0;
                if (rb_world_tile_entity(w, x, y, z, &block, &data))
                {
                    struct rb_te *t = arr_add(&f->te, 1);
                    *t = (struct rb_te){x, y, z, block, data};
                }
            }
            else rb_trig_double_plant(x, z, arr_add(&f->trig, 3));
        }
    }
    /* q moved if the arrays grew: find it again */
    q = (struct meshfeed_req *)f->req.p + (f->req.n - 1);
    q->te_n = (int32_t)f->te.n - q->te_first;
    q->trig_n = (int32_t)f->trig.n - q->trig_first;
    if (q->trig_n > 1)
        qsort((struct rb_trig *)f->trig.p + q->trig_first, (size_t)q->trig_n, sizeof(struct rb_trig), trig_order);
}

const struct meshfeed_out *meshfeed_end(struct meshfeed *f)
{
    /* chunks no request read for a while */
    enum { KEEP_FRAMES = 64 };
    for (int i = 0; i < f->nch;)
    {
        if (f->ch[i].last + KEEP_FRAMES < f->frame) drop(f, &f->ch[i]);
        else ++i;
    }
    struct meshfeed_out *o = &f->out;
    memset(o, 0, sizeof *o);
    o->epoch = f->epoch;
    o->seq = f->seq;
    o->chunk_slots = f->chunk_slots;
    o->band_slots = f->band_slots;
    o->nreq = (int)f->req.n;
    o->nte = (int)f->te.n;
    o->ntrig = (int)f->trig.n;
    o->nop = (int)f->op.n;
    o->req = f->req.p;
    o->te = f->te.p;
    o->trig = f->trig.p;
    o->op = f->op.p;
    o->payload = f->payload.p;
    o->npayload = f->payload.n;
    return o;
}

struct rb_trig *meshfeed_static_trig(int *n)
{
    int cap = 65 * 65 * 2 + 6, k = 0;
    struct rb_trig *t = malloc((size_t)cap * sizeof *t);
    if (!t) abort();
    for (int solid = 0; solid < 2; ++solid)
        for (int vx = -32; vx <= 32; ++vx)
            for (int vz = -32; vz <= 32; ++vz)
                k += rb_trig_liquid(vx, vz, solid, &t[k]);
    k += rb_trig_brewing(&t[k]);
    qsort(t, (size_t)k, sizeof *t, trig_order);
    *n = k;
    return t;
}
