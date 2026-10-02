/* The render feed (feed.h, pool.h struct pool_feed).
 *
 * Per env, a table of the client world's chunks within the render distance
 * plus one (a section's mesh reads a block past its edge): each chunk's write
 * sequence (world.h wseq) at the last feed and a hash per section. After a
 * step, a chunk whose write sequence moved has its sections hashed again and
 * the ones that changed go in the feed whole; a chunk new to the range
 * (loaded, or the camera moved) goes in the load list with every section, a
 * chunk that left it in the unload list. Everything the feed keeps is the
 * pool's (the C library's heap): an env's image is rewritten by a reset. */
#include "feed.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../engine/clientworld.h"
#include "../engine/combat.h"
#include "../engine/env.h"
#include "../engine/player.h"
#include "../engine/potion.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/world.h"
#include "pool.h"

struct fentry {
    int32_t cx, cz;
    uint8_t used, seen;
    uint64_t wseq, head;          /* the write sequence, the header's hash (mask, heights, biomes) */
    uint64_t sec[16];             /* each section's hash, 0 absent */
};

/* a growable array of the C library's */
struct garr {
    unsigned char *p;
    size_t n, cap, size;
};

struct pool_feed_state {
    int rd;
    struct fentry *tab;
    size_t cap, used;
    int dim, reset;
    const struct world *cw;
    struct garr secs, loads, lsecs, unloads, ents;
    unsigned char *out;
    size_t out_cap;
};

static void *garr_add(struct garr *a)
{
    if (a->n == a->cap)
    {
        size_t cap = a->cap ? 2 * a->cap : 16;
        unsigned char *p = proc_malloc(cap * a->size);
        if (a->n) memcpy(p, a->p, a->n * a->size);
        proc_free(a->p);
        a->p = p;
        a->cap = cap;
    }
    return a->p + a->n++ * a->size;
}

struct pool_feed_state *feed_new(const struct pool_obs *obs)
{
    struct pool_feed_state *f = proc_calloc(1, sizeof *f);
    f->rd = obs->rd > 0 ? obs->rd : 4;
    int side = 2 * (f->rd + 1) + 1;
    f->cap = 16;
    while (f->cap < (size_t)(2 * side * side)) f->cap *= 2;
    f->tab = proc_calloc(f->cap, sizeof *f->tab);
    f->secs.size = f->lsecs.size = sizeof(struct pool_feed_section);
    f->loads.size = sizeof(struct pool_feed_chunk);
    f->unloads.size = 2 * sizeof(int32_t);
    f->ents.size = sizeof(struct pool_feed_ent);
    f->reset = 1;
    return f;
}

void feed_free(struct pool_feed_state *f)
{
    if (f == NULL) return;
    proc_free(f->tab);
    proc_free(f->secs.p);
    proc_free(f->loads.p);
    proc_free(f->lsecs.p);
    proc_free(f->unloads.p);
    proc_free(f->ents.p);
    proc_free(f->out);
    proc_free(f);
}

void feed_reset(struct pool_feed_state *f, struct session *ss, struct env *e)
{
    (void)ss;
    (void)e;
    if (f == NULL) return;
    memset(f->tab, 0, f->cap * sizeof *f->tab);
    f->used = 0;
    f->reset = 1;
    f->cw = NULL;
}

void feed_tick(struct pool_feed_state *f, struct session *ss)
{
    (void)f;
    (void)ss;
}

/* ------------------------------------------------------------- sections */

static uint64_t mixw(uint64_t h, uint64_t w)
{
    h ^= w;
    h *= 0x9e3779b97f4a7c15ull;
    return h ^ (h >> 29);
}

static uint64_t hash_bytes(uint64_t h, const unsigned char *p, size_t n)
{
    size_t i = 0;
    for (; i + 8 <= n; i += 8)
    {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = mixw(h, w);
    }
    for (; i < n; ++i) h = mixw(h, p[i]);
    return h;
}

void feed_section_fill(struct pool_feed_section *o, const struct chunk *c, int s)
{
    const struct chunk_sec *sec = chunk_sec_at(c, s);
    o->cx = c->cx;
    o->sy = s;
    o->cz = c->cz;
    o->empty = sec == NULL;
    if (sec == NULL)
    {
        memset(o->ids, 0, sizeof o->ids);
        memset(o->meta, 0, sizeof o->meta);
        memset(o->sky, 0, sizeof o->sky);
        memset(o->block, 0, sizeof o->block);
        return;
    }
    for (int i = 0; i < 4096; ++i) o->ids[i] = (uint16_t)(sec->ids[i] | (sec->ids_hi ? sec->ids_hi[i] << 8 : 0));
    memcpy(o->meta, chunk_sec_metas(sec), sizeof o->meta);
    memcpy(o->sky, chunk_sec_sky(sec), sizeof o->sky);
    memcpy(o->block, chunk_sec_blocklight(sec), sizeof o->block);
}

uint64_t feed_section_hash(const struct pool_feed_section *o)
{
    if (o->empty) return 0;
    uint64_t h = 0xcbf29ce484222325ull;
    h = hash_bytes(h, (const unsigned char *)o->ids, sizeof o->ids);
    h = hash_bytes(h, o->meta, sizeof o->meta);
    h = hash_bytes(h, o->sky, sizeof o->sky);
    h = hash_bytes(h, o->block, sizeof o->block);
    return h | 1;
}

/* the section's hash straight from its band, equal to feed_section_hash of
 * what feed_section_fill writes */
static uint64_t band_hash(const struct chunk *c, int s)
{
    struct pool_feed_section tmp;
    if (chunk_sec_at(c, s) == NULL) return 0;
    feed_section_fill(&tmp, c, s);
    return feed_section_hash(&tmp);
}

static uint64_t head_hash(const struct chunk *c)
{
    uint64_t h = mixw(0x51ed27, c->mask);
    h = hash_bytes(h, (const unsigned char *)c->height, sizeof c->height);
    return hash_bytes(h, c->biome, sizeof c->biome);
}

static struct fentry *lookup(struct pool_feed_state *f, int cx, int cz, int make)
{
    size_t i = ((uint32_t)cx * 0x9e3779b1u ^ (uint32_t)cz * 0x85ebca6bu) & (f->cap - 1);
    for (;; i = (i + 1) & (f->cap - 1))
    {
        struct fentry *e = &f->tab[i];
        if (!e->used)
        {
            if (!make) return NULL;
            memset(e, 0, sizeof *e);
            e->used = 1;
            e->cx = cx;
            e->cz = cz;
            ++f->used;
            return e;
        }
        if (e->cx == cx && e->cz == cz) return e;
    }
}

/* the table without the entries not seen this feed (open addressing:
 * rebuilt, not tombstoned) */
static void drop_unseen(struct pool_feed_state *f)
{
    size_t n = 0;
    for (size_t i = 0; i < f->cap; ++i)
        if (f->tab[i].used && f->tab[i].seen) ++n;
    struct fentry *old = f->tab;
    f->tab = proc_calloc(f->cap, sizeof *f->tab);
    f->used = 0;
    for (size_t i = 0; i < f->cap; ++i)
    {
        if (!old[i].used) continue;
        if (!old[i].seen)
        {
            int32_t *u = garr_add(&f->unloads);
            u[0] = old[i].cx;
            u[1] = old[i].cz;
            continue;
        }
        struct fentry *e = lookup(f, old[i].cx, old[i].cz, 1);
        *e = old[i];
    }
    proc_free(old);
}

static void load_chunk(struct pool_feed_state *f, struct fentry *e, const struct chunk *c, int with_sections)
{
    struct pool_feed_chunk *h = garr_add(&f->loads);
    h->cx = c->cx;
    h->cz = c->cz;
    h->first = (int32_t)f->lsecs.n;
    h->count = 0;
    memcpy(h->biome, c->biome, sizeof h->biome);
    memcpy(h->height, c->height, sizeof h->height);
    e->head = head_hash(c);
    for (int s = 0; s < 16; ++s)
    {
        if (chunk_sec_at(c, s) == NULL) { e->sec[s] = 0; continue; }
        struct pool_feed_section *o = garr_add(&f->lsecs);
        feed_section_fill(o, c, s);
        e->sec[s] = feed_section_hash(o);
        ++h->count;
    }
    (void)with_sections;
}

/* ---------------------------------------------------------- the camera */

static void camera(struct pool_camera *o, struct session *ss)
{
    struct client_player *cp = &ss->cp;
    struct world *cw = ss->client_world;
    memset(o, 0, sizeof *o);
    o->x = cp->e.pos_x;
    o->y = cp->e.pos_y;
    o->z = cp->e.pos_z;
    o->yaw = cp->rotation_yaw;
    o->pitch = cp->rotation_pitch;
    o->y_offset = cp->e.y_offset;
    o->health = cp->sv.health;
    o->hurt = cp->sv.hurt_time;
    o->max_hurt = cp->sv.max_hurt_time > 0 ? cp->sv.max_hurt_time : 10;
    o->death = cp->sv.death_time;
    o->portal = cp->portal.time;
    o->sleeping = cp->sv.sleeping;
    o->bed_rot = -1;
    o->confusion = potion_map_get(&cp->sv.potions, 9) != NULL;
    o->night_vision = potion_map_get(&cp->sv.potions, 16) != NULL;
    o->blindness = potion_map_get(&cp->sv.potions, 15) != NULL;
    o->dim = cp->dimension;
    o->world_time = cp->cw_day;
    o->rain = cp->cw_rain;
    if (ss->sr != NULL)
        for (int i = 0; i < ss->sr->nworlds; ++i)
            if (ss->sr->w[i].dim == cp->dimension) o->thunder = ss->sr->w[i].st.prev_thundering_strength;
    if (cw == NULL) return;
    int bx = (int)floor(cp->e.pos_x), bz = (int)floor(cp->e.pos_z);
    int by = (int)floor(cp->e.pos_y - (double)cp->e.y_offset + 1.8 * 0.66);
    o->brightness = world_get_light(cw, LIGHT_SKY, bx, by, bz) << 20 | world_get_light(cw, LIGHT_BLOCK, bx, by, bz) << 4;
    const struct chunk *c = world_chunk(cw, bx >> 4, bz >> 4);
    o->biome = c ? c->biome[(bz & 15) << 4 | (bx & 15)] : -1;
    if (o->sleeping)
    {
        int sx = bx, sy = (int)floor(cp->e.pos_y), sz = bz;
        if ((world_get_block(cw, sx, sy, sz) & 4095) == 26) o->bed_rot = world_get_meta(cw, sx, sy, sz) & 3;
    }
    /* ActiveRenderInfo.getBlockAtEntityViewpoint at pt 1.0 (first person) */
    int vx = bx, vy = (int)floor(cp->e.pos_y), vz = bz;
    int id = world_get_block(cw, vx, vy, vz);
    if (id >= 8 && id <= 11)
    {
        int meta = world_get_meta(cw, vx, vy, vz);
        float h = (float)(meta >= 8 ? 0 : meta + 1) / 9.0F - 0.11111111F;
        if (cp->e.pos_y >= (double)((float)(vy + 1) - h)) id = world_get_block(cw, vx, vy + 1, vz);
    }
    o->view_material = id == 8 || id == 9 ? 1 : id == 10 || id == 11 ? 2 : 0;
}

static void entities(struct pool_feed_state *f, struct session *ss)
{
    const struct combat_state *c = ss->cp.combat;
    if (c == NULL) return;
    const struct clientworld *w = &c->client;
    for (int i = 0; i < w->nents; ++i)
    {
        const struct client_entity *e = &w->ents[i];
        if (e->is_dead) continue;
        struct pool_feed_ent *o = garr_add(&f->ents);
        memset(o, 0, sizeof *o);
        o->id = e->id;
        o->kind = e->is_fireball ? (e->is_large_fireball ? 1001 : 1002) : e->kind;
        o->x = e->x;
        o->y = e->y;
        o->z = e->z;
        o->yaw = e->yaw;
        o->pitch = e->pitch;
        o->head_yaw = e->head_yaw;
        o->flags = (e->flags0 & 1 ? 1 : 0) | (e->flags0 & 2 ? 2 : 0) | (e->flags0 & 32 ? 4 : 0) | (e->age_width > 0 ? 8 : 0);
        o->death = e->death_time;
        o->item.item = -1;
    }
}

/* ------------------------------------------------------------- the feed */

const void *feed_finish(struct pool_feed_state *f, struct session *ss, size_t *bytes)
{
    f->secs.n = f->loads.n = f->lsecs.n = f->unloads.n = f->ents.n = 0;
    struct client_player *cp = &ss->cp;
    struct world *cw = ss->client_world;
    int reset = f->reset;
    /* a dimension change is a new WorldClient: everything held goes */
    if (cw != f->cw || cp->dimension != f->dim)
    {
        if (!reset) memset(f->tab, 0, f->cap * sizeof *f->tab);
        f->used = 0;
        reset = 1;
        f->cw = cw;
        f->dim = cp->dimension;
    }
    f->reset = 0;
    for (size_t i = 0; i < f->cap; ++i) f->tab[i].seen = 0;
    if (cw != NULL)
    {
        int r = f->rd + 1;
        int pcx = (int)floor(cp->e.pos_x / 16.0), pcz = (int)floor(cp->e.pos_z / 16.0);
        for (int cx = pcx - r; cx <= pcx + r; ++cx)
            for (int cz = pcz - r; cz <= pcz + r; ++cz)
            {
                const struct chunk *c = world_chunk(cw, cx, cz);
                if (c == NULL || c->packed != NULL) continue;
                struct fentry *e = lookup(f, cx, cz, 0);
                if (e == NULL)
                {
                    e = lookup(f, cx, cz, 1);
                    e->seen = 1;
                    e->wseq = c->wseq;
                    load_chunk(f, e, c, 1);
                    continue;
                }
                e->seen = 1;
                if (c->wseq == e->wseq) continue;
                e->wseq = c->wseq;
                uint64_t hh = head_hash(c);
                if (hh != e->head)
                {
                    /* the header again, no sections: an update */
                    struct pool_feed_chunk *h = garr_add(&f->loads);
                    h->cx = cx;
                    h->cz = cz;
                    h->first = (int32_t)f->lsecs.n;
                    h->count = 0;
                    memcpy(h->biome, c->biome, sizeof h->biome);
                    memcpy(h->height, c->height, sizeof h->height);
                    e->head = hh;
                }
                for (int s = 0; s < 16; ++s)
                {
                    uint64_t h = band_hash(c, s);
                    if (h == e->sec[s]) continue;
                    struct pool_feed_section *o = garr_add(&f->secs);
                    feed_section_fill(o, c, s);
                    e->sec[s] = h;
                }
            }
    }
    drop_unseen(f);
    entities(f, ss);

    size_t need = sizeof(struct pool_feed) + f->secs.n * f->secs.size + f->loads.n * f->loads.size +
                  f->lsecs.n * f->lsecs.size + f->unloads.n * f->unloads.size + f->ents.n * f->ents.size;
    if (need > f->out_cap)
    {
        proc_free(f->out);
        f->out_cap = need + need / 4;
        f->out = proc_malloc(f->out_cap);
    }
    struct pool_feed *h = (struct pool_feed *)f->out;
    memset(h, 0, sizeof *h);
    h->reset = (uint32_t)reset;
    camera(&h->cam, ss);
    size_t off = sizeof *h;
#define PUT(field, arr)                                                  \
    do {                                                                 \
        h->off_##field = (uint32_t)off;                                  \
        h->n##field = (uint32_t)(arr).n;                                 \
        if ((arr).n) memcpy(f->out + off, (arr).p, (arr).n * (arr).size); \
        off += (arr).n * (arr).size;                                     \
    } while (0)
    PUT(sections, f->secs);
    PUT(loads, f->loads);
    PUT(load_sections, f->lsecs);
    PUT(unloads, f->unloads);
    PUT(ents, f->ents);
#undef PUT
    h->bytes = (uint32_t)off;
    *bytes = off;
    return f->out;
}
