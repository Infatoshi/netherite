/* A digest of what the light engine reads and writes of a client world
 * (lightdefer.h's deferral is exact when a replay that defers gives the same
 * digest after every row as one that does not): the world's epoch and write
 * sequence, and for each loaded chunk in load order its position, band mask,
 * height map and its minimum, precipitation heights, stamps, write sequence,
 * relight cursor, gap flags, band storage and every band's sky and block
 * light. Header-only, for the test harnesses. */
#ifndef NETHERITE_TESTS_CWDIGEST_H
#define NETHERITE_TESTS_CWDIGEST_H

#include <stdint.h>
#include <string.h>

#include "../engine/world.h"

static inline uint64_t cwd_mix(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    size_t i = 0;

    for (; i + 8 <= n; i += 8)
    {
        uint64_t w;

        memcpy(&w, b + i, 8);
        h = (h ^ w) * 0x9e3779b97f4a7c15ULL;
        h ^= h >> 29;
    }
    for (; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ULL;
    return h;
}

static inline uint64_t cw_digest(const struct world *w)
{
    uint64_t h = 0xcbf29ce484222325ULL;

    if (w == NULL) return 0;
    h = cwd_mix(h, &w->epoch, sizeof w->epoch);
    h = cwd_mix(h, &w->wseq, sizeof w->wseq);
    for (size_t i = 0; i < w->lon; ++i)
    {
        int64_t key = w->load_order[i];
        const struct chunk *c = world_chunk((struct world *)w, (int)(int32_t)(key & 0xffffffff), (int)(key >> 32));

        if (c == NULL) continue;
        h = cwd_mix(h, &c->cx, sizeof c->cx);
        h = cwd_mix(h, &c->cz, sizeof c->cz);
        h = cwd_mix(h, &c->mask, sizeof c->mask);
        h = cwd_mix(h, c->height, sizeof c->height);
        h = cwd_mix(h, &c->height_min, sizeof c->height_min);
        h = cwd_mix(h, c->precip, sizeof c->precip);
        h = cwd_mix(h, &c->stamp, sizeof c->stamp);
        h = cwd_mix(h, c->edge_stamp, sizeof c->edge_stamp);
        h = cwd_mix(h, &c->wseq, sizeof c->wseq);
        h = cwd_mix(h, &c->queued_light_checks, sizeof c->queued_light_checks);
        h = cwd_mix(h, &c->gap_lighting_updated, sizeof c->gap_lighting_updated);
        h = cwd_mix(h, c->update_skylight_columns, sizeof c->update_skylight_columns);
        for (int s = 0; s < 16; ++s)
        {
            const struct chunk_sec *sec = chunk_sec_at(c, s);
            uint8_t has = sec != NULL;

            h = cwd_mix(h, &has, 1);
            if (sec == NULL) continue;
            h = cwd_mix(h, chunk_sec_sky(sec), SEC_NIB_BYTES);
            h = cwd_mix(h, chunk_sec_blocklight(sec), SEC_NIB_BYTES);
        }
    }
    return h;
}

/* the digest's parts, a line per chunk (a difference's first field) */
static inline void cw_digest_detail(FILE *out, long long t, const struct world *w)
{
    if (w == NULL) return;
    fprintf(out, "%lld world epoch %llu wseq %llu\n", t, (unsigned long long)w->epoch, (unsigned long long)w->wseq);
    for (size_t i = 0; i < w->lon; ++i)
    {
        int64_t key = w->load_order[i];
        const struct chunk *c = world_chunk((struct world *)w, (int)(int32_t)(key & 0xffffffff), (int)(key >> 32));

        if (c == NULL) continue;
        uint64_t hl = 0xcbf29ce484222325ULL, hp = hl;
        uint32_t has = 0;

        for (int s = 0; s < 16; ++s)
        {
            const struct chunk_sec *sec = chunk_sec_at(c, s);

            if (sec == NULL) continue;
            has |= 1u << s;
            hl = cwd_mix(hl, chunk_sec_sky(sec), SEC_NIB_BYTES);
            hl = cwd_mix(hl, chunk_sec_blocklight(sec), SEC_NIB_BYTES);
        }
        hp = cwd_mix(hp, c->precip, sizeof c->precip);
        fprintf(out,
                "%lld %d,%d mask %04x has %04x height %016llx min %d precip %016llx stamp %llu edge %llu %llu %llu %llu "
                "wseq %llu cursor %d gap %d cols %016llx light %016llx\n",
                t, c->cx, c->cz, c->mask, has, (unsigned long long)cwd_mix(0, c->height, sizeof c->height), c->height_min,
                (unsigned long long)hp, (unsigned long long)c->stamp, (unsigned long long)c->edge_stamp[0],
                (unsigned long long)c->edge_stamp[1], (unsigned long long)c->edge_stamp[2],
                (unsigned long long)c->edge_stamp[3], (unsigned long long)c->wseq, c->queued_light_checks,
                c->gap_lighting_updated, (unsigned long long)cwd_mix(0, c->update_skylight_columns, 256),
                (unsigned long long)hl);
    }
}

#endif
