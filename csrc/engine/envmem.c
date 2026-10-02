/* One environment's bytes by owner (envmem.h). */
#include "envmem.h"

#include <stdlib.h>
#include <string.h>

#include "chunkload.h"
#include "combat.h"
#include "env.h"
#include "pickobj.h"
#include "serverreplay.h"
#include "session.h"
#include "tileentity.h"
#include "world.h"
#include "living.h"

_Static_assert(ARENA_BANDS == 1 << 17, "envmem.h sizes its band set by ARENA_BANDS");

static const char *const NAMES[EM_N] = {
    "server-chunks", "server-worlds", "region", "client", "renderer", "entities", "ticks", "scratch", "other",
};

const char *envmem_name(int owner)
{
    return owner >= 0 && owner < EM_N ? NAMES[owner] : "?";
}

void envmem_add(struct envmem *m, int owner, const char *what, size_t bytes)
{
    m->b[owner] += bytes;
    for (int i = 0; i < m->nitems; ++i)
        if (m->items[i].owner == owner && !strcmp(m->items[i].what, what))
        {
            m->items[i].bytes += bytes;
            return;
        }
    if (m->nitems < ENVMEM_ITEMS) m->items[m->nitems++] = (struct envmem_item){owner, what, bytes};
}

/* A world's chunks (with their tile entity stores) under owner/chunks, the
 * rest of the world (the map, the load order, the tile entity lists and
 * objects) under owner/tables. */
/* A chunk's bytes: its struct, its packed blob and the bands no owner counted
 * before (a band shared between the server's chunk and the client's copy is
 * the server's). */
static size_t chunk_bytes(struct envmem *m, const struct chunk *c)
{
    const struct slot_pool *bands = &nw_env->arena.bands;
    size_t n = sizeof *c + c->packed_n + (size_t)c->tes.cap * sizeof *c->tes.v;

    for (int s = 0; s < 16; ++s)
    {
        const struct chunk_sec *sec = chunk_sec_at(c, s);
        if (sec == NULL) continue;
        int32_t i = pool_holds(bands, sec) ? pool_index(bands, sec) : -1;
        if (i >= 0 && i < ARENA_BANDS)
        {
            if (m->seen[i >> 6] & (uint64_t)1 << (i & 63)) continue;
            m->seen[i >> 6] |= (uint64_t)1 << (i & 63);
        }
        n += chunk_sec_bytes(sec);
    }
    return n;
}

static int world_bytes(struct envmem *m, const struct world *w, int owner, const char *chunks, const char *tables)
{
    size_t n = 0;
    int k = 0;
    for (size_t i = 0; i < w->cap; ++i)
    {
        const struct chunk *c = chunk_ptr(w->slot[i]);
        if (c == NULL) continue;
        n += chunk_bytes(m, c);
        ++k;
    }
    envmem_add(m, owner, chunks, n);
    size_t t = w->cap * sizeof *w->slot + w->locap * sizeof *w->load_order;
    t += (size_t)(w->te_cap + w->te_added_cap + w->te_reg_cap + w->te_registry_cap) * sizeof(void *);
    t += (size_t)w->te_registry_n * sizeof(struct tile_entity);
    envmem_add(m, owner, tables, t);
    return k;
}

/* A tick set's blocks in the list slab (its heap, runs, membership and
 * column tables), as the slab's classes round them */
static size_t ticks_bytes(const struct ticks_set *s)
{
    size_t b = slab_block_bytes((size_t)s->capheap * sizeof *s->heap) + slab_block_bytes(s->nslots * ticks_slot_size())
             + slab_block_bytes(s->ncol * sizeof *s->ckeys) + slab_block_bytes(s->ncol * sizeof *s->ccounts);

    for (int r = 0; r < TICKS_RUNS; ++r) b += slab_block_bytes((size_t)s->runs[r].cap * sizeof *s->runs[r].e);
    return b;
}

/* A fixed struct's committed bytes: the pages of [p, p + len) that have been
 * written (a calloc'd or static struct costs what it touches), in full below
 * 64 KB (pages it shares with its neighbours). */
static size_t range_resident(const void *p, size_t len)
{
    if (len < 65536) return len;
    uintptr_t lo = ((uintptr_t)p + 4095) & ~(uintptr_t)4095, hi = ((uintptr_t)p + len) & ~(uintptr_t)4095;
    return fixed_array_resident((const void *)lo, (hi - lo) / 4096, 4096) + (lo - (uintptr_t)p) + ((uintptr_t)p + len - hi);
}

static size_t pool_live(const struct slot_pool *p)
{
    return (size_t)p->live * p->size;
}

void envmem_session(const struct session *ss, struct envmem *m)
{
    memset(m, 0, sizeof *m);
    const struct env *e = nw_env;
    const struct serverreplay *sr = ss->sr;
    size_t tick_slab = 0;

    if (sr != NULL)
    {
        const struct populate *pops[3] = {&sr->pop, &sr->hell, &sr->sky};
        for (int i = 0; i < 3; ++i)
        {
            m->chunks[0] += world_bytes(m, &pops[i]->world, EM_SERVER_CHUNKS, "chunks", "world tables");
            envmem_add(m, EM_SERVER_WORLDS, "struct populate (world, providers)", range_resident(pops[i], sizeof *pops[i]));
        }
        for (int i = 0; i < 3; ++i)
        {
            const struct sr_dimstate *d = &sr->dims[i];
            m->chunks[1] += world_bytes(m, &d->saved, EM_REGION, "parked chunks", "world tables");
            envmem_add(m, EM_REGION, "struct world", range_resident(&d->saved, sizeof d->saved));
            size_t rt = (size_t)d->capchunk_ticks * sizeof *d->chunk_ticks;
            for (int k = 0; k < d->nchunk_ticks; ++k) rt += (size_t)d->chunk_ticks[k].n * sizeof *d->chunk_ticks[k].ticks;
            envmem_add(m, EM_REGION, "saved entities, ticks, lazy list",
                       (size_t)d->nsaved_ents * sizeof *d->saved_ents + rt + (size_t)d->caplazy * sizeof *d->lazy);

            envmem_add(m, EM_ENTITIES, "entity worlds (iew, fhw, anw)",
                       range_resident(&d->iew, sizeof d->iew) + range_resident(&d->fhw, sizeof d->fhw)
                       + range_resident(&d->anw, sizeof d->anw));
            envmem_add(m, EM_ENTITIES, "pass order, bolts, chunk index",
                       fixed_array_resident(d->ents, SR_MAX_ORDER, sizeof *d->ents)
                       + (size_t)d->capbolts * sizeof *d->bolts + (size_t)d->anw.capchunks * sizeof *d->anw.chunks);
            size_t tb = ticks_bytes(&d->ticks);
            tick_slab += tb;
            envmem_add(m, EM_TICKS, "pending tick sets (list slab)", tb);
            envmem_add(m, EM_SCRATCH, "write records", fixed_array_resident(d->extra, ST_MAX_WRITES, sizeof *d->extra));
            envmem_add(m, EM_SERVER_WORLDS, "player managers, providers", cl_bytes(d->cl));
            const struct spawner *sp = &d->spawner;
            envmem_add(m, EM_SCRATCH, "spawner records",
                       fixed_array_resident(sp->recs, SP_MAX_RECS, sizeof *sp->recs)
                       + fixed_array_resident(sp->boxes, SP_MAX_BOXES, sizeof *sp->boxes)
                       + fixed_array_resident(sp->out, SP_MAX_OUT, sizeof *sp->out)
                       + fixed_array_resident(sp->rec_index, (size_t)sp->rec_index_cap, sizeof *sp->rec_index)
                       + fixed_array_resident(sp->rec_flags, SP_MAX_RECS, sizeof *sp->rec_flags));
        }
        for (int i = 0; i < sr->nworlds; ++i)
            envmem_add(m, EM_SCRATCH, "write records",
                       fixed_array_resident(sr->w[i].st.writes, ST_MAX_WRITES, sizeof *sr->w[i].st.writes)
                       + fixed_array_resident(sr->w[i].st.spawns, ST_MAX_SPAWNS, sizeof *sr->w[i].st.spawns));
        envmem_add(m, EM_OTHER, "struct serverreplay (rest)",
                   range_resident(sr, sizeof *sr) - range_resident(&sr->pop, sizeof sr->pop)
                   - range_resident(&sr->hell, sizeof sr->hell) - range_resident(&sr->sky, sizeof sr->sky)
                   - range_resident(&sr->dims, sizeof sr->dims));
        const struct pickobj_state *po = sr->pickobj;
        if (po != NULL)
            envmem_add(m, EM_CLIENT, "pick objects",
                       sizeof *po + fixed_array_resident(po->pend, (size_t)po->cappend, sizeof *po->pend)
                       + fixed_array_resident(po->objs, (size_t)po->capobjs, sizeof *po->objs));
        if (sr->combat != NULL)
        {
            envmem_add(m, EM_CLIENT, "client entities", range_resident(&sr->combat->client, sizeof sr->combat->client));
            envmem_add(m, EM_OTHER, "tracker, pending packets",
                       range_resident(sr->combat, sizeof *sr->combat)
                       - range_resident(&sr->combat->client, sizeof sr->combat->client)
                       + fixed_array_resident(sr->combat->pending, (size_t)sr->combat->cap_pending,
                                              sizeof *sr->combat->pending));
        }
    }

    if (ss->client_world != NULL)
    {
        m->chunks[2] += world_bytes(m, ss->client_world, EM_CLIENT, "chunks", "world tables");
        envmem_add(m, EM_CLIENT, "struct world", range_resident(ss->client_world, sizeof *ss->client_world));
    }
    envmem_add(m, EM_OTHER, "struct session (players)", range_resident(ss, sizeof *ss));
    envmem_add(m, EM_REGION, "spill index and buffer", rspill_index_bytes(&e->rspill));
    m->spilled = rspill_records(&e->rspill);
    m->spill_file = e->rspill.end;

    /* the arena: live slots (the bands are the chunks', counted above) */
    const struct arena_env *a = &e->arena;
    envmem_add(m, EM_ENTITIES, "livings", pool_live(&a->livings));
    envmem_add(m, EM_ENTITIES, "livings' parts (AI, trading, inventory)",
               pool_live(&a->ai_parts) + pool_live(&a->villager_parts) + pool_live(&a->player_parts));
    envmem_add(m, EM_ENTITIES, "list entries, items, falling, doors, paths",
               pool_live(&a->an_ents) + pool_live(&a->ie_ents) + pool_live(&a->fh_ents) + pool_live(&a->doors)
               + pool_live(&a->paths));
    /* the pools' slots past the live ones that are still resident (a slot
     * given back is cleared, so each slot up to the pool's high-water mark
     * stays committed); the bands' and chunks' are the chunk owners' */
    const struct slot_pool *lp[] = {&a->livings, &a->ai_parts, &a->villager_parts, &a->player_parts};
    size_t lres = 0, llive = 0;
    for (size_t i = 0; i < sizeof lp / sizeof *lp; ++i)
    {
        lres += fixed_array_resident(lp[i]->base, (size_t)lp[i]->top, lp[i]->size);
        llive += pool_live(lp[i]);
    }
    envmem_add(m, EM_ENTITIES, "livings and parts: free slots, resident", lres > llive ? lres - llive : 0);
    const struct slot_pool *rest[] = {&a->an_ents, &a->ie_ents, &a->fh_ents, &a->doors, &a->paths};
    size_t rres = 0, rlive = 0;
    for (size_t i = 0; i < sizeof rest / sizeof *rest; ++i)
    {
        rres += fixed_array_resident(rest[i]->base, (size_t)rest[i]->top, rest[i]->size);
        rlive += pool_live(rest[i]);
    }
    envmem_add(m, EM_ENTITIES, "list entries .. paths: free slots, resident", rres > rlive ? rres - rlive : 0);
    /* the chunks' and their bands' slots: the live ones are the chunk
     * owners' (above), a freed one still resident is its pool's */
    const struct slot_pool *cp[] = {&a->bands, &a->nibs, &a->chunks};
    const char *cw[] = {"chunk bands: free slots, resident", "bands' nibble arrays: free slots, resident",
                        "chunks: free slots, resident"};
    for (int i = 0; i < 3; ++i)
    {
        size_t res = fixed_array_resident(cp[i]->base, (size_t)cp[i]->top, cp[i]->size);
        /* the nibble pool's sixteen constant arrays are live and no band's */
        size_t live = pool_live(cp[i]) - (cp[i] == &a->nibs ? 16 * cp[i]->size : 0);
        envmem_add(m, EM_SERVER_CHUNKS, cw[i], res > live ? res - live : 0);
    }
    /* the list slab: its live blocks less the tick sets' (counted under
     * ticks), and the pages its free blocks still hold (a free block of 1 MB
     * or more keeps only its first) */
    size_t slab_live, slab_free_bytes;
    slab_census(&a->slab, &slab_live, &slab_free_bytes);
    size_t slab_res = fixed_array_resident(a->slab.base, a->slab.used, 1);
    envmem_add(m, EM_ENTITIES, "list slab: section lists, chunk tables, paths",
               slab_live > tick_slab ? slab_live - tick_slab : 0);
    envmem_add(m, EM_OTHER, "list slab: free blocks, resident", slab_res > slab_live ? slab_res - slab_live : 0);
    /* the path finder's store: the pages its searches have written */
    envmem_add(m, EM_SCRATCH, "path finder store",
               fixed_array_resident(a->pf.pts, PF_MAX_POINTS, sizeof *a->pf.pts)
               + fixed_array_resident(a->pf.heap, PF_MAX_POINTS, sizeof *a->pf.heap)
               + fixed_array_resident(a->pf.keys, 2 * PF_MAX_POINTS, sizeof *a->pf.keys)
               + fixed_array_resident(a->pf.vals, 2 * PF_MAX_POINTS, sizeof *a->pf.vals));

    /* the env struct: the client's particles are the client's, the rest
     * scratch and fixed tables */
    size_t fx = range_resident(&e->survival.fx, sizeof e->survival.fx);
    size_t expl = range_resident(&e->scratch.expl, sizeof e->scratch.expl);
    size_t itag = range_resident(&e->itemtag, sizeof e->itemtag);
    size_t s2c = range_resident(&e->survival.s2c, sizeof e->survival.s2c);
    envmem_add(m, EM_OTHER, "env: packet queues", s2c);
    envmem_add(m, EM_CLIENT, "particles", fx);
    size_t own = ticks_bytes(&e->ticks.own);
    tick_slab += own;
    envmem_add(m, EM_TICKS, "pending tick sets (list slab)", own);
    envmem_add(m, EM_SCRATCH, "env: explosion frames", expl);
    envmem_add(m, EM_SCRATCH, "env: item tag store", itag);
    envmem_add(m, EM_SCRATCH, "env: rest", range_resident(e, sizeof *e) - range_resident(&e->arena, sizeof e->arena) - fx - expl - itag - s2c);
}

void envmem_chunk(struct envmem *m, int owner, const char *what, const struct chunk *c)
{
    envmem_add(m, owner, what, c ? chunk_bytes(m, c) : 0);
}

size_t envmem_total(const struct envmem *m)
{
    size_t t = 0;
    for (int i = 0; i < EM_N; ++i) t += m->b[i];
    return t;
}

void envmem_print(FILE *f, const char *label, const struct envmem *m, int detail)
{
    fprintf(f, "%s total %.1f MB:", label, envmem_total(m) / 1048576.0);
    for (int i = 0; i < EM_N; ++i) fprintf(f, " %s %.1f", NAMES[i], m->b[i] / 1048576.0);
    fprintf(f, " (chunks: server %d, region %d, client %d; spilled %llu, file %.1f MB)\n", m->chunks[0], m->chunks[1],
            m->chunks[2], (unsigned long long)m->spilled, m->spill_file / 1048576.0);
    for (int i = 0; detail && i < m->nitems; ++i)
        if (m->items[i].bytes >= 262144)
            fprintf(f, "    %-14s %-42s %7.1f MB\n", NAMES[m->items[i].owner], m->items[i].what,
                    m->items[i].bytes / 1048576.0);
}
