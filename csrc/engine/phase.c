/* The phase table and the tracer (phase.h). The tracer is the harness's: it
 * runs only when a test turns it on, reads the session's state between two
 * phases and writes nothing the tick reads. */
#include "phase.h"

#include <stdlib.h>
#include <string.h>

#include "chunkload.h"
#include "combat.h"
#include "endfight.h"
#include "entity_nbt.h"
#include "env.h"
#include "player.h"
#include "portal.h"
#include "serverreplay.h"
#include "session.h"
#include "tileentity.h"
#include "villagers.h"
#include "world.h"

#define S PHM(SCALARS)
#define R PHM(WRAND)
#define D PHM(DET)
#define B PHM(BLOCKS)
#define P PHM(PENDING)
#define E PHM(ENTITIES)
#define T PHM(TILES)
#define W PHM(RECORD)
#define V PHM(VILLAGE)
#define PL PHM(PLAYER)
#define K PHM(TRACKER)
#define CL PHM(CLIENT)

/* A phase's reads are what its outcome depends on; its writes are the only
 * classes it may change (--phase-check holds every recording to them). A
 * chunk load anywhere (a read off the loaded edge, a spawn, a door scan)
 * populates, and population writes blocks, pending ticks, tile entities,
 * World.rand, Det and the write record; those phases list the lot. */
const struct phase_desc PHASES[PH_COUNT] = {
    [PH_R0] = {"R0", "the header's MobMove entries", "session.c take_setups",
               E | PL, E | K},
    [PH_C1] = {"C1", "NetHandlerPlayClient's chunk and block packets",
               "serverreplay_sync_client_chunks, serverreplay_sync_client",
               B | W | PL, CL | PL},
    [PH_C2] = {"C2", "Minecraft.runTick: input, EntityClientPlayerMP.onUpdate, sendMotionUpdates",
               "client_player_tick", CL | PL | K | D, CL | K | D},
    [PH_TP] = {"TP", "MinecraftServer's paused save", "serverreplay_pause_save",
               B | PL, B | W},
    [PH_DV] = {"DV", "the Dev entries (agent only)", "session.c run_dev", PHM_ALL, PHM_ALL},
    [PH_S1] = {"S1", "World.updateWeather, areAllPlayersAsleep, wakeAllPlayers",
               "servertick.c st_weather_sleep", S | R | PL, S | R | B | P | W | PL},
    [PH_S2] = {"S2", "SpawnerAnimals.findChunksForSpawning", "servertick.c st_spawner (spawner_tick)",
               S | R | D | B | E | PL, S | R | D | B | P | E | T | W | V | K},
    [PH_S1T] = {"S1T", "calculateSkylightSubtracted, the total and day time",
                "servertick.c st_clocks", S | R, S},
    [PH_S3] = {"S3", "ChunkProviderServer.unloadQueuedChunks", "servertick.c st_unload (cl_unload_step)",
               S | B | P | E | T, B | P | E | T | K},
    [PH_S4] = {"S4", "WorldServer.tickUpdates", "servertick.c tick_updates",
               S | R | D | B | P | E, S | R | D | B | P | E | T | W | PL | K},
    [PH_S5] = {"S5", "func_147456_g's head: the ambient countdown, the player's light check",
               "servertick.c tick_player_light", S | R | B | PL, S | R | B},
    [PH_S6] = {"S6", "func_147456_g's chunk loop: light flags, lightning, ice and snow, random ticks",
               "servertick.c tick_chunks", S | R | D | B | P | E, S | R | D | B | P | E | T | W | PL | K},
    [PH_SG] = {"SG", "VillageSiege.tick", "servertick.c village_siege_tick",
               S | R | D | B | E | V, S | R | D | B | P | E | T | W | V | K},
    [PH_S9] = {"S9", "updatePlayerInstances, removeStalePortalLocations, VillageCollection.tick",
               "serverreplay.c sr_world_tail", S | R | B | E | V | PL,
               S | R | D | B | P | E | T | W | V | PL | K},
    [PH_BE] = {"BE", "WorldServer.func_147488_Z, the block events", "world.c world_block_events_run",
               B | T, T},
    [PH_S10] = {"S10", "World.updateEntities' weatherEffects", "serverreplay.c sr_weather_effects",
                S | R | B | E, S | R | D | B | P | E | T | W | PL | K},
    [PH_S11] = {"S11", "World.updateEntities' loadedEntityList walk", "serverreplay.c sr_entity_pass",
                PHM_ALL & ~CL, PHM_ALL & ~CL},
    [PH_S12] = {"S12", "World.updateEntities' tile entity walk", "serverreplay.c sr_tile_pass",
                S | R | D | B | E | T, S | R | D | B | P | E | T | W | K},
    [PH_NT] = {"NT", "EntityTracker.updateTrackedEntities", "serverreplay.c sr_track (combat_server_finish)",
               E | PL | K, PL | K},
    [PH_S13] = {"S13", "Rows.blkHash over the world's writes", "serverreplay.c sr_fold_writes", W, W},
    [PH_SE] = {"SE", "WorldServer.tick without chunks", "serverreplay.c sr_tick_empty", S | R, S | R},
    [PH_N1] = {"N1", "NetHandlerPlayServer: processPlayer and the row's packets",
               "server_player_tick", PHM_ALL & ~CL, PHM_ALL & ~CL},
    [PH_N2] = {"N2", "PlayerManager.updateMountedMovingPlayer", "cl_c03",
               S | B | E | PL, S | R | D | B | P | E | T | W | PL | K},
    [PH_N1E] = {"N1E", "the action's end", "serverreplay_action_end", E | W, E | W | PL | K},
    [PH_N3] = {"N3", "EntityTracker's packets for the client", "serverreplay_queue_packets",
               E | PL | K, PL | K},
    [PH_T] = {"T", "MinecraftServer.saveAllWorlds' unload marks", "serverreplay_save_marks",
              B | PL, B},
    [PH_VD] = {"VD", "IntegratedServer.tick's view distance change (ServerConfigurationManager.func_152611_a)",
               "serverreplay_set_view_distance", S | B | E | PL, S | R | D | B | P | E | T | W | PL | K},
};

const char *const PHASE_CLASS_NAMES[PHK_COUNT] = {
    "scalars", "wrand", "det", "blocks", "pending", "entities", "tiles", "record", "village", "player", "tracker",
    "client",
};

#undef S
#undef R
#undef D
#undef B
#undef P
#undef E
#undef T
#undef W
#undef V
#undef PL
#undef K
#undef CL

#define PH (nw_env->phase)

/* ---------------------------------------------------------------- hashing */

static inline uint64_t mix(uint64_t h, uint64_t v)
{
    h ^= v;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h;
}

static uint64_t mix_bytes(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    size_t i = 0;

    for (; i + 8 <= n; i += 8)
    {
        uint64_t v;
        memcpy(&v, b + i, 8);
        h = mix(h, v);
    }

    uint64_t v = 0;
    memcpy(&v, b + i, n - i);
    return mix(h, v ^ (uint64_t)n << 48);
}

static uint64_t dbits(double d)
{
    uint64_t v;
    memcpy(&v, &d, 8);
    return v;
}

static uint64_t fbits(float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    return v;
}

static uint64_t mix_str(uint64_t h, const char *s)
{
    return s ? mix_bytes(h, s, strlen(s)) : mix(h, 0x5eed);
}

/* ------------------------------------------------------------ the classes */

static int world_live(const struct serverreplay *sr, const struct sr_world *w)
{
    const struct sr_dimstate *a = &sr->dims[sr_dim_index(w->dim)];
    return w->st.w != NULL && (w->dim == 0 || (a->live && a->dim == w->dim));
}

static uint64_t k_scalars(const struct serverreplay *sr)
{
    uint64_t h = 1;

    for (int i = 0; i < sr->nworlds; ++i)
    {
        const struct sr_world *w = &sr->w[i];
        const struct servertick *s = &w->st;

        h = mix(h, (uint64_t)(int64_t)w->dim);
        h = mix(h, (uint32_t)ST_LCG(s));
        h = mix(h, (uint32_t)s->skylight_subtracted);
        h = mix(h, fbits(s->raining_strength) | fbits(s->prev_raining_strength) << 32);
        h = mix(h, fbits(s->thundering_strength) | fbits(s->prev_thundering_strength) << 32);
        h = mix(h, (uint64_t)s->total_time);
        h = mix(h, (uint64_t)s->world_time);
        h = mix(h, (uint32_t)s->daylight_cycle);
        h = mix(h, (uint32_t)s->rain_time | (uint64_t)(uint32_t)s->thunder_time << 32);
        h = mix(h, (uint32_t)s->raining | (uint64_t)(uint32_t)s->thundering << 32);
        h = mix(h, (uint64_t)s->next_tick_entry);
        h = mix(h, (uint32_t)s->all_players_sleeping);
        h = mix(h, (uint32_t)s->ambient_tick_countdown | (uint64_t)(uint32_t)s->difficulty << 32);
        h = mix(h, (uint32_t)s->siege_phase | (uint64_t)(uint32_t)s->siege_mustering << 32);
        h = mix(h, (uint32_t)s->siege_count | (uint64_t)(uint32_t)s->siege_timer << 32);
        h = mix(h, (uint32_t)w->update_entity_tick);
        h = mix(h, (uint64_t)w->pm_sweep);
    }

    return mix(h, (uint64_t)nw_env->ticks.next_entry_id);
}

static uint64_t k_wrand(const struct serverreplay *sr)
{
    uint64_t h = 2;

    for (int i = 0; i < sr->nworlds; ++i) h = mix(h, ST_RAND(&sr->w[i].st).seed);
    return h;
}

static uint64_t mix_rng(uint64_t h, const det_rng *r)
{
    h = mix(h, r->r.seed);
    h = mix(h, (uint32_t)r->have_next_next_gaussian);
    return mix(h, r->have_next_next_gaussian ? dbits(r->next_next_gaussian) : 0);
}

static uint64_t k_det(const struct serverreplay *sr)
{
    const det_state *d = &SR_DET(sr);
    uint64_t h = mix(3, (uint64_t)d->world_seed);

    for (int r = 0; r < DET_ROLES; ++r)
    {
        h = mix_rng(h, &d->seeder[r]);
        h = mix_rng(h, &d->math[r]);
        h = mix(h, (uint32_t)d->next_id[r]);
    }

    for (const det_split *s = d->splits; s != NULL; s = s->next)
    {
        h = mix_str(h, s->name);
        for (int r = 0; r < DET_ROLES; ++r)
        {
            h = mix(h, s->used[r]);
            if (s->used[r]) h = mix_rng(h, &s->d[r]);
        }
    }

    return mix(h, SR_SHUF(sr).seed);
}

/* One chunk whole: the cells and light of its sections, its maps and flags.
 * precipitationHeightMap is left out: a lazy cache, not state. */
static uint64_t chunk_digest(uint64_t h, const struct chunk *c)
{
    h = mix(h, (uint32_t)c->cx | (uint64_t)(uint32_t)c->cz << 32);
    h = mix(h, c->mask);

    for (int s = 0; s < 16; ++s)
    {
        const struct chunk_sec *sec = chunk_sec_at(c, s);

        if (sec == NULL) continue;
        h = mix(h, (uint64_t)s);
        h = mix_bytes(h, sec->ids, SEC_CELLS);
        h = mix_bytes(h, chunk_sec_metas(sec), SEC_CELLS / 2);
        h = mix_bytes(h, chunk_sec_sky(sec), SEC_CELLS / 2);
        h = mix_bytes(h, chunk_sec_blocklight(sec), SEC_CELLS / 2);
        if (sec->ids_hi != NULL) h = mix_bytes(h, sec->ids_hi, SEC_CELLS / 2);
    }

    if (c->packed != NULL) h = mix_bytes(h, c->packed, c->packed_n);
    h = mix_bytes(h, c->height, sizeof c->height);
    h = mix(h, (uint32_t)c->height_min);
    h = mix_bytes(h, c->update_skylight_columns, sizeof c->update_skylight_columns);
    h = mix_bytes(h, c->biome, sizeof c->biome);
    h = mix_bytes(h, c->sections_ticking, sizeof c->sections_ticking);
    h = mix(h, c->gap_lighting_updated | c->light_populated << 8 | c->terrain_populated << 16 |
                   (uint64_t)c->populated << 24 | (uint64_t)c->no_sky << 32);
    h = mix(h, (uint32_t)c->queued_light_checks);
    h = mix(h, (uint64_t)c->inhabited_time);
    return mix(h, (uint32_t)c->tes.n);
}

static int chunk_cmp(const void *a, const void *b)
{
    const struct chunk *x = *(const struct chunk *const *)a, *y = *(const struct chunk *const *)b;
    if (x->cx != y->cx) return x->cx < y->cx ? -1 : 1;
    return x->cz < y->cz ? -1 : x->cz > y->cz;
}

/* The chunk map, and the chunks written since the write sequence stood at
 * since, in (cx, cz) order. */
static uint64_t world_written(uint64_t h, const struct world *w, uint64_t since)
{
    h = mix(h, w->used);
    h = mix(h, w->lon);
    for (size_t k = 0; k < w->lon; ++k) h = mix(h, (uint64_t)w->load_order[k]);
    h = mix(h, (uint64_t)w->reqs.n);

    size_t n = 0;
    const struct chunk **list = PH.written;

    for (size_t i = 0; i < w->cap; ++i)
    {
        const struct chunk *c = chunk_ptr(w->slot[i]);

        if (c == NULL || c->wseq <= since) continue;
        if (n == PH.written_cap)
        {
            PH.written_cap = n ? 2 * n : 256;
            list = realloc(list, PH.written_cap * sizeof *list);
            if (list == NULL) { fprintf(stderr, "phase: out of memory\n"); exit(1); }
            PH.written = list;
        }
        list[n++] = c;
    }

    qsort(list, n, sizeof *list, chunk_cmp);
    h = mix(h, n);
    for (size_t k = 0; k < n; ++k) h = chunk_digest(h, list[k]);
    return h;
}

static uint64_t k_blocks(const struct serverreplay *sr)
{
    uint64_t h = 4;

    for (int i = 0; i < sr->nworlds; ++i)
    {
        const struct sr_world *w = &sr->w[i];

        if (!world_live(sr, w)) continue;
        h = mix(h, (uint64_t)(int64_t)w->dim);
        h = world_written(h, w->st.w, PH.epoch[i]);

        const struct sr_dimstate *a = &sr->dims[sr_dim_index(w->dim)];
        /* the store's chunks in memory and in its spill file alike */
        if (a->saved_init) h = mix(mix(h, a->saved.used + nw_env->rspill.dims[sr_dim_index(w->dim)].n), a->saved.wseq);
        if (a->cl != NULL) h = mix(h, cl_digest(a->cl));
    }
    return h;
}

static uint64_t k_pending(const struct serverreplay *sr)
{
    uint64_t h = 5;

    for (int i = 0; i < 3; ++i)
    {
        const struct sr_dimstate *a = &sr->dims[i];
        const struct ticks_set *t = &a->ticks;
        uint64_t acc = 0;

        if (!a->live) continue;
        for (int p = 0; p < TICKS_PARTS; ++p)
        {
            int m;
            const struct tick_entry *part = ticks_set_part(t, p, &m);

            for (int k = 0; k < m; ++k)
            {
                const struct tick_entry *e = &part[k];
                uint64_t v = mix(mix(mix(mix((uint32_t)e->x | (uint64_t)(uint32_t)e->z << 32,
                                             (uint32_t)e->y | (uint64_t)(uint32_t)e->block << 32),
                                         (uint64_t)e->time),
                                     (uint32_t)e->priority),
                                 (uint64_t)e->entry);
                acc += mix(v, 0x7ead);
            }
        }
        h = mix(mix(mix(h, (uint64_t)i), (uint32_t)ticks_set_count(t)), acc);
        h = mix(h, (uint32_t)(a->nchunk_ticks + nw_env->rspill.dims[i].nticks));
    }
    return h;
}

static uint64_t k_entities(const struct session *ss)
{
    struct serverreplay *sr = ss->sr;
    uint64_t h = 6;

    for (int i = 0; i < 3; ++i)
    {
        const struct sr_dimstate *a = &sr->dims[i];

        if (!a->live) continue;
        h = mix(h, (uint64_t)(int64_t)a->dim);
        h = mix(h, (uint32_t)a->nents);
        h = mix(h, (uint32_t)a->iew.n | (uint64_t)(uint32_t)a->fhw.n << 32);
        h = mix(h, (uint32_t)a->anw.n | (uint64_t)(uint32_t)a->anw.iew.n << 32);

        for (int k = 0; k < a->nents; ++k)
        {
            const struct sr_ent *rec = &a->ents[k];
            int id;

            if (rec->pool == 3)
            {
                h = mix(h, 3);
                continue;
            }
            nbt_scratch_begin();
            nbt *tag = sr_entry_nbt(ss->s, &ss->sp, rec, &id);
            h = ent_digest_add(h, id, tag);
            nbt_scratch_end();
        }

        h = mix(h, (uint32_t)a->nbolts);
        for (int k = 0; k < a->nbolts; ++k)
        {
            const struct lightning_bolt *b = &a->bolts[k];
            h = mix(mix(mix(h, dbits(b->x)), dbits(b->y)), dbits(b->z));
            h = mix_rng(h, &b->rand);
            h = mix(h, (uint32_t)b->state | (uint64_t)(uint32_t)b->living_time << 32);
            h = mix(h, (uint32_t)b->dead | (uint64_t)(uint32_t)b->ticks_existed << 32);
            h = mix(h, (uint64_t)b->vertex);
        }
    }
    return h;
}

static uint64_t k_tiles(const struct serverreplay *sr)
{
    uint64_t h = 7;

    for (int i = 0; i < sr->nworlds; ++i)
    {
        const struct sr_world *w = &sr->w[i];

        if (!world_live(sr, w)) continue;
        const struct world *wd = w->st.w;
        h = mix(h, (uint32_t)wd->te_n | (uint64_t)(uint32_t)wd->te_added_n << 32);
        for (int k = 0; k < wd->te_n; ++k)
        {
            char *text = te_render(wd->te_list[k]);
            h = mix_str(h, text);
            free(text);
        }
    }
    return h;
}

static uint64_t k_record(const struct serverreplay *sr)
{
    uint64_t h = mix(mix(8, sr->blk_hash), (uint64_t)sr->blk_count);

    for (int i = 0; i < sr->nworlds; ++i)
    {
        const struct servertick *s = &sr->w[i].st;

        h = mix(h, (uint32_t)s->nwrites);
        h = mix_bytes(h, s->writes, (size_t)s->nwrites * sizeof *s->writes);
        h = mix(h, (uint32_t)s->nspawns);
        for (int k = 0; k < s->nspawns; ++k)
        {
            const struct st_spawn *sp = &s->spawns[k];
            h = mix(h, (uint32_t)sp->id);
            h = mix_str(h, sp->cls);
            h = mix(mix(mix(h, dbits(sp->x)), dbits(sp->y)), dbits(sp->z));
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        const struct sr_dimstate *a = &sr->dims[i];

        if (!a->live) continue;
        h = mix(h, (uint32_t)a->nextra);
        h = mix_bytes(h, a->extra, (size_t)a->nextra * sizeof *a->extra);
    }

    h = mix(h, (uint32_t)sr->nxw);
    for (int k = 0; k < sr->nxw; ++k)
    {
        h = mix(h, (uint64_t)(int64_t)sr->xw[k].dim);
        h = mix_bytes(h, &sr->xw[k].w, sizeof sr->xw[k].w);
    }
    return mix(h, (uint32_t)sr->ndev_writes | (uint64_t)(uint32_t)sr->dev_block_count << 32);
}

static uint64_t mix_teleporter(uint64_t h, const struct teleporter *tp)
{
    h = mix(mix(h, TP_RAND(tp).seed), (uint64_t)tp->world_time);
    h = mix(h, tp->cache.count);
    for (size_t k = 0; k < tp->cache.key_count; ++k)
    {
        const struct portal_cache_entry *e = portal_cache_get((struct portal_cache *)&tp->cache, tp->cache.keys[k]);
        h = mix(h, (uint64_t)tp->cache.keys[k]);
        if (e == NULL) continue;
        h = mix(h, (uint32_t)e->x | (uint64_t)(uint32_t)e->z << 32);
        h = mix(h, (uint32_t)e->y);
        h = mix(h, (uint64_t)e->last_update_time);
    }
    return h;
}

static uint64_t k_village(const struct serverreplay *sr)
{
    const struct village_collection *vc = &sr->vc;
    const struct village_siege *vs = &sr->vs;
    uint64_t h = 9;

    h = mix(h, (uint32_t)vc->tick_counter);
    h = mix(h, (uint32_t)vc->num_positions);
    h = mix_bytes(h, vc->positions, (size_t)vc->num_positions * sizeof vc->positions[0]);
    h = mix(h, (uint32_t)vc->num_new_doors);
    h = mix_bytes(h, vc->new_doors, (size_t)vc->num_new_doors * sizeof vc->new_doors[0]);
    h = mix(h, (uint32_t)vc->num_villages);
    for (int i = 0; i < vc->num_villages; ++i)
    {
        const struct village *v = vc_list_at((struct village_collection *)vc, i);

        h = mix(h, (uint32_t)v->num_doors);
        for (int k = 0; k < v->num_doors; ++k)
            h = mix_bytes(h, door_at(v->doors[k]), sizeof(struct village_door_info));
        h = mix(h, (uint32_t)v->center_x | (uint64_t)(uint32_t)v->center_z << 32);
        h = mix(h, (uint32_t)v->center_y | (uint64_t)(uint32_t)v->village_radius << 32);
        h = mix(h, (uint32_t)v->center_helper_x | (uint64_t)(uint32_t)v->center_helper_z << 32);
        h = mix(h, (uint32_t)v->center_helper_y | (uint64_t)(uint32_t)v->last_add_door_timestamp << 32);
        h = mix(h, (uint32_t)v->tick_counter | (uint64_t)(uint32_t)v->num_villagers << 32);
        h = mix(h, (uint32_t)v->no_breed_ticks | (uint64_t)(uint32_t)v->num_iron_golems << 32);
        h = mix(h, (uint32_t)v->num_player_rep);
        for (int k = 0; k < v->num_player_rep; ++k)
            h = mix(mix_str(h, v->player_rep[k].name), (uint32_t)v->player_rep[k].rep);
        h = mix(h, (uint32_t)v->num_agressors);
        for (int k = 0; k < v->num_agressors; ++k)
        {
            h = mix(h, v->agressors[k].agressor);
            h = mix(h, (uint32_t)v->agressors[k].agression_time | (uint64_t)(uint32_t)v->agressors[k].agressor_id << 32);
        }
    }

    h = mix(h, (uint32_t)vs->field_75535_b | (uint64_t)(uint32_t)vs->field_75536_c << 32);
    h = mix(h, (uint32_t)vs->field_75533_d | (uint64_t)(uint32_t)vs->field_75534_e << 32);
    h = mix(h, (uint32_t)vs->the_village | (uint64_t)(uint32_t)vs->field_75532_g << 32);
    h = mix(h, (uint32_t)vs->field_75538_h | (uint64_t)(uint32_t)vs->field_75539_i << 32);

    h = mix_teleporter(h, &sr->tp_over);
    h = mix_teleporter(h, &sr->tp_hell);
    return mix_teleporter(h, &sr->tp_end);
}

static uint64_t mix_s2c(uint64_t h, const struct s2c_queue *q)
{
    h = mix(h, (uint32_t)q->n);
    for (int k = 0; k < q->n; ++k)
    {
        const struct s2c_pkt *p = &q->q[k];
        h = mix(h, (uint32_t)p->kind | (uint64_t)(uint32_t)p->window << 32);
        h = mix(mix(mix(mix(mix(h, dbits(p->f0)), dbits(p->f1)), dbits(p->f2)), dbits(p->f3)), dbits(p->f4));
        h = mix(h, (uint32_t)p->i0 | (uint64_t)(uint32_t)p->i1 << 32);
        h = mix(h, (uint32_t)p->i2 | (uint64_t)(uint32_t)p->slot << 32);
        h = mix_bytes(h, &p->st, sizeof p->st);
        h = mix(h, (uint32_t)p->ntrlist | (uint64_t)(uint32_t)p->s37_slot << 32);
    }
    return h;
}

static uint64_t k_player(const struct session *ss)
{
    const struct serverreplay *sr = ss->sr;
    uint64_t h = 10;

    nbt_scratch_begin();
    nbt *tag = ent_nbt_player(&ss->sp);
    h = ent_digest_add(h, sr->player_entity_id, tag);
    nbt_scratch_end();

    h = mix_s2c(h, s2c_out_peek());
    h = mix(h, (uint32_t)sr->nsend | (uint64_t)(uint32_t)sr->send_dim << 32);
    h = mix_bytes(h, sr->send_queue, (size_t)sr->nsend * 2 * sizeof *sr->send_queue);
    return mix(h, (uint32_t)sr->nsent);
}

static uint64_t k_tracker(const struct serverreplay *sr)
{
    const struct combat_state *c = sr->combat;
    uint64_t h = 12;

    if (c == NULL) return h;
    h = mix(h, (uint32_t)c->tracker.nentries);
    for (int k = 0; k < c->tracker.nentries; ++k)
    {
        const struct tracker_entry *e = &c->tracker.entries[k];
        h = mix_str(mix(h, (uint32_t)e->id), tracker_class_name(e->cls));
        h = mix(mix(mix(h, dbits(e->x)), dbits(e->y)), dbits(e->z));
        h = mix(mix(mix(h, dbits(e->motion_x)), dbits(e->motion_y)), dbits(e->motion_z));
        h = mix(h, fbits(e->yaw) | fbits(e->pitch) << 32);
        h = mix(h, fbits(e->head_yaw) | (uint64_t)(uint32_t)e->last_scaled_x << 32);
        h = mix(h, (uint32_t)e->last_scaled_y | (uint64_t)(uint32_t)e->last_scaled_z << 32);
        h = mix(h, (uint32_t)e->last_yaw | (uint64_t)(uint32_t)e->last_pitch << 32);
        h = mix(h, (uint32_t)e->last_head_yaw | (uint64_t)(uint32_t)e->ticks << 32);
        h = mix(mix(mix(h, dbits(e->last_motion_x)), dbits(e->last_motion_y)), dbits(e->last_motion_z));
        h = mix(h, (uint32_t)e->ticks_since_forced_teleport | (uint64_t)(uint32_t)e->attached_id << 32);
        h = mix(h, (uint64_t)e->riding_entity | (uint64_t)e->is_riding << 8 | (uint64_t)e->is_airborne << 16 |
                       (uint64_t)e->watch_initialized << 24 | (uint64_t)e->watched << 32 |
                       (uint64_t)e->velocity_changed << 40 | (uint64_t)e->dw_dirty << 48);
        h = mix(mix(mix(h, dbits(e->watch_x)), dbits(e->watch_y)), dbits(e->watch_z));
        h = mix(h, (uint32_t)e->size | (uint64_t)(uint32_t)e->sent_dw16 << 32);
        h = mix(h, fbits(e->dw.health) | (uint64_t)(uint32_t)e->dw.flags0 << 32);
        h = mix(h, (uint32_t)e->dw.air | (uint64_t)(uint32_t)e->dw.arrows << 32);
        h = mix(h, (uint32_t)e->dw.dw16);
    }

    h = mix(h, (uint32_t)c->npending);
    for (int k = 0; k < c->npending; ++k)
    {
        const struct tracker_packet *p = &c->pending[k].pkt;
        h = mix(h, (uint32_t)p->kind | (uint64_t)(uint32_t)p->id << 32);
        h = mix(h, (uint32_t)p->type | (uint64_t)(uint32_t)p->data << 32);
        h = mix(h, (uint32_t)p->x | (uint64_t)(uint32_t)p->y << 32);
        h = mix(h, (uint32_t)p->z | (uint64_t)(uint32_t)p->yaw << 32);
        h = mix(h, (uint32_t)p->pitch | (uint64_t)(uint32_t)p->head_yaw << 32);
        h = mix(h, (uint32_t)p->mx | (uint64_t)(uint32_t)p->my << 32);
        h = mix(h, (uint32_t)p->mz | (uint64_t)(uint32_t)p->num_ids << 32);
        if (p->num_ids > 0 && p->num_ids <= 127) h = mix_bytes(h, p->ids, (size_t)p->num_ids * sizeof p->ids[0]);
        h = mix(h, (uint32_t)p->slot | (uint64_t)(uint32_t)p->leash << 32);
        h = mix(h, (uint32_t)p->vehicle_id | (uint64_t)(uint32_t)p->effect << 32);
        h = mix(h, (uint32_t)p->num_chunks | (uint64_t)(uint32_t)p->block << 32);
        h = mix(h, (uint32_t)p->meta | (uint64_t)(uint32_t)p->num_records << 32);
    }

    h = mix(h, (uint32_t)c->player_watch_initialized);
    return mix(mix(mix(h, dbits(c->player_watch_x)), dbits(c->player_watch_y)), dbits(c->player_watch_z));
}

static uint64_t k_client(const struct session *ss)
{
    const struct client_player *cp = &ss->cp;
    const struct entity *e = &cp->e;
    uint64_t h = 11;

    h = mix(mix(mix(h, dbits(e->pos_x)), dbits(e->pos_y)), dbits(e->pos_z));
    h = mix(mix(mix(h, dbits(e->motion_x)), dbits(e->motion_y)), dbits(e->motion_z));
    h = mix(h, fbits(cp->rotation_yaw) | fbits(cp->rotation_pitch) << 32);
    h = mix(h, fbits(e->fall_distance) | (uint64_t)e->on_ground << 32);
    h = mix(h, fbits(cp->sv.health) | (uint64_t)(uint32_t)cp->sv.air << 32);
    h = mix_bytes(h, &cp->sv.food, sizeof cp->sv.food);
    h = mix_bytes(h, cp->sv.inv, sizeof cp->sv.inv);
    h = mix(h, (uint32_t)cp->sv.current_item);
    if (PH.co != NULL)
    {
        /* the row's packets by value: click_sent, click_ret and chat point
         * at the client player's arrays, wherever the harness keeps its
         * session (a thread's, a process's), so their contents are hashed,
         * not the addresses, which differ from run to run */
        struct client_out co = *PH.co;
        co.click_sent = NULL;
        co.click_ret = NULL;
        co.chat = NULL;
        h = mix_bytes(h, &co, sizeof co);
        if (PH.co->click_sent != NULL)
        {
            h = mix_bytes(h, cp->click_sent, sizeof cp->click_sent);
            h = mix_bytes(h, cp->click_ret, sizeof cp->click_ret);
        }
        if (PH.co->chat != NULL) h = mix_bytes(h, PH.co->chat, sizeof *PH.co->chat);
    }

    /* the client's entity mirrors (combat.c's WorldClient view) */
    const struct combat_state *c = ss->sr->combat;
    if (c != NULL)
    {
        const struct clientworld *cw = &c->client;
        h = mix(mix(h, cw->rand.seed), (uint32_t)cw->update_lcg);
        h = mix(h, (uint32_t)cw->nents);
        for (int k = 0; k < cw->nents; ++k)
        {
            const struct client_entity *e = &cw->ents[k];
            h = mix(h, (uint32_t)e->id | (uint64_t)e->is_dead << 32);
            h = mix(mix(mix(h, dbits(e->x)), dbits(e->y)), dbits(e->z));
            h = mix(mix(mix(h, dbits(e->motion_x)), dbits(e->motion_y)), dbits(e->motion_z));
            h = mix(h, fbits(e->yaw) | fbits(e->pitch) << 32);
            h = mix(h, (uint32_t)e->server_pos_x | (uint64_t)(uint32_t)e->server_pos_z << 32);
            h = mix(h, (uint32_t)e->server_pos_y | (uint64_t)(uint32_t)e->new_pos_rotation_increments << 32);
        }
    }
    if (ss->client_world != NULL) h = world_written(h, ss->client_world, PH.epoch[3]);
    return h;
}

static uint64_t class_digest(int k)
{
    const struct session *ss = PH.ss;

    switch (k)
    {
    case PHK_SCALARS: return k_scalars(ss->sr);
    case PHK_WRAND: return k_wrand(ss->sr);
    case PHK_DET: return k_det(ss->sr);
    case PHK_BLOCKS: return k_blocks(ss->sr);
    case PHK_PENDING: return k_pending(ss->sr);
    case PHK_ENTITIES: return k_entities(ss);
    case PHK_TILES: return k_tiles(ss->sr);
    case PHK_RECORD: return k_record(ss->sr);
    case PHK_VILLAGE: return k_village(ss->sr);
    case PHK_PLAYER: return k_player(ss);
    case PHK_TRACKER: return k_tracker(ss->sr);
    case PHK_CLIENT: return k_client(ss);
    }
    return 0;
}

/* ------------------------------------------------------------- the tracer */

static int traced(void)
{
    return (PH.on & PHASE_TRACE) && PH.ss != NULL && PH.ss->server_rows && PH.ss->sr != NULL;
}

static void violation(const char *fmt, int id, const char *what)
{
    if (PH.violations++ == 0)
    {
        char label[16];
        if (id >= PH_WORLD_FIRST && id <= PH_WORLD_LAST) snprintf(label, sizeof label, "%d.%s", PH.world_dim, PHASES[id].label);
        else snprintf(label, sizeof label, "%s", PHASES[id].label);
        char msg[200];
        snprintf(msg, sizeof msg, fmt, label, what);
        snprintf(PH.first_violation, sizeof PH.first_violation, "row %lld %s", (long long)PH.row, msg);
    }
}

int phase_begin_(int id)
{
    int taken = 0;
    if (!traced())
    {
        if ((PH.on & PHASE_KERNEL) && ((PH.kernel_mask >> id) & 1)) taken = PH.kernel(id, 0);
        if (PH.on & PHASE_PROF) phase_prof_begin_(id);
        return taken;
    }
    if (PH.open >= 0) violation("phase %s begins inside %s", id, PHASES[PH.open].label);
    PH.open = id;

    const struct serverreplay *sr = PH.ss->sr;
    for (int i = 0; i < 3; ++i) PH.epoch[i] = i < sr->nworlds && sr->w[i].st.w != NULL ? sr->w[i].st.w->wseq : 0;
    PH.epoch[3] = PH.ss->client_world != NULL ? PH.ss->client_world->wseq : 0;

    if (PH.check)
        for (int k = 0; k < PHK_COUNT; ++k) PH.before[k] = class_digest(k);
    if ((PH.kernel_mask >> id) & 1) taken = PH.kernel(id, 0);
    /* the profile times the phase, not the digests around it */
    if (PH.on & PHASE_PROF) phase_prof_begin_(id);
    return taken;
}

void phase_end_(int id)
{
    if ((PH.kernel_mask >> id) & 1) PH.kernel(id, 1);
    if (PH.on & PHASE_PROF) phase_prof_end_(id);
    if (!traced()) return;
    if (PH.open != id) violation("phase %s ends inside %s", id, PH.open >= 0 ? PHASES[PH.open].label : "none");
    PH.open = -1;

    unsigned writes = PHASES[id].writes;
    uint64_t h = mix(0x9e3779b97f4a7c15ULL, (uint64_t)id);

    for (int k = 0; k < PHK_COUNT; ++k)
    {
        int declared = (writes >> k) & 1;
        if (!declared && !PH.check) continue;

        uint64_t v = class_digest(k);

        if (declared)
        {
            h = mix(h, v);
            if (PH.check && v != PH.before[k] && PH.observed[id][k]++ == 0) PH.first[id][k] = PH.rows;
        }
        else if (v != PH.before[k])
        {
            ++PH.undeclared[id][k];
            violation("phase %s wrote %s, which it does not declare", id, PHASE_CLASS_NAMES[k]);
        }
    }

    ++PH.phases;
    char *p = PH.line + PH.linen;
    size_t left = sizeof PH.line - (size_t)PH.linen;
    int n;

    if (id >= PH_WORLD_FIRST && id <= PH_WORLD_LAST)
        n = snprintf(p, left, " %d.%s=%016llx", PH.world_dim, PHASES[id].label, (unsigned long long)h);
    else n = snprintf(p, left, " %s=%016llx", PHASES[id].label, (unsigned long long)h);
    if (n > 0 && (size_t)n < left) PH.linen += n;
}

void phase_row_begin_(const struct session *ss, int64_t t)
{
    PH.ss = ss;
    PH.open = -1;
    PH.row = t;
    PH.linen = snprintf(PH.line, sizeof PH.line, "%lld", (long long)t);
    if (PH.on & PHASE_PROF) phase_prof_row_begin_(ss->server_rows && ss->sr != NULL);
}

void phase_row_end_(void)
{
    if (PH.on & PHASE_PROF) phase_prof_row_end_();
    if (!traced()) return;
    if (PH.linen < (int)sizeof PH.line - 1) PH.line[PH.linen++] = '\n';
    PH.trace_hash = mix_bytes(PH.trace_hash, PH.line, (size_t)PH.linen);
    ++PH.rows;
    if (PH.out != NULL) fwrite(PH.line, 1, (size_t)PH.linen, PH.out);
    PH.co = NULL;
}

void phase_trace_start(FILE *out, int check)
{
    PH.on |= PHASE_TRACE;
    PH.check = check;
    PH.out = out;
    PH.open = -1;
    PH.trace_hash = 0x6a09e667f3bcc908ULL;
    PH.rows = PH.phases = PH.violations = 0;
    PH.first_violation[0] = 0;
    memset(PH.undeclared, 0, sizeof PH.undeclared);
    memset(PH.observed, 0, sizeof PH.observed);
    memset(PH.first, 0, sizeof PH.first);

    if (out == NULL) return;
    fprintf(out, "# phases (label: vanilla; native; reads; writes)\n");
    for (int i = 0; i < PH_COUNT; ++i)
    {
        fprintf(out, "# %s: %s; %s;", PHASES[i].label, PHASES[i].vanilla, PHASES[i].native);
        for (int pass = 0; pass < 2; ++pass)
        {
            unsigned m = pass ? PHASES[i].writes : PHASES[i].reads;
            int any = 0;
            for (int k = 0; k < PHK_COUNT; ++k)
                if ((m >> k) & 1) fprintf(out, "%s%s", any++ ? "," : " ", PHASE_CLASS_NAMES[k]);
            fprintf(out, pass ? "\n" : ";");
        }
    }
}

void phase_trace_summary(FILE *to)
{
    fprintf(to, "phase trace: %ld rows, %ld phases, digest %016llx", PH.rows, PH.phases,
            (unsigned long long)PH.trace_hash);
    if (PH.check) fprintf(to, ", check: %ld undeclared writes%s%s", PH.violations,
                          PH.violations ? ", first: " : "", PH.first_violation);
    fprintf(to, "\n");
    for (int i = 0; i < PH_COUNT; ++i)
        for (int k = 0; k < PHK_COUNT; ++k)
        {
            if (PH.undeclared[i][k])
                fprintf(to, "phase check: %s wrote %s in %d phase runs\n", PHASES[i].label, PHASE_CLASS_NAMES[k],
                        PH.undeclared[i][k]);
            if (PH.observed[i][k])
                fprintf(to, "phase wrote: %s %s %d from row %ld\n", PHASES[i].label, PHASE_CLASS_NAMES[k],
                        PH.observed[i][k], PH.first[i][k]);
        }
}
