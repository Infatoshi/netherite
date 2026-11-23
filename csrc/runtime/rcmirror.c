/* The device cast's copy of each env's client world, its host side (rcmirror.h). */
#include "rcmirror.h"

#include <string.h>

#include "../engine/session.h"
#include "../engine/world.h"

int rcmirror_side(int range) { return 2 * (range + 1) / 16 + 2; }

static inline int ring_index(int R, int cx, int cz)
{
    int a = cx % R, b = cz % R;
    return (a < 0 ? a + R : a) * R + (b < 0 ? b + R : b);
}

/* bytes to a 64-bit hash, four words in flight (n a multiple of 32) */
static uint64_t hash_bytes(uint64_t seed, const unsigned char *p, size_t n)
{
    const uint64_t K = 0x9e3779b97f4a7c15ull;
    uint64_t h0 = seed, h1 = seed ^ 0x6a09e667f3bcc909ull, h2 = seed ^ 0xbb67ae8584caa73bull, h3 = seed ^ 0x3c6ef372fe94f82bull;
    for (size_t i = 0; i < n; i += 32)
    {
        uint64_t w[4];
        memcpy(w, p + i, 32);
        h0 = (h0 ^ w[0]) * K, h0 ^= h0 >> 29;
        h1 = (h1 ^ w[1]) * K, h1 ^= h1 >> 29;
        h2 = (h2 ^ w[2]) * K, h2 ^= h2 >> 29;
        h3 = (h3 ^ w[3]) * K, h3 ^= h3 >> 29;
    }
    uint64_t h = h0 ^ (h1 * 3) ^ (h2 * 5) ^ (h3 * 7);
    h = (h ^ (h >> 31)) * K;
    return h ^ (h >> 32);
}

static uint64_t sec_hash(const struct chunk_sec *s)
{
    uint64_t h = hash_bytes(1, s->ids, SEC_CELLS);
    h = hash_bytes(h, chunk_sec_metas(s), SEC_NIB_BYTES);
    h = hash_bytes(h, chunk_sec_sky(s), SEC_NIB_BYTES);
    h = hash_bytes(h, chunk_sec_blocklight(s), SEC_NIB_BYTES);
    return h ? h : 1;
}

/* a record's bytes in env id's stage (copied in first when it is full); NULL when the flush failed */
static unsigned char *rec_new(struct rcmirror *m, int id, int kind, int col, int sec)
{
    struct rcm_env *E = &m->env[id];
    if (E->used + RCD_REC_BYTES > E->cap)
    {
        if (E->nrec == 0 || m->flush == NULL || m->flush(m->flush_ctx, id)) return NULL;
        ++E->flushes;
        E->used = 0, E->nrec = 0;
    }
    struct rcd_rec r = {kind, id, col, sec};
    unsigned char *p = E->stage + E->used;
    memcpy(p, &r, sizeof r);
    E->used += RCD_REC_BYTES;
    E->nrec++, E->recs++;
    return p + sizeof r;
}

int rcmirror_sync(struct rcmirror *m, int id, uint32_t epoch, struct session *ss, struct rcd_head *h)
{
    _Static_assert(sizeof(struct rcd_sec) == SEC_CELLS + 3 * SEC_NIB_BYTES, "a section's cells and nibbles");
    _Static_assert(RCD_REC_BYTES % 16 == 0 && sizeof(struct rcd_col) % 16 == 0, "records copied 16 bytes at a time");
    struct rcm_env *E = &m->env[id];
    ++E->syncs;
    if (!h->ok) return -1;
    struct world *w = ss->client_world;
    const int R = m->R;
    if (E->epoch != epoch || E->world != (const void *)w)
    {
        for (int c = 0; c < R * R; ++c) E->col[c].state = 0;
        E->epoch = epoch, E->world = w;
    }
    if (h->ncx > R || h->ncz > R) return h->ok = 0, -1;
    int top = 0;
    for (int a = 0; a < h->ncx; ++a)
        for (int b = 0; b < h->ncz; ++b)
        {
            const int cx = h->cx0 + a, cz = h->cz0 + b;
            struct chunk *ch = world_chunk(w, cx, cz);
            if (ch != NULL && ch->mask != 0)
            {
                int t = 16 * (32 - __builtin_clz((unsigned)ch->mask));
                if (t > top) top = t;
            }
            const int ci = ring_index(R, cx, cz);
            struct rcm_col *C = &E->col[ci];
            const int held = C->state && C->cx == cx && C->cz == cz;
            if (held && C->chunk == ch && C->exists == (ch != NULL) && (ch == NULL || C->wseq == ch->wseq)) continue;
            ++E->dirty;
            /* the column's bytes, and each section's that changed (all of them in a column the ring did not hold) */
            C->state = 0;
            struct rcd_col col;
            memset(&col, 0, sizeof col);
            col.cx = cx, col.cz = cz, col.exists = ch != NULL;
            if (ch != NULL)
            {
                col.mask = ch->mask;
                for (int s = 0; s < 16; ++s)
                {
                    const struct chunk_sec *sec = (ch->mask >> s & 1) ? chunk_sec_at(ch, s) : NULL;
                    if (sec == NULL)
                    {
                        C->sec_hash[s] = 0;
                        continue;
                    }
                    if (sec->ids_hi != NULL) return h->ok = 0, -1;
                    col.present |= (uint16_t)(1u << s);
                    uint64_t hs = sec_hash(sec);
                    if (!held || hs != C->sec_hash[s])
                    {
                        unsigned char *p = rec_new(m, id, RCD_REC_SEC, ci, s);
                        if (p == NULL) return h->ok = 0, -1;
                        memcpy(p, sec->ids, SEC_CELLS);
                        memcpy(p + SEC_CELLS, chunk_sec_metas(sec), SEC_NIB_BYTES);
                        memcpy(p + SEC_CELLS + SEC_NIB_BYTES, chunk_sec_sky(sec), SEC_NIB_BYTES);
                        memcpy(p + SEC_CELLS + 2 * SEC_NIB_BYTES, chunk_sec_blocklight(sec), SEC_NIB_BYTES);
                    }
                    C->sec_hash[s] = hs;
                }
                memcpy(col.height, ch->height, sizeof col.height);
                memcpy(col.biome, ch->biome, sizeof col.biome);
            }
            else
                memset(C->sec_hash, 0, sizeof C->sec_hash);
            uint64_t hc = hash_bytes(2, (const unsigned char *)&col, sizeof col);
            if (!held || hc != C->col_hash)
            {
                unsigned char *p = rec_new(m, id, RCD_REC_COL, ci, 0);
                if (p == NULL) return h->ok = 0, -1;
                memcpy(p, &col, sizeof col);
            }
            C->col_hash = hc;
            C->cx = cx, C->cz = cz, C->chunk = ch, C->exists = ch != NULL, C->wseq = ch ? ch->wseq : 0;
            C->state = 1;
        }
    h->top = top;
    return 0;
}
