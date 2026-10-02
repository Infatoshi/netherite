/* The light engine's capture (lightcap.h): the mirror's bookkeeping, the
 * record stream and the counts. */
#include "lightcap.h"
#include "lightdefer.h"
#include "blocks.h"
#include "env.h"
#include "world.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define LC_HASH 4096

/* the largest call record: every window chunk written, every band stored */
#define LC_CALL_MAX(L) (sizeof(struct lc_call) + LC_WIN_N * (sizeof(struct lc_after) + LC_PAD8((L)->chunk_bytes) + 16 * 4096))
#define LC_PUT_MAX(L) (sizeof(struct lc_put) + LC_PAD8((L)->chunk_bytes) + 16 * 8192)
#define LC_PAD8(n) (((size_t)(n) + 7) & ~(size_t)7)

/* what the mirror holds in a slot: the chunk (by world, position and
 * address) and its write sequence when it was put */
/* What the light engine reads of a chunk besides its bands. Chunk.relightBlock
 * rewrites a column's height and sky between the light calls of one block
 * write without moving the chunk's write sequence, so a slot is also stale
 * when these differ (the sky it wrote goes with a height change). */
struct lc_head {
    int32_t height[256], precip[256], height_min, queued_light_checks;
    uint16_t mask;
    uint8_t no_sky, terrain_populated;
};

static void head_of(struct lc_head *h, const struct chunk *ch)
{
    memset(h, 0, sizeof *h);
    memcpy(h->height, ch->height, sizeof h->height);
    memcpy(h->precip, ch->precip, sizeof h->precip);
    h->height_min = ch->height_min;
    h->mask = ch->mask;
    h->no_sky = ch->no_sky;
    h->queued_light_checks = ch->queued_light_checks;
    h->terrain_populated = ch->terrain_populated;
}

/* precip: whether the precipitation heights count (the deferred mode's
 * device never reads them, and the host fills them in lazily between syncs) */
static int head_same(const struct lc_head *h, const struct chunk *ch, int precip)
{
    return h->mask == ch->mask && h->height_min == ch->height_min && h->no_sky == ch->no_sky &&
           h->queued_light_checks == ch->queued_light_checks && h->terrain_populated == ch->terrain_populated &&
           !memcmp(h->height, ch->height, sizeof h->height) &&
           (!precip || !memcmp(h->precip, ch->precip, sizeof h->precip));
}

struct lc_slot {
    const struct world *w;
    const struct chunk *c;
    int32_t cx, cz;
    uint64_t wseq, used;
    int32_t next;               /* the hash chain, -1 ends it */
    uint8_t live;
    uint16_t present;           /* the bands with storage the mirror has */
    uint64_t band[16];          /* each one's cells' hash (band_hash) */
    struct lc_head head;        /* the fields the light engine reads */
};

struct lc_rec {
    uint64_t index, epoch0, wseq0;
    int acx, acz;
    struct chunk *win[LC_WIN_N];
    uint64_t before[LC_WIN_N];
    int sl[LC_WIN_N];
};

struct lightcap {
    struct lc_sink *sink;
    struct lc_layout L;
    struct lc_stats st;
    size_t at;                  /* bytes of sink->buf filled */
    int failed;
    int32_t head[LC_HASH];
    struct lc_slot slot[LC_SLOTS];
    int32_t nslots;
    /* each world's write sequence at its last call: one that goes back is a
     * new world at a freed one's address, whose slots are dropped */
    struct { const struct world *w; uint64_t wseq; } worlds[8];
    int drivers;
    uint64_t nrec;              /* records so far */
    struct lc_rec rec;          /* the update's record */
    /* the driver running (drv_token 1), its record and its result so far */
    int drv_token;
    struct lc_rec drv_rec;
    struct lc_call drv;
    int maxslots;               /* slots the consumer's mirror holds (<= LC_SLOTS) */
    /* the deferred mode (lightcap_new_defer): the records since the last
     * sync start at win_start and use win_slots slots; the world's epoch and
     * write sequence the device has (ctr_*) */
    int defer;
    uint64_t win_start;
    int win_slots;
    const struct world *ctr_w;
    uint64_t ctr_epoch, ctr_wseq;
    struct { uint64_t syncs, records, back_slots, back_bytes, band_writes, bands_made, bands_owned; } dst;
};

void lightcap_layout(struct lc_layout *L)
{
    memset(L, 0, sizeof *L);
    L->chunk_bytes = sizeof(struct chunk);
    L->chunk_slot = CHUNK_SLOT;
    /* the mirror's bands are the inline form (world.h struct chunk_sec_flat) */
    L->sec_bytes = (sizeof(struct chunk_sec_flat) + 63) & ~(size_t)63;
    L->cx = offsetof(struct chunk, cx);
    L->cz = offsetof(struct chunk, cz);
    L->band = offsetof(struct chunk, band);
    L->height = offsetof(struct chunk, height);
    L->precip = offsetof(struct chunk, precip);
    L->height_min = offsetof(struct chunk, height_min);
    L->mask = offsetof(struct chunk, mask);
    L->no_sky = offsetof(struct chunk, no_sky);
    L->stamp = offsetof(struct chunk, stamp);
    L->edge_stamp = offsetof(struct chunk, edge_stamp);
    L->wseq = offsetof(struct chunk, wseq);
    L->terrain_populated = offsetof(struct chunk, terrain_populated);
    L->queued_light_checks = offsetof(struct chunk, queued_light_checks);
    L->sec_ids_hi = offsetof(struct chunk_sec_flat, h.ids_hi);
    L->sec_shared = offsetof(struct chunk_sec_flat, h.shared);
    L->sec_ids = offsetof(struct chunk_sec_flat, h.ids);
    L->sec_metas = offsetof(struct chunk_sec_flat, metas);
    L->sec_sky = offsetof(struct chunk_sec_flat, sky);
    L->sec_blocklight = offsetof(struct chunk_sec_flat, blocklight);
}

void lightcap_blocks(uint8_t opacity[4096], uint8_t light[4096], uint8_t air[4096])
{
    int am = -1;

    for (int i = 0; i < (int)(sizeof MATERIALS / sizeof MATERIALS[0]); ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, "air") == 0) am = i;
    for (int i = 0; i < 4096; ++i)
    {
        opacity[i] = BLOCKS[i].opacity;
        light[i] = BLOCKS[i].light;
        air[i] = BLOCKS[i].material == am;
    }
}

struct lightcap *lightcap_new(struct lc_sink *sink, int drivers)
{
    struct lightcap *c = mmap(NULL, sizeof *c, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (c == MAP_FAILED) return NULL;
    memset(c, 0, sizeof *c);
    c->sink = sink;
    c->drivers = drivers;
    c->drv_token = -1;
    c->maxslots = LC_SLOTS;
    lightcap_layout(&c->L);
    for (int i = 0; i < LC_HASH; ++i) c->head[i] = -1;
    return c;
}

void lightcap_free(struct lightcap *c)
{
    if (c == NULL) return;
    if (c->sink != NULL && c->sink->free != NULL) c->sink->free(c->sink->ctx);
    munmap(c, sizeof *c);
}

const struct lc_stats *lightcap_stats(const struct lightcap *c)
{
    return &c->st;
}

static uint32_t key_hash(const struct world *w, int cx, int cz)
{
    uint64_t h = (uint64_t)(uintptr_t)w * 0x9e3779b97f4a7c15ULL ^ (uint64_t)(uint32_t)cx * 0xc2b2ae3d27d4eb4fULL ^
                 (uint64_t)(uint32_t)cz * 0x165667b19e3779f9ULL;
    h ^= h >> 31;
    return (uint32_t)h & (LC_HASH - 1);
}

static void unlink_slot(struct lightcap *c, int s)
{
    struct lc_slot *e = &c->slot[s];
    int32_t *p = &c->head[key_hash(e->w, e->cx, e->cz)];

    while (*p != s) p = &c->slot[*p].next;
    *p = e->next;
    e->live = 0;
}

/* the slot holding (w, cx, cz), or a slot made for it (the least recently
 * used one once all are taken); *fresh set when the mirror does not have it */
static int slot_for(struct lightcap *c, const struct world *w, int cx, int cz, int *fresh)
{
    uint32_t h = key_hash(w, cx, cz);

    for (int32_t s = c->head[h]; s >= 0; s = c->slot[s].next)
        if (c->slot[s].w == w && c->slot[s].cx == cx && c->slot[s].cz == cz)
        {
            *fresh = 0;
            return s;
        }

    int s;

    for (s = 0; s < c->nslots && c->slot[s].live; ++s)
        ;
    if (s == c->nslots && c->nslots < c->maxslots) c->nslots++;
    else if (s == c->nslots)
    {
        s = 0;
        for (int i = 1; i < c->maxslots; ++i)
            if (c->slot[i].used < c->slot[s].used) s = i;
        unlink_slot(c, s);
    }
    memset(&c->slot[s], 0, sizeof c->slot[s]);
    c->slot[s] = (struct lc_slot){.w = w, .cx = cx, .cz = cz, .next = c->head[h], .live = 1};
    c->head[h] = s;
    *fresh = 1;
    return s;
}

/* room for n more bytes, handing the buffer over when it is short */
static int room(struct lightcap *c, size_t n)
{
    if (c->sink == NULL) return 0;
    if (c->failed) return -1;
    if (c->at + n <= c->sink->cap) return 0;
    c->st.flushes++;
    if ((c->sink->buf = c->sink->submit(c->sink->ctx, c->at)) == NULL)
    {
        c->failed = 1;
        return -1;
    }
    c->at = 0;
    return c->at + n <= c->sink->cap ? 0 : -1;
}

static uint8_t *put_bytes(struct lightcap *c, const void *p, size_t n)
{
    uint8_t *d = c->sink->buf + c->at;

    if (p != NULL) memcpy(d, p, n);
    c->at += LC_PAD8(n);
    return d;
}

static uint16_t band_mask(const struct chunk *ch, uint16_t *shared)
{
    uint16_t m = 0, sh = 0;

    for (int k = 0; k < 16; ++k)
    {
        const struct chunk_sec *s = chunk_sec_at(ch, k);

        if (s == NULL) continue;
        if (s->ids_hi != NULL)
        {
            /* no 1.7.10 block id is above 255: the mirror does not carry the
             * high nibbles */
            fprintf(stderr, "lightcap: chunk (%d,%d) band %d holds an id above 255\n", ch->cx, ch->cz, k);
            abort();
        }
        m |= (uint16_t)(1u << k);
        if (s->shared) sh |= (uint16_t)(1u << k);
    }
    if (shared) *shared = sh;
    return m;
}

/* A band's ids, sky and block light hashed (four lanes of xxhash64's
 * round), what the mirror compares to send only the bands that changed. */
static uint64_t band_hash(const struct chunk_sec *sec)
{
    static const uint64_t P1 = 0x9e3779b185ebca87ULL, P2 = 0xc2b2ae3d27d4eb4fULL;
    uint64_t v[4] = {P1, P2, P1 ^ P2, P1 + P2};
    const uint8_t *part[3] = {sec->ids, chunk_sec_sky(sec), chunk_sec_blocklight(sec)};
    const size_t len[3] = {4096, 2048, 2048};

    for (int p = 0; p < 3; ++p)
        for (size_t i = 0; i < len[p]; i += 32)
            for (int l = 0; l < 4; ++l)
            {
                uint64_t w;

                memcpy(&w, part[p] + i + 8 * l, 8);
                v[l] += w * P2;
                v[l] = (v[l] << 31 | v[l] >> 33) * P1;
            }

    uint64_t h = (v[0] << 1 | v[0] >> 63) + (v[1] << 7 | v[1] >> 57) + (v[2] << 12 | v[2] >> 52) + (v[3] << 18 | v[3] >> 46);

    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    return h | 1;   /* 0 is a band without storage */
}

/* the chunk into slot s: its struct and every band whose storage or cells
 * differ from what the mirror has */
static void emit_put(struct lightcap *c, int s, const struct chunk *ch)
{
    struct lc_slot *e = &c->slot[s];
    uint16_t shared, present = band_mask(ch, &shared), sent = 0;
    uint64_t h[16];

    for (int k = 0; k < 16; ++k)
    {
        h[k] = present >> k & 1 ? band_hash(chunk_sec_at(ch, k)) : 0;
        if (h[k] != 0 && (!(e->present >> k & 1) || e->band[k] != h[k])) sent |= (uint16_t)(1u << k);
    }

    size_t n = sizeof(struct lc_put) + LC_PAD8(c->L.chunk_bytes) + (size_t)__builtin_popcount(sent) * 8192;

    c->st.puts++;
    c->st.put_bands += (uint64_t)__builtin_popcount(sent);
    c->st.put_bytes += n;
    e->present = present;
    memcpy(e->band, h, sizeof h);
    head_of(&e->head, ch);
    if (c->sink == NULL || room(c, n)) return;

    struct lc_put *p = (struct lc_put *)put_bytes(c, NULL, sizeof *p);

    *p = (struct lc_put){LC_PUT, (uint32_t)s, present, shared, sent, 0, (uint32_t)n, 0};
    put_bytes(c, ch, c->L.chunk_bytes);
    for (int k = 0; k < 16; ++k)
    {
        if (!(sent >> k & 1)) continue;
        const struct chunk_sec *sec = chunk_sec_at(ch, k);
        if (sec == NULL) continue;   /* never: sent is present's */
        put_bytes(c, sec->ids, 4096);
        put_bytes(c, chunk_sec_sky(sec), 2048);
        put_bytes(c, chunk_sec_blocklight(sec), 2048);
    }
}

/* the mirror's hashes of a chunk the call wrote, as C left it (the consumer
 * takes C's result) */
static void rehash(struct lightcap *c, int s, const struct chunk *ch)
{
    struct lc_slot *e = &c->slot[s];

    e->present = band_mask(ch, NULL);
    for (int k = 0; k < 16; ++k) e->band[k] = e->present >> k & 1 ? band_hash(chunk_sec_at(ch, k)) : 0;
    e->wseq = ch->wseq;
    head_of(&e->head, ch);
}

/* rehash for a slot a deferred run wrote back: only the bands whose light
 * the write-back changed (CHANGED) or made are hashed again. The others hold
 * what the mirror held before the run (a light call writes no ids, and the
 * host changes nothing of a slot between its put and the sync), so their
 * hashes stand. */
static void rehash_changed(struct lightcap *c, int s, const struct chunk *ch, uint16_t changed)
{
    struct lc_slot *e = &c->slot[s];
    uint16_t was = e->present;

    e->present = band_mask(ch, NULL);
    for (int k = 0; k < 16; ++k)
        if (!(e->present >> k & 1)) e->band[k] = 0;
        else if ((changed >> k & 1) || !(was >> k & 1)) e->band[k] = band_hash(chunk_sec_at(ch, k));
    e->wseq = ch->wseq;
    head_of(&e->head, ch);
}

static void world_seen(struct lightcap *c, const struct world *w)
{
    int i;

    for (i = 0; i < 8 && c->worlds[i].w != NULL && c->worlds[i].w != w; ++i)
        ;
    if (i == 8)
    {
        fprintf(stderr, "lightcap: more than 8 worlds\n");
        abort();
    }
    if (c->worlds[i].w == w && w->wseq < c->worlds[i].wseq)
        for (int s = 0; s < c->nslots; ++s)
            if (c->slot[s].live && c->slot[s].w == w) unlink_slot(c, s);
    c->worlds[i].w = w;
    c->worlds[i].wseq = w->wseq;
}

/* An open record: the 5x5 chunks around (acx, acz) as they were before
 * and their slots (-1 not loaded, -2 loaded and not sent). reach limits
 * what is sent: a refused call sends nothing, an update the chunks within
 * 18 blocks of its cell, a driver all 25. */
static void rec_open(struct lightcap *c, struct world *w, struct lc_rec *r, int acx, int acz, int refused, int rx0,
                     int rx1, int rz0, int rz1)
{
    const struct lc_layout *L = &c->L;

    r->index = c->nrec++;
    r->acx = acx;
    r->acz = acz;
    if (c->sink != NULL && room(c, LC_PUT_MAX(L) * LC_WIN_N + LC_CALL_MAX(L))) c->failed = 1;
    world_seen(c, w);

    for (int k = 0; k < LC_WIN_N; ++k)
    {
        struct chunk *ch = world_chunk(w, acx + k % LC_WIN - 2, acz + k / LC_WIN - 2);

        r->win[k] = ch;
        r->sl[k] = -1;
        if (ch == NULL) continue;
        r->before[k] = ch->wseq;
        if (refused || ch->cx < rx0 || ch->cx > rx1 || ch->cz < rz0 || ch->cz > rz1)
        {
            r->sl[k] = -2;
            continue;
        }

        int fresh, s = slot_for(c, w, ch->cx, ch->cz, &fresh);
        struct lc_slot *e = &c->slot[s];

        if (fresh || e->c != ch || e->wseq != ch->wseq || !head_same(&e->head, ch, !c->defer))
        {
            emit_put(c, s, ch);
            e->c = ch;
            e->wseq = ch->wseq;
        }
        if (e->used <= c->win_start) c->win_slots++;
        e->used = r->index + 1;
        r->sl[k] = s;
    }
    r->epoch0 = w->epoch;
    r->wseq0 = w->wseq;
}

/* the record closed: what C wrote, into the stream after f (the fields the
 * caller filled) */
static void rec_close(struct lightcap *c, struct world *w, struct lc_rec *r, struct lc_call *f)
{
    const struct lc_layout *L = &c->L;
    uint32_t written = 0;

    for (int k = 0; k < LC_WIN_N; ++k)
    {
        struct chunk *ch = r->win[k];

        if (world_chunk(w, r->acx + k % LC_WIN - 2, r->acz + k / LC_WIN - 2) != ch)
        {
            fprintf(stderr, "lightcap: record %llu changed the loaded chunks\n", (unsigned long long)r->index);
            abort();
        }
        if (ch != NULL && ch->wseq != r->before[k]) written |= 1u << k;
        if ((written >> k & 1) && r->sl[k] < 0)
        {
            fprintf(stderr, "lightcap: record %llu wrote a chunk out of its reach\n", (unsigned long long)r->index);
            abort();
        }
    }
    c->st.chunks_written += (uint64_t)__builtin_popcount(written);

    size_t n = sizeof(struct lc_call);

    for (int k = 0; k < LC_WIN_N; ++k)
        if (written >> k & 1)
            n += sizeof(struct lc_after) + LC_PAD8(L->chunk_bytes) + (size_t)__builtin_popcount(band_mask(r->win[k], NULL)) * 4096;
    c->st.call_bytes += n;

    if (c->sink != NULL && !c->failed && room(c, n) == 0)
    {
        struct lc_call *o = (struct lc_call *)put_bytes(c, NULL, sizeof *o);

        *o = *f;
        o->kind = LC_CALL;
        o->bytes = (uint32_t)n;
        o->index = r->index;
        o->cx = r->acx;
        o->cz = r->acz;
        o->no_generate = w->no_generate;
        o->remote = w->is_remote;
        o->epoch0 = r->epoch0;
        o->wseq0 = r->wseq0;
        o->epoch1 = w->epoch;
        o->wseq1 = w->wseq;
        o->written = written;
        for (int k = 0; k < LC_WIN_N; ++k)
            o->slot[k] = r->sl[k] == -2 ? LC_STALE : r->sl[k] < 0 ? 0 : (uint32_t)r->sl[k] + 1;

        for (int k = 0; k < LC_WIN_N; ++k)
        {
            if (!(written >> k & 1)) continue;

            uint16_t present = band_mask(r->win[k], NULL);
            struct lc_after *a = (struct lc_after *)put_bytes(c, NULL, sizeof *a);

            *a = (struct lc_after){(uint32_t)r->sl[k], present, 0};
            put_bytes(c, r->win[k], L->chunk_bytes);
            for (int b = 0; b < 16; ++b)
            {
                if (!(present >> b & 1)) continue;
                const struct chunk_sec *sec = chunk_sec_at(r->win[k], b);
                put_bytes(c, chunk_sec_sky(sec), 2048);
                put_bytes(c, chunk_sec_blocklight(sec), 2048);
            }
        }
    }

    world_seen(c, w);

    /* the mirror now holds what C left: the consumer compares and takes it */
    for (int k = 0; k < LC_WIN_N; ++k)
        if (written >> k & 1) rehash(c, r->sl[k], r->win[k]);
}

int lightcap_update(struct lightcap *c, struct world *w, int type, int x, int y, int z)
{
    int in_driver = c->drv_token >= 0;
    uint64_t epoch0 = w->epoch;

    ++c->st.calls;
    if (!in_driver)
        rec_open(c, w, &c->rec, x >> 4, z >> 4, !world_do_chunks_near_chunk_exist(w, x, y, z, 17), (x - 18) >> 4,
                 (x + 18) >> 4, (z - 18) >> 4, (z + 18) >> 4);

    nw_env->light.tail = 0;
    nw_env->light.overflow = 0;

    int ret = world_light_update_raw(w, type, x, y, z);

    c->st.sky += type == LIGHT_SKY;
    c->st.remote += w->is_remote != 0;
    c->st.guard += ret == 0;
    c->st.writes += w->epoch - epoch0;
    c->st.wrote += w->epoch != epoch0;
    c->st.overflow += nw_env->light.overflow;
    if (nw_env->light.tail > c->st.tail_max) c->st.tail_max = nw_env->light.tail;

    if (in_driver)
    {
        /* a driver's call: counted into the driver's record */
        c->st.driver_calls++;
        c->drv.calls++;
        c->drv.tail_sum += (uint64_t)nw_env->light.tail;
        c->drv.overflow |= nw_env->light.overflow;
        return ret;
    }

    struct lc_call f;

    memset(&f, 0, sizeof f);
    f.what = LC_O_UPDATE;
    f.type = type;
    f.x = x;
    f.y = y;
    f.z = z;
    f.ret = ret;
    f.overflow = nw_env->light.overflow;
    f.calls = 1;
    f.tail_sum = (uint64_t)nw_env->light.tail;
    rec_close(c, w, &c->rec, &f);
    return ret;
}

int lightcap_driver_begin(struct lightcap *c, struct world *w, struct chunk *ch, int what, int side)
{
    if (!c->drivers || c->drv_token >= 0) return -1;
    if (what == LC_O_RELIGHT && !w->no_generate)
    {
        /* its block reads one column out load a missing neighbour */
        static const int DX[4] = {-1, 1, 0, 0}, DZ[4] = {0, 0, -1, 1};

        for (int i = 0; i < 4; ++i)
            if (world_chunk(w, ch->cx + DX[i], ch->cz + DZ[i]) == NULL)
            {
                c->st.relight_host++;
                return -1;
            }
    }
    c->st.drivers[what]++;
    rec_open(c, w, &c->drv_rec, ch->cx, ch->cz, 0, ch->cx - 2, ch->cx + 2, ch->cz - 2, ch->cz + 2);
    memset(&c->drv, 0, sizeof c->drv);
    c->drv.what = what;
    c->drv.side = side;
    c->drv.dim = w->dim;
    c->drv.cursor0 = ch->queued_light_checks;
    c->drv_token = 1;
    return 1;
}

void lightcap_driver_end(struct lightcap *c, struct world *w, struct chunk *ch, int token, int ret)
{
    if (token < 0) return;
    c->drv_token = -1;
    c->drv.ret = ret;
    c->drv.cursor1 = ch->queued_light_checks;
    rec_close(c, w, &c->drv_rec, &c->drv);
}

/* ------------------------------------------------------ the deferred mode */

struct lightcap *lightcap_new_defer(struct lc_sink *sink, int nslots)
{
    if (sink == NULL || sink->sync == NULL || nslots < 64 || nslots > LC_SLOTS) return NULL;

    struct lightcap *c = lightcap_new(sink, 1);

    if (c == NULL) return NULL;
    c->defer = 1;
    c->maxslots = nslots;
    return c;
}

void lightcap_defer_forget(struct lightcap *c, const struct world *w)
{
    for (int s = 0; s < c->nslots; ++s)
        if (c->slot[s].live && (w == NULL || c->slot[s].w == w)) unlink_slot(c, s);
    for (int i = 0; i < 8; ++i)
        if (w == NULL || c->worlds[i].w == w) c->worlds[i].w = NULL, c->worlds[i].wseq = 0;
    if (w == NULL || c->ctr_w == w) c->ctr_w = NULL;
}

/* one operation's record: the chunks it reads put first (rec_open), then the
 * call with no after-state */
static void defer_emit(struct lightcap *c, struct world *w, const struct ld_op *o)
{
    struct lc_rec *r = &c->rec;
    struct lc_call f;

    memset(&f, 0, sizeof f);
    f.what = o->what;
    f.dim = w->dim;
    if (o->what == LC_O_UPDATE)
    {
        int x = o->x, y = o->y, z = o->z;

        rec_open(c, w, r, x >> 4, z >> 4, !world_do_chunks_near_chunk_exist(w, x, y, z, 17), (x - 18) >> 4,
                 (x + 18) >> 4, (z - 18) >> 4, (z + 18) >> 4);
        f.type = o->type;
        f.x = x;
        f.y = y;
        f.z = z;
        c->st.calls++;
        c->st.sky += o->type == LIGHT_SKY;
        c->st.remote += w->is_remote != 0;
    }
    else
    {
        rec_open(c, w, r, o->x, o->z, 0, o->x - 2, o->x + 2, o->z - 2, o->z + 2);
        c->st.drivers[LC_O_RELIGHT]++;
    }
    if (c->sink == NULL || c->failed || room(c, sizeof f))
    {
        c->failed = 1;
        return;
    }

    struct lc_call *p = (struct lc_call *)put_bytes(c, NULL, sizeof *p);

    *p = f;
    p->kind = LC_CALL;
    p->bytes = (uint32_t)sizeof *p;
    p->index = r->index;
    p->cx = r->acx;
    p->cz = r->acz;
    p->no_generate = w->no_generate;
    p->remote = w->is_remote;
    p->epoch0 = p->epoch1 = w->epoch;
    p->wseq0 = p->wseq1 = w->wseq;
    p->flags = LC_F_AUTH;
    /* the device has the world's counters unless the host moved them (a
     * write, which syncs first) or this is another world */
    if (c->ctr_w != w || c->ctr_epoch != w->epoch || c->ctr_wseq != w->wseq)
    {
        p->flags |= LC_F_CTR;
        c->ctr_w = w;
        c->ctr_epoch = w->epoch;
        c->ctr_wseq = w->wseq;
    }
    for (int k = 0; k < LC_WIN_N; ++k)
        p->slot[k] = r->sl[k] == -2 ? LC_STALE : r->sl[k] < 0 ? 0 : (uint32_t)r->sl[k] + 1;
    c->st.call_bytes += sizeof *p;
    c->dst.records++;
}

/* the device's slots written into the host's chunks; 0 on success */
static int defer_apply(struct lightcap *c, struct world *w, const uint8_t *back)
{
    const struct lc_layout *L = &c->L;
    struct lc_back h;

    memcpy(&h, back, sizeof h);
    if (h.status != 0)
    {
        fprintf(stderr, "lightcap: the device could not run a deferred record (status %d)\n", h.status);
        return -1;
    }

    const uint8_t *p = back + sizeof h, *end = back + h.bytes;

    c->dst.back_slots += h.slots;
    c->dst.back_bytes += h.bytes;
    for (uint32_t i = 0; i < h.slots; ++i)
    {
        struct lc_after a;

        if (p + sizeof a > end) return -1;
        memcpy(&a, p, sizeof a);
        p += sizeof a;

        const uint8_t *dc = p;
        /* the slot's high bit: the device regenerated its sky map */
        int regen = (int)(a.slot >> 31);

        a.slot &= 0x7fffffffu;

        struct lc_slot *e = a.slot < (uint32_t)c->nslots ? &c->slot[a.slot] : NULL;
        struct chunk *ch = e != NULL && e->live && e->w == w ? (struct chunk *)e->c : NULL;

        p += LC_PAD8(L->chunk_bytes);
        if (ch == NULL || world_chunk(w, ch->cx, ch->cz) != ch)
        {
            fprintf(stderr, "lightcap: the device wrote slot %u, which holds no chunk of the world\n", a.slot);
            return -1;
        }

        /* the fields the light engine writes; heights and precipitation
         * only where it regenerated the sky map (the host fills the
         * precipitation heights in lazily between syncs) */
        uint16_t shared = a.shared, changed = 0;

        memcpy(&ch->mask, dc + L->mask, sizeof ch->mask);
        memcpy(&ch->stamp, dc + L->stamp, sizeof ch->stamp);
        memcpy(ch->edge_stamp, dc + L->edge_stamp, sizeof ch->edge_stamp);
        memcpy(&ch->wseq, dc + L->wseq, sizeof ch->wseq);
        memcpy(&ch->queued_light_checks, dc + L->queued_light_checks, sizeof ch->queued_light_checks);
        if (regen)
        {
            memcpy(ch->height, dc + L->height, sizeof ch->height);
            memcpy(ch->precip, dc + L->precip, sizeof ch->precip);
            memcpy(&ch->height_min, dc + L->height_min, sizeof ch->height_min);
        }
        for (int s = 0; s < 16; ++s)
        {
            struct chunk_sec *sec = (struct chunk_sec *)chunk_sec_at(ch, s);

            if (!(a.present >> s & 1))
            {
                if (sec != NULL)
                {
                    fprintf(stderr, "lightcap: chunk (%d,%d) band %d lost its storage on the device\n", ch->cx, ch->cz, s);
                    return -1;
                }
                continue;
            }

            const uint8_t *sky = p, *bl = p + 2048;

            p += 4096;
            if (sec == NULL)
            {
                sec = chunk_sec_make(ch, s);
                c->dst.bands_made++;
                changed |= (uint16_t)(1u << s);
            }
            else if (sec->shared != 0 && !(shared >> s & 1))
            {
                sec = chunk_sec_own(ch, s);
                c->dst.bands_owned++;
            }
            if (memcmp(chunk_sec_sky(sec), sky, 2048) || memcmp(chunk_sec_blocklight(sec), bl, 2048))
            {
                if (sec->shared != 0)
                {
                    fprintf(stderr, "lightcap: chunk (%d,%d) band %d is shared and its light changed\n", ch->cx, ch->cz, s);
                    return -1;
                }
                chunk_sec_nib_store(sec, NIB_SKY, sky);
                chunk_sec_nib_store(sec, NIB_BLOCK, bl);
                c->dst.band_writes++;
                changed |= (uint16_t)(1u << s);
            }
        }
        if (p > end) return -1;
        rehash_changed(c, (int)a.slot, ch, changed);
    }
    w->epoch = h.epoch;
    w->wseq = h.wseq;
    c->ctr_w = w;
    c->ctr_epoch = h.epoch;
    c->ctr_wseq = h.wseq;
    return 0;
}

/* the stream so far run, and what it wrote applied */
static int defer_sync(struct lightcap *c, struct world *w)
{
    const uint8_t *back = NULL;

    c->dst.syncs++;
    if (c->failed) return -1;

    int rc = c->sink->sync(c->sink->ctx, c->at, &back);

    c->at = 0;
    c->win_start = c->nrec;
    c->win_slots = 0;
    if (rc != 0 || back == NULL || defer_apply(c, w, back))
    {
        c->failed = 1;
        return -1;
    }
    return 0;
}

int lightcap_defer_run(struct lightcap *c, struct world *w, const struct ld_op *ops, size_t n)
{
    if (!c->defer || c->failed) return -1;
    for (size_t i = 0; i < n; ++i)
    {
        /* the mirror keeps every slot the records since the last sync
         * wrote: sync before they could take half of it */
        if (c->win_slots + LC_WIN_N > c->maxslots / 2 && defer_sync(c, w)) return -1;
        defer_emit(c, w, &ops[i]);
        if (c->failed) return -1;
    }
    return defer_sync(c, w);
}

int lightcap_finish(struct lightcap *c, FILE *out)
{
    const struct lc_stats *s = &c->st;
    int rc = 0;

    if (c->sink != NULL)
    {
        if (!c->failed && c->at > 0)
        {
            c->st.flushes++;
            if ((c->sink->buf = c->sink->submit(c->sink->ctx, c->at)) == NULL) c->failed = 1;
            c->at = 0;
        }
        rc = c->sink->finish(c->sink->ctx, out);
        if (c->failed && rc == 0) rc = -1;
    }
    fprintf(out,
            "light capture: %llu calls (%llu sky, %llu block; %llu on the client world), %llu refused by the 17-block box, %llu wrote light "
            "(%llu writes, %llu chunks), queue max %d, %llu calls hit the 32,768 cap; stream %llu puts (%llu bands) %.1f MB, "
            "calls %.1f MB\n",
            (unsigned long long)s->calls, (unsigned long long)s->sky, (unsigned long long)(s->calls - s->sky),
            (unsigned long long)s->remote, (unsigned long long)s->guard, (unsigned long long)s->wrote, (unsigned long long)s->writes,
            (unsigned long long)s->chunks_written, s->tail_max, (unsigned long long)s->overflow,
            (unsigned long long)s->puts, (unsigned long long)s->put_bands, s->put_bytes / 1e6, s->call_bytes / 1e6);
    if (c->defer)
        fprintf(out,
                "light capture (deferred, the device's own light): %llu records, %llu syncs; %llu slots came back "
                "(%.1f MB), %llu bands written, %llu made, %llu made the chunk's own\n",
                (unsigned long long)c->dst.records, (unsigned long long)c->dst.syncs, (unsigned long long)c->dst.back_slots,
                c->dst.back_bytes / 1e6, (unsigned long long)c->dst.band_writes, (unsigned long long)c->dst.bands_made,
                (unsigned long long)c->dst.bands_owned);
    else if (c->drivers)
        fprintf(out,
                "light capture drivers: %llu func_150809_p column passes, %llu func_150801_a edges, %llu relight checks "
                "(%llu of the calls above ran inside them), %llu relight checks left to their calls (a facing chunk "
                "not loaded)\n",
                (unsigned long long)s->drivers[LC_O_COLUMNS], (unsigned long long)s->drivers[LC_O_SIDE],
                (unsigned long long)s->drivers[LC_O_RELIGHT], (unsigned long long)s->driver_calls,
                (unsigned long long)s->relight_host);
    return rc;
}

/* ---- the stored capture (lightcap.h lc_sink_file_create) ---- */

struct lc_file {
    FILE *f;
    uint8_t *buf;
    uint64_t bytes, buffers;
    int bad;
    struct lc_sink sink;
};

static uint8_t *lcf_submit(void *ctx, size_t n)
{
    struct lc_file *k = ctx;
    uint64_t n64 = n;

    if (k->bad || fwrite(&n64, sizeof n64, 1, k->f) != 1 || fwrite(k->buf, 1, n, k->f) != n)
    {
        k->bad = 1;
        return NULL;
    }
    k->bytes += n;
    k->buffers++;
    return k->buf;
}

static int lcf_finish(void *ctx, FILE *out)
{
    struct lc_file *k = ctx;
    uint64_t end = 0;

    if (!k->bad && (fwrite(&end, sizeof end, 1, k->f) != 1 || fflush(k->f) != 0)) k->bad = 1;
    if (k->bad)
    {
        fprintf(out, "light capture: the stored capture could not be written\n");
        return -1;
    }
    fprintf(out, "light capture stored: %llu bytes in %llu buffers\n", (unsigned long long)k->bytes,
            (unsigned long long)k->buffers);
    return 0;
}

static void lcf_free(void *ctx)
{
    struct lc_file *k = ctx;

    free(k->buf);
    free(k);
}

struct lc_sink *lc_sink_file_create(FILE *f, size_t cap, int drivers)
{
    struct lc_file *k = calloc(1, sizeof *k);
    struct lc_file_head *h = calloc(1, sizeof *h);

    if (k == NULL || h == NULL || (k->buf = malloc(cap)) == NULL)
    {
        free(h);
        if (k) free(k->buf);
        free(k);
        return NULL;
    }
    h->magic = LC_FILE_MAGIC;
    h->version = LC_FILE_VERSION;
    h->layout_bytes = sizeof h->L;
    h->drivers = (uint32_t)drivers;
    h->cap = cap;
    lightcap_layout(&h->L);
    lightcap_blocks(h->opacity, h->light, h->air);
    k->f = f;
    k->bad = fwrite(h, sizeof *h, 1, f) != 1;
    free(h);
    k->sink = (struct lc_sink){k, k->buf, cap, lcf_submit, lcf_finish, lcf_free, NULL};
    return &k->sink;
}
