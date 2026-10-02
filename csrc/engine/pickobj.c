#include "arena.h"
#include "pickobj.h"
#include "env.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "blocks.h"
#include "chunkload.h"
#include "collide.h"
#include "det.h"
#include "dragon.h"
#include "endfight.h"
#include "fallhang.h"
#include "item_entity.h"
#include "items.h"
#include "jmath.h"
#include "living.h"
#include "player.h"
#include "raytrace.h"
#include "serverreplay.h"
#include "survival.h"
#include "world.h"

/* ------------------------------------------------------------ the server */

/* One server entity of a tracked kind, as the tracker reads it. */
struct obj_view {
    int id, kind;
    double x, y, z, mx, my, mz;
    float yaw, pitch, width, height;
    int cx, cz;                 /* chunkCoordX/Z */
    ie_ent *ie;
    fh_ent *fh;
    struct dragon_crystal_state *crystal;
    struct dragon_state *dragon;
};

/* The client's EntityDragon (a PK_DRAGON copy). */
struct pickobj_dragon {
    struct dragon_state d;
    struct dragon_interp ip;
};

static int ie_kind_any(const ie_ent *en);

static int ie_kind(const ie_ent *en)
{
    if (en == NULL || en->is_dead) return 0;
    return ie_kind_any(en);
}

/* the kind whether or not the entity is dead */
static int ie_kind_any(const ie_ent *en)
{
    if (en == NULL) return 0;
    if (en->kind == IE_TNT) return PK_TNT;
    /* the kinds the client only holds (the pick never tests them): their
     * copies' constructions and the S0D's pickup effect spend the client's
     * draws */
    if (en->kind == IE_ITEM) return PK_ITEM;
    if (en->kind == IE_ORB) return PK_ORB;
    if (en->kind == IE_ARROW) return PK_ARROW;
    if (en->kind == IE_SNOWBALL || en->kind == IE_EGG || en->kind == IE_ENDER_PEARL || en->kind == IE_EXP_BOTTLE ||
        en->kind == IE_POTION) return PK_THROWN;
    if (en->kind == IE_ENDER_EYE) return PK_EYE;
    return 0;
}

static int ie_view(ie_ent *ie, struct obj_view *v);

static int view_of(const struct serverreplay *sr, const struct sr_ent *rec, struct obj_view *v)
{
    memset(v, 0, sizeof *v);
    ie_ent *ie = NULL;

    if (rec->pool == 0) ie = sr_ent_p(sr, rec);
    else if (rec->pool == 2)
    {
        const struct an_ent *ae = sr_ent_p(sr, rec);
        if (ae->is_living) return 0;
        ie = ie_get(ae->ieh);
    }
    else if (rec->pool == 1)
    {
        fh_ent *fh = sr_ent_p(sr, rec);
        if (fh->is_dead) return 0;
        v->kind = fh->kind == FH_FALLING ? PK_FALLING : fh->kind == FH_PAINTING ? PK_PAINTING :
                  fh->kind == FH_KNOT ? PK_KNOT : PK_FRAME;
        v->id = fh->entity_id;
        v->x = fh->e.pos_x; v->y = fh->e.pos_y; v->z = fh->e.pos_z;
        v->mx = fh->e.motion_x; v->my = fh->e.motion_y; v->mz = fh->e.motion_z;
        v->yaw = fh->rotation_yaw;
        v->pitch = fh->rotation_pitch;
        v->width = fh->e.width;
        v->height = fh->e.height;
        v->cx = fh->chunk_x;
        v->cz = fh->chunk_z;
        v->fh = fh;
        return 1;
    }
    else if (rec->pool == SR_POOL_DRAGON)
    {
        struct dragon_state *d = sr_ent_p(sr, rec);
        if (d->dead) return 0;
        v->kind = PK_DRAGON;
        v->id = d->entity_id;
        v->x = d->x; v->y = d->y; v->z = d->z;
        v->mx = d->mx; v->my = d->my; v->mz = d->mz;
        v->yaw = d->yaw;
        v->pitch = 0.0F;
        v->width = 16.0F;
        v->height = 8.0F;
        v->cx = d->chunk_x;
        v->cz = d->chunk_z;
        v->dragon = d;
        return 1;
    }
    else if (rec->pool == SR_POOL_CRYSTAL)
    {
        struct dragon_crystal_state *c = sr_ent_p(sr, rec);
        if (c->dead || !c->in_world) return 0;
        v->kind = PK_CRYSTAL;
        v->id = c->entity_id;
        v->x = c->x; v->y = c->y; v->z = c->z;
        v->mx = c->mx; v->my = c->my; v->mz = c->mz;
        v->yaw = c->yaw;
        v->width = 2.0F;
        v->height = 2.0F;
        v->cx = mh_floor(c->x / 16.0);
        v->cz = mh_floor(c->z / 16.0);
        v->crystal = c;
        return 1;
    }
    else return 0;

    v->kind = ie_kind(ie);
    if (!v->kind) return 0;
    return ie_view(ie, v);
}

static int ie_view(ie_ent *ie, struct obj_view *v)
{
    v->id = ie->entity_id;
    v->x = ie->e.pos_x; v->y = ie->e.pos_y; v->z = ie->e.pos_z;
    v->mx = ie->e.motion_x; v->my = ie->e.motion_y; v->mz = ie->e.motion_z;
    v->yaw = ie->rotation_yaw;
    v->pitch = ie->rotation_pitch;
    v->width = ie->e.width;
    v->height = ie->e.height;
    v->cx = ie->chunk_x;
    v->cz = ie->chunk_z;
    v->ie = ie;
    return 1;
}

/* the fixed packet queue and client object list (made at the bind) */
#define PICKOBJ_MAX_PEND 8192

static void send(struct pickobj_state *s, const struct pickobj_packet *p)
{
    if (s->npend == PICKOBJ_MAX_PEND) abort();
    s->pend[s->npend] = *p;
    s->pend[s->npend++].seq = ++nw_env->combat.pkt_seq;
}

/* EntityTracker.trackEntity's range, frequency and velocity flag per kind
 * (the range capped at the tracker's entityViewDistance, the furthest
 * viewable block of the view distance it took at the load: a later view
 * distance change leaves it). */
static void entry_params(struct serverreplay *sr, int kind, struct tracker_entry *te)
{
    int range = 160, freq = 20, vel = 1;

    switch (kind)
    {
    case PK_TNT: range = 160; freq = 10; vel = 1; break;
    case PK_FALLING: range = 160; freq = 20; vel = 1; break;
    case PK_ITEM: range = 64; freq = 20; vel = 1; break;
    case PK_ORB: range = 160; freq = 20; vel = 1; break;
    case PK_ARROW: range = 64; freq = 20; vel = 0; break;
    case PK_THROWN: range = 64; freq = 10; vel = 1; break;
    case PK_EYE: range = 64; freq = 4; vel = 1; break;
    case PK_PAINTING: case PK_FRAME: case PK_KNOT: range = 160; freq = 0x7fffffff; vel = 0; break;
    case PK_CRYSTAL: range = 256; freq = 0x7fffffff; vel = 0; break;
    /* EntityDragon is an IMob, so the IAnimals branch takes it first */
    case PK_DRAGON: range = 80; freq = 3; vel = 1; break;
    }
    int furthest = sr->tracker_view * 16 - 16;
    /* the dragon's 80 as combat.c's livings keep it (the End's
     * EntityTracker took its entityViewDistance at the world's creation) */
    if (range > furthest && kind != PK_DRAGON) range = furthest;
    te->tracking_range = range;
    te->update_frequency = freq;
    te->send_velocity_updates = vel;
}

/* func_151260_c's packet for the kind, then the start-watching S12. */
static void spawn_packets(struct pickobj_state *s, struct tracker_entry *te, const struct obj_view *v)
{
    struct pickobj_packet p;
    memset(&p, 0, sizeof p);
    p.kind = PKO_SPAWN;
    p.id = v->id;
    p.obj_kind = v->kind;
    p.x = mh_floor(v->x * 32.0);
    p.y = mh_floor(v->y * 32.0);
    p.z = mh_floor(v->z * 32.0);
    p.pitch = (int8_t)mh_floor_float(v->pitch * 256.0F / 360.0F);
    p.yaw = (int8_t)mh_floor_float(v->yaw * 256.0F / 360.0F);

    if (v->kind == PK_DRAGON)
    {
        /* S0FPacketSpawnMob: the size's rounding on x and z, the angles
         * truncated, the velocity (no S12 follows a mob's spawn), the
         * watcher's health */
        const struct dragon_state *d = v->dragon;
        p.x = tracker_mul32_size(v->x, te->size);
        p.z = tracker_mul32_size(v->z, te->size);
        p.yaw = (int8_t)(int)(v->yaw * 256.0F / 360.0F);
        p.pitch = (int8_t)(int)(v->pitch * 256.0F / 360.0F);
        p.mx = (int)(fmin(fmax(v->mx, -3.9), 3.9) * 8000.0);
        p.my = (int)(fmin(fmax(v->my, -3.9), 3.9) * 8000.0);
        p.mz = (int)(fmin(fmax(v->mz, -3.9), 3.9) * 8000.0);
        p.health = d->health;
        send(s, &p);
        te->last_motion_x = v->mx;
        te->last_motion_y = v->my;
        te->last_motion_z = v->mz;
        return;
    }
    if (v->kind == PK_FALLING)
    {
        p.data = v->fh->block | v->fh->meta << 16;
    }
    else if (v->kind == PK_PAINTING || v->kind == PK_FRAME)
    {
        /* S10, and the frame's S0E with the tile times 32 */
        p.x = v->fh->tile_x;
        p.y = v->fh->tile_y;
        p.z = v->fh->tile_z;
        p.data = v->fh->dir;
        p.art = v->fh->art;
    }
    else if (v->kind == PK_KNOT)
    {
        /* func_151260_c's S0E type 77 with the tile times 32 */
        p.x = mh_floor_float((float)(v->fh->tile_x * 32));
        p.y = mh_floor_float((float)(v->fh->tile_y * 32));
        p.z = mh_floor_float((float)(v->fh->tile_z * 32));
    }
    send(s, &p);

    te->last_motion_x = v->mx;
    te->last_motion_y = v->my;
    te->last_motion_z = v->mz;
    if (te->send_velocity_updates)
    {
        struct pickobj_packet q;
        memset(&q, 0, sizeof q);
        q.kind = PKO_VELOCITY;
        q.id = v->id;
        q.mx = (int)(fmin(fmax(v->mx, -3.9), 3.9) * 8000.0);
        q.my = (int)(fmin(fmax(v->my, -3.9), 3.9) * 8000.0);
        q.mz = (int)(fmin(fmax(v->mz, -3.9), 3.9) * 8000.0);
        send(s, &q);
    }
}

static void destroy_later(struct pickobj_state *s, int id)
{
    if (s->ndestroyed >= PICKOBJ_MAX_TRACK) list_full("objects destroyed in one tick", PICKOBJ_MAX_TRACK);
    s->destroyed[s->ndestroyed++] = id;
}

static void destroy_now(struct pickobj_state *s, int id)
{
    struct pickobj_packet p;
    memset(&p, 0, sizeof p);
    p.kind = PKO_DESTROY;
    p.id = id;
    send(s, &p);
}

/* EntityTrackerEntry.tryStartWachingThis for the one player. */
static void try_watch(struct serverreplay *sr, struct pickobj_state *s, struct tracker_entry *te,
                      const struct obj_view *v)
{
    const struct server_player *p = sr->player;
    /* through the exit portal World.removeEntity took the player out of
     * playerEntities too: sendEventsToPlayers reaches no one */
    if (p == NULL || p->conquered_end || p->limbo || p->offworld) return;
    double dx = p->e.pos_x - (double)(te->last_scaled_x / 32);
    double dz = p->e.pos_z - (double)(te->last_scaled_z / 32);
    int pcx = mh_floor(p->e.pos_x / 16.0), pcz = mh_floor(p->e.pos_z / 16.0);
    int chunk_watched = abs(v->cx - pcx) <= sr->view && abs(v->cz - pcz) <= sr->view;
    /* as combat.c's livings: isPlayerWatchingThisChunk also wants the chunk
     * off the player's send queue (a falling block over a chunk not sent
     * yet reaches the client when the chunk does) */
    if (sr->d->cl != NULL) chunk_watched = cl_player_watching(sr->d->cl, v->cx, v->cz);
    /* a knot is always EntityLeashKnot.func_110129_a's (a knot is never
     * saved), whose forceSpawn skips the chunk: a respawned player gets it
     * before any chunk (pickobj_server_respawned) */
    if (v->kind == PK_KNOT) chunk_watched = 1;
    double r = (double)te->tracking_range;

    if (dx >= -r && dx <= r && dz >= -r && dz <= r)
    {
        if (!te->watched && chunk_watched)
        {
            te->watched = true;
            spawn_packets(s, te, v);
        }
    }
    else if (te->watched)
    {
        te->watched = false;
        destroy_later(s, v->id);
    }
}

static struct pickobj_track *trk_find(struct pickobj_state *s, int id)
{
    for (int i = 0; i < s->ntrk; ++i)
        if (s->trk[i].id == id) return &s->trk[i];
    return NULL;
}

/* EntityTracker.trackEntity: the entry at the entity's spawn, and its first
 * sendEventsToPlayers. */
static int player_here(struct serverreplay *sr)
{
    return sr->player != NULL && sr->here == serverreplay_player_dim(sr);
}

static void track(struct serverreplay *sr, struct pickobj_state *s, const struct obj_view *v)
{
    if (trk_find(s, v->id)) return;
    if (s->ntrk >= PICKOBJ_MAX_TRACK) list_full("tracked objects", PICKOBJ_MAX_TRACK);
    struct tracker_entry *te = tracker_add(&s->tr, v->id, TRK_OBJECT, v->x, v->y, v->z, v->yaw, v->pitch, 0.0F);
    if (!te) return;
    entry_params(sr, v->kind, te);
    if (v->kind == PK_DRAGON && v->id == s->dragon_born_id) te->ticks = sr->current_tick - s->dragon_born_tick;
    te->size = tracker_entity_size(v->width);
    if (v->kind == PK_PAINTING || v->kind == PK_FRAME)
    {
        /* the frame's scaled position is its tile's, the painting's the
         * entity's own (the entry constructor floors the position) */
    }
    struct pickobj_track *t = &s->trk[s->ntrk++];
    memset(t, 0, sizeof *t);
    t->id = v->id;
    t->made_before_player = !sr->pass_player_done;
    t->kind = v->kind;
    if (v->dragon) t->health = v->dragon->health;
    te->dim = sr->here;
    if (player_here(sr))
    {
        try_watch(sr, s, te, v);
        return;
    }
    /* another world's tracker: no player to try; an entry made at the
     * replay's first pass is the snapshot's, tracked before tick 0 */
    if (s->first_pass_done && s->first_tick == sr->current_tick)
    {
        te->ticks = (int)sr->w[0].st.total_time;
        for (int j = 0; j < sr->ntrk; ++j)
            if (sr->trk[j].id == te->id)
            {
                te->ticks = sr->trk[j].v[0];
                te->ticks_since_forced_teleport = sr->trk[j].v[1];
                te->last_scaled_x = sr->trk[j].v[2];
                te->last_scaled_y = sr->trk[j].v[3];
                te->last_scaled_z = sr->trk[j].v[4];
                te->last_yaw = sr->trk[j].v[5];
                te->last_pitch = sr->trk[j].v[6];
                te->last_head_yaw = sr->trk[j].v[7];
                te->watch_initialized = sr->trk[j].v[8] != 0;
                te->last_motion_x = sr->trk[j].d[0];
                te->last_motion_y = sr->trk[j].d[1];
                te->last_motion_z = sr->trk[j].d[2];
                te->watch_x = sr->trk[j].d[3];
                te->watch_y = sr->trk[j].d[4];
                te->watch_z = sr->trk[j].d[5];
            }
    }
}

void pickobj_server_spawn(struct serverreplay *sr, int pool, void *p)
{
    struct pickobj_state *s = sr->pickobj;
    if (s != NULL && pool == SR_POOL_DRAGON)
    {
        s->dragon_born_id = ((struct dragon_state *)p)->entity_id;
        s->dragon_born_tick = sr->current_tick;
    }
    if (s == NULL || sr->player == NULL) return;
    struct sr_ent rec = {pool, sr_ent_id(sr, pool, p)};
    struct obj_view v;
    if (!view_of(sr, &rec, &v))
    {
        /* an item or orb the same phase already killed (an explosion's own
         * drop, taken by its blast) was tracked at its spawn all the same:
         * its S0E went out, the next pass's S13 follows */
        ie_ent *ie = pool == 0 ? p : NULL;
        if (pool == 2 && !((struct an_ent *)p)->is_living) ie = ie_get(((struct an_ent *)p)->ieh);
        memset(&v, 0, sizeof v);
        if (ie == NULL || !ie->is_dead || !(v.kind = ie_kind_any(ie))) return;
        ie_view(ie, &v);
    }
    track(sr, s, &v);
}

void pickobj_server_player_left(struct serverreplay *sr)
{
    struct pickobj_state *s = sr->pickobj;
    if (s == NULL) return;
    for (int i = 0; i < s->tr.nentries; ++i)
    {
        if (s->tr.entries[i].watched) destroy_later(s, s->tr.entries[i].id);
        s->tr.entries[i].watched = false;
    }
}

/* World.removeEntity's untrackEntity for a portal traveller: the old
 * world's entry ends (a watching player gets the S13 with its next
 * onUpdate's destroyedItemsNetCache) and the destination's
 * spawnEntityInWorld tracks the same entity anew. */
void pickobj_server_untrack(struct serverreplay *sr, int id)
{
    struct pickobj_state *s = sr->pickobj;
    if (s == NULL) return;
    struct tracker_entry *te = tracker_find(&s->tr, id);
    if (te == NULL) return;
    if (te->watched) destroy_later(s, id);
    tracker_remove(&s->tr, id);
    struct pickobj_track *t = trk_find(s, id);
    if (t != NULL) *t = s->trk[--s->ntrk];
}

void pickobj_server_tick(struct serverreplay *sr)
{
    struct pickobj_state *s = sr->pickobj;
    if (s == NULL || sr->player == NULL) return;
    if (!s->first_pass_done)
    {
        s->first_pass_done = 1;
        s->first_tick = sr->current_tick;
    }
    /* a world the player is not in: its tracker runs with no player */
    int here = player_here(sr);

    /* EntityPlayerMP.onUpdate's destroyedItemsNetCache, at the player's
     * place in this tick's pass */
    for (int i = 0; here && i < s->ndestroyed; ++i) destroy_now(s, s->destroyed[i]);
    if (here) s->ndestroyed = 0;

    int player = -1;
    for (int k = 0; k < sr->d->nents; ++k)
        if (sr->d->ents[k].pool == 3) { player = k; break; }

    /* cleared only as far as the tracked list reaches (seen_upto): the
     * whole 16 KB was cleared every tick (lane/villscale) */
    int *seen ENV_LOCAL = envstack_take(PICKOBJ_MAX_TRACK * sizeof *seen);
    int seen_n = 0;
#define SEEN_UPTO(n) do { while (seen_n < (n)) seen[seen_n++] = 0; } while (0)

    for (int k = 0; k < sr->d->nents; ++k)
    {
        struct obj_view v;
        if (!view_of(sr, &sr->d->ents[k], &v)) continue;
        struct pickobj_track *t = trk_find(s, v.id);
        if (t == NULL)
        {
            track(sr, s, &v);
            t = trk_find(s, v.id);
            if (t == NULL) continue;
        }
        SEEN_UPTO(s->ntrk);
        seen[t - s->trk] = 1;
        t->before_player = player >= 0 && k < player;

        struct tracker_entry *te = tracker_find(&s->tr, v.id);
        if (te == NULL) continue;

        /* sendLocationToAllClients' head: the watch check from the previous
         * scaled position, when the entity moved four blocks since the last */
        /* an entry made this tick ahead of the dead player's place in the
         * pass (a mob's death orb; World.updateEntities removes the player
         * at its place) reached it at addEntityToTracker;
         * removeEntityFromAllTrackingPlayers took it back, and this first
         * update sends the spawn again */
        if (!te->watch_initialized && sr->player_removed_tick == sr->current_tick && t->made_before_player)
            te->watched = false;
        double wx = v.x - te->watch_x, wy = v.y - te->watch_y, wz = v.z - te->watch_z;
        if (!te->watch_initialized || wx * wx + wy * wy + wz * wz > 16.0)
        {
            te->watch_initialized = true;
            te->watch_x = v.x;
            te->watch_y = v.y;
            te->watch_z = v.z;
            if (here) try_watch(sr, s, te, &v);
        }

        te->x = v.x;
        te->y = v.y;
        te->z = v.z;
        te->yaw = v.yaw;
        te->pitch = v.pitch;
        te->head_yaw = 0.0F;
        te->motion_x = v.mx;
        te->motion_y = v.my;
        te->motion_z = v.mz;

        /* the dragon: knockBack's isAirBorne and a health change
         * (DataWatcher.hasChanges) both run the update block */
        int meta = 0;
        if (v.dragon)
        {
            /* EntityLivingBase.onDeath's setEntityState(3): the S19 went to
             * the watchers when the killing damage landed, ahead of this
             * update (whose S1C waits for the entry's third tick) */
            if (v.dragon->health <= 0.0F) pickobj_server_dragon_death(sr, v.id);
            te->is_airborne = v.dragon->is_air_borne;
            if (v.dragon->health != t->health)
            {
                te->dw_dirty = true;
                t->health = v.dragon->health;
                meta = 1;
            }
        }
        if (v.ie != NULL && v.ie->stack_watch_dirty)
        {
            te->dw_dirty = true;
            v.ie->stack_watch_dirty = 0;
        }
        struct tracker_packet *pkt ENV_LOCAL = envstack_take(4 * sizeof *pkt);   /* tracker_entry_tick clears each it fills */
        int n = tracker_entry_tick(&s->tr, te, pkt, 4);
        if (v.dragon) v.dragon->is_air_borne = te->is_airborne;
        for (int i = 0; te->watched && i < n; ++i)
        {
            struct pickobj_packet q;
            memset(&q, 0, sizeof q);
            q.id = v.id;
            switch (pkt[i].kind)
            {
            case TRACKER_PKT_S15: q.kind = PKO_MOVE; q.has_pos = 1; break;
            case TRACKER_PKT_S16: q.kind = PKO_MOVE; q.has_rot = 1; break;
            case TRACKER_PKT_S17: q.kind = PKO_MOVE; q.has_pos = 1; q.has_rot = 1; break;
            case TRACKER_PKT_S18: q.kind = PKO_TELEPORT; q.has_pos = 1; q.has_rot = 1; break;
            case TRACKER_PKT_S12: q.kind = PKO_VELOCITY; break;
            default: continue;
            }
            q.x = pkt[i].x; q.y = pkt[i].y; q.z = pkt[i].z;
            q.yaw = pkt[i].yaw; q.pitch = pkt[i].pitch;
            q.mx = pkt[i].mx; q.my = pkt[i].my; q.mz = pkt[i].mz;
            send(s, &q);
        }
        /* func_111190_b's S1C, after the block's movement packets */
        if (meta && te->watched)
        {
            struct pickobj_packet q;
            memset(&q, 0, sizeof q);
            q.kind = PKO_META;
            q.id = v.id;
            q.health = v.dragon->health;
            send(s, &q);
        }

        /* the tail: a setBeenAttacked entity's velocity */
        int changed = 0;
        if (v.ie) { changed = v.ie->e.velocity_changed || v.ie->velocity_changed; v.ie->e.velocity_changed = 0; v.ie->velocity_changed = 0; }
        if (v.fh) { changed = v.fh->e.velocity_changed; v.fh->e.velocity_changed = 0; }
        if (changed && te->watched)
        {
            struct pickobj_packet q;
            memset(&q, 0, sizeof q);
            q.kind = PKO_VELOCITY;
            q.id = v.id;
            q.mx = (int)(fmin(fmax(v.mx, -3.9), 3.9) * 8000.0);
            q.my = (int)(fmin(fmax(v.my, -3.9), 3.9) * 8000.0);
            q.mz = (int)(fmin(fmax(v.mz, -3.9), 3.9) * 8000.0);
            send(s, &q);
        }
    }

    /* the entities that left the world this tick: ahead of the player, the
     * player's onUpdate already sent the S13; behind it, the next one does */
    SEEN_UPTO(s->ntrk);
#undef SEEN_UPTO
    for (int i = 0; i < s->ntrk; )
    {
        if (seen[i]) { ++i; continue; }
        struct tracker_entry *te = tracker_find(&s->tr, s->trk[i].id);
        /* another world's entries are its own tracker's */
        if (te && te->dim != sr->here) { ++i; continue; }
        if (te && te->watched)
        {
            if (s->trk[i].before_player) destroy_now(s, s->trk[i].id);
            else destroy_later(s, s->trk[i].id);
        }
        tracker_remove(&s->tr, s->trk[i].id);
        s->trk[i] = s->trk[--s->ntrk];
        seen[i] = seen[s->ntrk];
    }

    /* the player's own entry: four blocks since its last check, every entry
     * tries the player again. A player out of the world (dead, or through
     * the exit portal) has none (removeEntityFromAllTrackingPlayers took
     * it, 2 here); the respawn's addEntityToTracker makes a new one and
     * every entry tries the new player */
    const struct server_player *p = sr->player;
    if (p->sv.removed)
    {
        s->player_watch_initialized = 2;
        return;
    }
    if (!here) return;
    double px = p->e.pos_x - s->player_watch_x, py = p->e.pos_y - s->player_watch_y, pz = p->e.pos_z - s->player_watch_z;
    /* the player's own entry: gone while a ghost has it unlisted */
    if (!sr->player_unlisted && (s->player_watch_initialized != 1 || px * px + py * py + pz * pz > 16.0))
    {
        int first = !s->player_watch_initialized;
        s->player_watch_initialized = 1;
        s->player_watch_x = p->e.pos_x;
        s->player_watch_y = p->e.pos_y;
        s->player_watch_z = p->e.pos_z;
        for (int k = 0; !first && k < sr->d->nents; ++k)
        {
            struct obj_view v;
            if (!view_of(sr, &sr->d->ents[k], &v)) continue;
            struct tracker_entry *te = tracker_find(&s->tr, v.id);
            if (te) try_watch(sr, s, te, &v);
        }
    }
}

void pickobj_server_respawned(struct serverreplay *sr)
{
    /* EntityTracker.trackEntity's loop for the new EntityPlayerMP, inside
     * respawnPlayer's spawnEntityInWorld (after the S07 and S08): a knot
     * reaches the client in the respawn's own queue (pfc-knotrespawn-f53).
     * The loop walks trackedEntities, a HashSet; with one knot in range
     * the order does not show */
    struct pickobj_state *s = sr->pickobj;
    if (s == NULL || !player_here(sr)) return;
    for (int k = 0; k < sr->d->nents; ++k)
    {
        struct obj_view v;
        if (!view_of(sr, &sr->d->ents[k], &v) || v.kind != PK_KNOT) continue;
        struct tracker_entry *te = tracker_find(&s->tr, v.id);
        if (te) try_watch(sr, s, te, &v);
    }
}

void pickobj_server_mark_s07(struct serverreplay *sr)
{
    if (sr->pickobj) sr->pickobj->s07_mark = 1 + sr->pickobj->npend;
}

void pickobj_server_dragon_death(struct serverreplay *sr, int id)
{
    struct pickobj_state *s = sr->pickobj;
    struct pickobj_track *t = s ? trk_find(s, id) : NULL;
    if (t == NULL || t->death_sent) return;
    t->death_sent = 1;
    struct tracker_entry *te = tracker_find(&s->tr, id);
    if (te == NULL || !te->watched) return;
    struct pickobj_packet q;
    memset(&q, 0, sizeof q);
    q.kind = PKO_DEATH;
    q.id = id;
    send(s, &q);
}

void pickobj_server_collect(struct serverreplay *sr, int id)
{
    struct pickobj_state *s = sr->pickobj;
    if (s == NULL) return;
    struct tracker_entry *te = tracker_find(&s->tr, id);
    if (te == NULL && player_here(sr))
    {
        /* spawned inside this network phase (a furnace output's orbs) and
         * taken by the same phase's C03 before the phase's end ordered it:
         * spawnEntityInWorld's entry and S11 came first */
        for (int i = 0; i < sr->d->iew.n; ++i)
        {
            ie_ent *ie = ie_ent_at(sr->d->iew.slot[i]);
            struct obj_view v;
            memset(&v, 0, sizeof v);
            if (ie->entity_id != id || !(v.kind = ie_kind_any(ie))) continue;
            ie_view(ie, &v);
            track(sr, s, &v);
            break;
        }
        te = tracker_find(&s->tr, id);
    }
    if (te == NULL || !te->watched) return;
    struct pickobj_packet p;
    memset(&p, 0, sizeof p);
    p.kind = PKO_COLLECT;
    p.id = id;
    send(s, &p);
}

/* EntityTracker.func_85172_a: the chunk just went to the player, and every
 * tracked object standing in it tries the player again, a dead one still in
 * the world too (an item picked up in the last network phase: the S0E, then
 * the S13 when the entity pass removes it) */
void pickobj_chunk_sent(struct serverreplay *sr, int cx, int cz)
{
    struct pickobj_state *s = sr->pickobj;
    if (s == NULL || sr->player == NULL) return;
    for (int k = 0; k < sr->d->nents; ++k)
    {
        struct obj_view v;
        const struct sr_ent *rec = &sr->d->ents[k];
        ie_ent *ie = rec->pool == 0 ? sr_ent_p(sr, rec) : NULL;
        if (ie != NULL && ie->is_dead)
        {
            memset(&v, 0, sizeof v);
            if (!(v.kind = ie_kind_any(ie))) continue;
            ie_view(ie, &v);
        }
        else if (!view_of(sr, rec, &v)) continue;
        if (v.cx != cx || v.cz != cz) continue;
        struct tracker_entry *te = tracker_find(&s->tr, v.id);
        /* a snapshot's dead item never had its entry made (the bind skips
         * the dead): Java's is still there */
        if (te == NULL && v.ie != NULL && v.ie->is_dead)
        {
            track(sr, s, &v);
            te = tracker_find(&s->tr, v.id);
        }
        if (te) try_watch(sr, s, te, &v);
    }
}

/* NetHandlerPlayServer.processUseEntity's INTERACT on a tracked object:
 * the same reach as the attack, then EntityPlayerMP.interactWith's
 * interactFirst. Only EntityItemFrame answers (true); the rest keep
 * Entity's false and nothing happens. */
int pickobj_server_interact(struct serverreplay *sr, int entity_id, int item, int damage, int tag, int *count)
{
    struct obj_view v;
    int found = 0;
    for (int k = 0; k < sr->d->nents && !found; ++k)
        if (view_of(sr, &sr->d->ents[k], &v) && v.id == entity_id) found = 1;
    if (!found) return 0;

    struct server_player *p = sr->player;
    if (p == NULL) return 0;

    struct rt_mop mop;
    int blocked = raytrace_blocks(p->e.world, p->e.pos_x, p->e.pos_y + 1.62F, p->e.pos_z,
                                  v.x, v.y, v.z, 0, 0, 0, &mop);
    double dx = p->e.pos_x - v.x, dy = p->e.pos_y - v.y, dz = p->e.pos_z - v.z;
    if (dx * dx + dy * dy + dz * dz >= (blocked ? 9.0 : 36.0)) return 0;

    if (v.kind != PK_FRAME) return 0;
    fh_frame_interact(v.fh, item, damage, tag, count);
    return 1;
}

int pickobj_server_attack(struct serverreplay *sr, int entity_id)
{
    struct obj_view v;
    int found = 0;
    for (int k = 0; k < sr->d->nents && !found; ++k)
        if (view_of(sr, &sr->d->ents[k], &v) && v.id == entity_id) found = 1;
    if (!found) return 0;

    struct server_player *p = sr->player;
    if (p == NULL) return 1;

    /* NetHandlerPlayServer.processUseEntity: canEntityBeSeen picks the reach */
    struct rt_mop mop;
    int blocked = raytrace_blocks(p->e.world, p->e.pos_x, p->e.pos_y + 1.62F, p->e.pos_z,
                                  v.x, v.y, v.z, 0, 0, 0, &mop);
    double dx = p->e.pos_x - v.x, dy = p->e.pos_y - v.y, dz = p->e.pos_z - v.z;
    if (dx * dx + dy * dy + dz * dz >= (blocked ? 9.0 : 36.0)) return 1;

    /* attackTargetEntityWithCurrentItem */
    if (v.kind == PK_PAINTING || v.kind == PK_FRAME || v.kind == PK_KNOT)
    {
        /* EntityHanging.hitByEntity: attackEntityFrom(player, 0) breaks it,
         * and hitByEntity's true ends the attack */
        serverreplay_break_hanging(sr, v.fh);
        return 1;
    }

    float damage = 1.0F;
    if (p->held_attr_item >= 0 && p->held_attr_item < 4096)
        damage = (float)(1.0 + (double)ITEMS[p->held_attr_item].damage);
    int knockback = p->sprinting ? 1 : 0;
    int landed = 0;

    if (v.kind == PK_CRYSTAL)
    {
        serverreplay_end_crystal_hit(sr, v.crystal);
        landed = 1;
    }
    else
    {
        /* Entity.attackEntityFrom: setBeenAttacked, then false */
        if (v.ie) v.ie->e.velocity_changed = 1;
        if (v.fh) v.fh->e.velocity_changed = 1;
    }

    if (!landed) return 1;

    if (knockback > 0)
    {
        float yaw = p->rotation_yaw * 3.1415927F / 180.0F;
        double ax = (double)(-mh_sin(yaw) * (float)knockback * 0.5F);
        double az = (double)(mh_cos(yaw) * (float)knockback * 0.5F);
        if (v.ie)
        {
            v.ie->e.motion_x += ax;
            v.ie->e.motion_y += 0.1;
            v.ie->e.motion_z += az;
        }
        else if (v.crystal)
        {
            v.crystal->mx += ax;
            v.crystal->my += 0.1;
            v.crystal->mz += az;
        }
        p->e.motion_x *= 0.6;
        p->e.motion_z *= 0.6;
        /* setSprinting(false): the S20's dirt only when a modifier goes */
        if (p->sprinting) p->attr_dirty = 1;
        p->sprinting = 0;
        p->move_speed = player_move_speed(0, p->pot_slow, p->pot_speed);
    }
    if (damage >= 18.0F) surv_add_stat(p, ACH_OVERKILL, 1);
    surv_add_exhaustion(p, 0.3F);
    return 1;
}

/* ------------------------------------------------------------ the client */

static struct pickobj *obj_find(struct pickobj_state *s, int id)
{
    for (int i = 0; i < s->nobjs; ++i)
        if (s->objs[i].id == id) return &s->objs[i];
    return NULL;
}

/* Chunk.addEntity's membership, and updateEntityWithOptionalForce's move to
 * a new list when the chunk coordinates changed (the y compared unclamped
 * against the clamped chunkCoordY). */
static void chunk_add(struct pickobj *o)
{
    o->cx = mh_floor(o->e.pos_x / 16.0);
    o->cz = mh_floor(o->e.pos_z / 16.0);
    int cy = mh_floor(o->e.pos_y / 16.0);
    o->cy = cy < 0 ? 0 : (cy > 15 ? 15 : cy);
    o->seq = ++entity_chunk_stamp;
}

static void chunk_update(struct pickobj *o)
{
    int cx = mh_floor(o->e.pos_x / 16.0), cy = mh_floor(o->e.pos_y / 16.0), cz = mh_floor(o->e.pos_z / 16.0);
    if (cx != o->cx || cy != o->cy || cz != o->cz) chunk_add(o);
}

/* Entity.setPositionAndRotation2: the position, then the lift out of any
 * block box the entity's box (0.03125 in on x and z) meets; r is where it
 * leaves the copy. EntityArrow's override only sets the position. */
static void position2_at(const struct pickobj *o, double x, double y, double z, double r[3])
{
    r[0] = x; r[1] = y; r[2] = z;
    if (o->kind == PK_ARROW) return;
    struct entity tmp = o->e;
    entity_set_position(&tmp, x, y, z);
    struct aabb q = tmp.bounding_box;
    q.min_x += 0.03125; q.max_x -= 0.03125;
    q.min_z += 0.03125; q.max_z -= 0.03125;
    struct collide_list *l COLLIDE_SCRATCH = collide_scratch_begin();
    collide_list_clear(l);
    world_get_colliding_bounding_boxes(tmp.world, q, l);
    if (l->n == 0) return;
    double top = 0.0;
    for (int i = 0; i < l->n; ++i)
        if (l->box[i].max_y > top) top = l->box[i].max_y;
    r[1] = y + (top - tmp.bounding_box.min_y);
}

static void set_position2(struct pickobj *o, double x, double y, double z)
{
    double r[3];
    position2_at(o, x, y, z, r);
    entity_set_position(&o->e, r[0], r[1], r[2]);
}

static void set_position2_packet(struct pickobj *o, const struct pickobj_packet *p, double x, double y, double z)
{
    if (p->pre) entity_set_position(&o->e, p->pre_x, p->pre_y, p->pre_z);
    else set_position2(o, x, y, z);
}

static void remove_copy(struct pickobj_state *s, int id);

static void spawn_copy(struct pickobj_state *s, const struct pickobj_packet *p)
{
    if (s->client_world == NULL) return;
    /* WorldClient.addEntityToWorld: a copy with the id already there (an
     * entry that forgot the dead player spawns again) is removed for the
     * new one */
    remove_copy(s, p->id);
    if (s->nobjs == PICKOBJ_MAX_OBJS) list_full("object copies in the client world", PICKOBJ_MAX_OBJS);
    struct pickobj *o = &s->objs[s->nobjs++];
    memset(o, 0, sizeof *o);
    o->id = p->id;
    o->kind = p->obj_kind;
    entity_init(&o->e, s->client_world);

    /* the Entity constructor's draws, on the client's streams */
    det_rng own;
    memset(&own, 0, sizeof own);
    if (s->det)
    {
        det_next_entity_id_role(s->det, DET_CLIENT);
        own = det_new_random_role(s->det, DET_CLIENT);
        int64_t msb, lsb;
        det_uuid_role(s->det, DET_CLIENT, &msb, &lsb);
    }
    if (o->kind == PK_DRAGON)
    {
        /* new EntityDragon(world): EntityLivingBase's three Math.random
         * initializers, then the seven EntityDragonPart constructors */
        o->dragon = calloc(1, sizeof *o->dragon);
        if (!o->dragon) abort();
        if (s->det)
        {
            for (int i = 0; i < 3; ++i) (void)det_math_random_role(s->det, DET_CLIENT);
            for (int i = 0; i < 7; ++i)
            {
                det_next_entity_id_role(s->det, DET_CLIENT);
                (void)det_new_random_role(s->det, DET_CLIENT);
                int64_t msb, lsb;
                det_uuid_role(s->det, DET_CLIENT, &msb, &lsb);
            }
        }
        struct dragon_state *d = &o->dragon->d;
        d->rand = own;
        d->entity_id = p->id;
        d->ring_index = -1;
        d->healing_crystal_index = -1;
        d->part[0].width = d->part[0].height = 6.0F;
        d->part[1].width = d->part[1].height = 8.0F;
        for (int i = 2; i < 7; ++i) d->part[i].width = d->part[i].height = 4.0F;
        /* handleSpawnMob: the parts' ids follow the packet's */
        for (int i = 0; i < 7; ++i) d->part[i].entity_id = p->id + 1 + i;
        /* setPositionAndRotation: the angles through setRotation's % 360 */
        d->x = (double)p->x / 32.0;
        d->y = (double)p->y / 32.0;
        d->z = (double)p->z / 32.0;
        d->bb_min_x = d->x - 8.0; d->bb_max_x = d->x + 8.0;
        d->bb_min_y = d->y;
        d->bb_min_z = d->z - 8.0; d->bb_max_z = d->z + 8.0;
        d->yaw = fmodf((float)(p->yaw * 360) / 256.0F, 360.0F);
        o->dragon->ip.pitch = fmodf((float)(p->pitch * 360) / 256.0F, 360.0F);
        o->dragon->ip.health = p->health;
        o->server_x = p->x;
        o->server_y = p->y;
        o->server_z = p->z;
        o->e.motion_x = (double)((float)p->mx / 8000.0F);
        o->e.motion_y = (double)((float)p->my / 8000.0F);
        o->e.motion_z = (double)((float)p->mz / 8000.0F);
    }

    float w, h, yo;
    pick_kind_size(o->kind, 0, &w, &h, &yo);
    entity_set_size(&o->e, w, h);
    o->e.y_offset = yo;
    double x = (double)p->x / 32.0, y = (double)p->y / 32.0, z = (double)p->z / 32.0;

    if (o->kind == PK_PAINTING || o->kind == PK_FRAME)
    {
        fh_ent h2;
        memset(&h2, 0, sizeof h2);
        h2.kind = o->kind == PK_PAINTING ? FH_PAINTING : FH_FRAME;
        h2.art = p->art;
        h2.tile_x = p->x; h2.tile_y = p->y; h2.tile_z = p->z;
        fh_set_direction(&h2, p->data);
        o->tile_x = p->x; o->tile_y = p->y; o->tile_z = p->z;
        o->dir = p->data;
        o->art = p->art;
        o->e.pos_x = h2.e.pos_x;
        o->e.pos_y = h2.e.pos_y;
        o->e.pos_z = h2.e.pos_z;
        o->e.bounding_box = h2.e.bounding_box;
        o->server_x = mh_floor(o->e.pos_x * 32.0);
        o->server_y = mh_floor(o->e.pos_y * 32.0);
        o->server_z = mh_floor(o->e.pos_z * 32.0);
    }
    else if (o->kind == PK_KNOT)
    {
        /* new EntityLeashKnot(world, (int)x, (int)y, (int)z): the block's
         * centre; serverPos is the packet's */
        o->tile_x = (int)x; o->tile_y = (int)y; o->tile_z = (int)z;
        entity_set_position(&o->e, (double)o->tile_x + 0.5, (double)o->tile_y + 0.5, (double)o->tile_z + 0.5);
        o->server_x = p->x;
        o->server_y = p->y;
        o->server_z = p->z;
    }
    else if (o->kind == PK_DRAGON)
    {
        entity_set_position(&o->e, o->dragon->d.x, o->dragon->d.y, o->dragon->d.z);
    }
    else
    {
        entity_set_position(&o->e, x, y, z);
        o->server_x = p->x;
        o->server_y = p->y;
        o->server_z = p->z;
    }

    if (o->kind == PK_TNT)
    {
        /* the constructor's fuse angle and kick; the S12 after the spawn
         * replaces the kick */
        if (s->det) (void)det_math_random_role(s->det, DET_CLIENT);
        o->fuse = 80;
        o->e.motion_y = 0.2;
    }
    else if (o->kind == PK_ITEM || o->kind == PK_ORB)
    {
        /* EntityItem's hoverStart, rotationYaw and two motions; EntityXPOrb's
         * rotationYaw and three motions (the S12 after the spawn replaces
         * them) */
        if (s->det)
            for (int i = 0; i < 4; ++i) (void)det_math_random_role(s->det, DET_CLIENT);
    }
    else if (o->kind == PK_FALLING)
    {
        o->block = p->data & 65535;
        o->meta = p->data >> 16;
    }
    o->pitch = (float)(p->pitch * 360) / 256.0F;
    o->yaw = (float)(p->yaw * 360) / 256.0F;
    chunk_add(o);
}

static void remove_copy(struct pickobj_state *s, int id)
{
    for (int i = 0; i < s->nobjs; ++i)
        if (s->objs[i].id == id)
        {
            free(s->objs[i].dragon);
            memmove(&s->objs[i], &s->objs[i + 1], (size_t)(s->nobjs - i - 1) * sizeof *s->objs);
            --s->nobjs;
            return;
        }
}

static void handle(struct pickobj_state *s, const struct pickobj_packet *p)
{
    if (p->kind == PKO_SPAWN) { spawn_copy(s, p); return; }
    if (p->kind == PKO_DESTROY) { remove_copy(s, p->id); return; }
    if (p->kind == PKO_COLLECT)
    {
        /* handleCollectItem: a copy the client holds gets an EntityPickupFX
         * (the Entity constructor's draws, then EntityFX's five Math.random:
         * the pickup effect keeps the shared stream) and leaves the world */
        if (obj_find(s, p->id) == NULL) return;
        if (s->det)
        {
            det_next_entity_id_role(s->det, DET_CLIENT);
            (void)det_new_random_role(s->det, DET_CLIENT);
            int64_t msb, lsb;
            det_uuid_role(s->det, DET_CLIENT, &msb, &lsb);
            for (int i = 0; i < 5; ++i) (void)det_math_random_role(s->det, DET_CLIENT);
        }
        remove_copy(s, p->id);
        return;
    }

    struct pickobj *o = obj_find(s, p->id);
    if (o == NULL) return;

    if (o->dragon != NULL)
    {
        /* NetHandlerPlayClient on an EntityLivingBase: the moves and the
         * teleport go through setPositionAndRotation2's three steps */
        struct dragon_interp *ip = &o->dragon->ip;
        if (p->kind == PKO_META) ip->health = p->health;
        else if (p->kind == PKO_DEATH)
        {
            /* handleHealthUpdate(3): the death sound's pitch on the copy's
             * own Random, then setHealth(0) */
            (void)det_rng_float(&o->dragon->d.rand);
            (void)det_rng_float(&o->dragon->d.rand);
            ip->health = 0.0F;
        }
        else if (p->kind == PKO_VELOCITY)
        {
            o->e.motion_x = (double)p->mx / 8000.0;
            o->e.motion_y = (double)p->my / 8000.0;
            o->e.motion_z = (double)p->mz / 8000.0;
        }
        else if (p->kind == PKO_MOVE || p->kind == PKO_TELEPORT)
        {
            double yoff = 0.0;
            if (p->kind == PKO_MOVE)
            {
                o->server_x += p->x;
                o->server_y += p->y;
                o->server_z += p->z;
            }
            else
            {
                o->server_x = p->x;
                o->server_y = p->y;
                o->server_z = p->z;
                yoff = 0.015625;
            }
            float yaw = p->has_rot ? (float)(p->yaw * 360) / 256.0F : o->dragon->d.yaw;
            float pitch = p->has_rot ? (float)(p->pitch * 360) / 256.0F : ip->pitch;
            ip->new_x = (double)o->server_x / 32.0;
            ip->new_y = (double)o->server_y / 32.0 + yoff;
            ip->new_z = (double)o->server_z / 32.0;
            ip->new_yaw = (double)yaw;
            ip->new_pitch = (double)pitch;
            ip->incr = 3;
        }
        return;
    }

    if (p->kind == PKO_VELOCITY)
    {
        o->e.motion_x = (double)p->mx / 8000.0;
        o->e.motion_y = (double)p->my / 8000.0;
        o->e.motion_z = (double)p->mz / 8000.0;
        return;
    }
    if (p->kind == PKO_MOVE)
    {
        /* handleEntityMovement: every S14 sets the position from serverPos,
         * the look only when the packet has one */
        o->server_x += p->x;
        o->server_y += p->y;
        o->server_z += p->z;
        if (p->has_rot)
        {
            o->yaw = (float)(p->yaw * 360) / 256.0F;
            o->pitch = (float)(p->pitch * 360) / 256.0F;
        }
        set_position2_packet(o, p, (double)o->server_x / 32.0, (double)o->server_y / 32.0, (double)o->server_z / 32.0);
        return;
    }
    if (p->kind == PKO_TELEPORT)
    {
        o->server_x = p->x;
        o->server_y = p->y;
        o->server_z = p->z;
        o->yaw = (float)(p->yaw * 360) / 256.0F;
        o->pitch = (float)(p->pitch * 360) / 256.0F;
        set_position2_packet(o, p, (double)o->server_x / 32.0, (double)o->server_y / 32.0 + 0.015625,
                             (double)o->server_z / 32.0);
    }
}

/* EntityItem's and EntityXPOrb's onUpdate on the client begin with
 * func_145771_j, whose World.func_147461_a over the copy's own box runs each
 * reached block's addCollisionBoxesToList: a brewing stand's leaves the
 * shared Block's bounds at its base, which the server's placement and every
 * ray read (the one effect of the copy's physics modelled here; its place
 * comes from the packets). */
static void copy_brewing_touch(const struct entity *e)
{
    const struct aabb *b = &e->bounding_box;
    int x0 = mh_floor(b->min_x), x1 = mh_floor(b->max_x + 1.0);
    int y0 = mh_floor(b->min_y), y1 = mh_floor(b->max_y + 1.0);
    int z0 = mh_floor(b->min_z), z1 = mh_floor(b->max_z + 1.0);
    for (int x = x0; x < x1; ++x)
        for (int z = z0; z < z1; ++z)
            for (int y = y0 - 1; y < y1; ++y)
                if ((world_get_block(e->world, x, y, z) & 4095) == 117) nw_env->collide.brewing_base = 1;
}

/* One copy's onUpdate on the client world. */
static void update_copy(struct client_player *cp, struct pickobj *o)
{
    struct entity *e = &o->e;

    switch (o->kind)
    {
    case PK_ITEM:
    case PK_ORB:
        if (!nw_env->collide.brewing_base && e->world != NULL) copy_brewing_touch(e);
        break;
    case PK_FALLING:
        if (o->block == 0) { o->dead = 1; break; }
        e->prev_pos_x = e->pos_x;
        e->prev_pos_y = e->pos_y;
        e->prev_pos_z = e->pos_z;
        e->motion_y -= 0.03999999910593033;
        entity_move(e, e->motion_x, e->motion_y, e->motion_z);
        e->motion_x *= 0.9800000190734863;
        e->motion_y *= 0.9800000190734863;
        e->motion_z *= 0.9800000190734863;
        break;
    case PK_TNT:
        e->prev_pos_x = e->pos_x;
        e->prev_pos_y = e->pos_y;
        e->prev_pos_z = e->pos_z;
        e->motion_y -= 0.03999999910593033;
        entity_move(e, e->motion_x, e->motion_y, e->motion_z);
        e->motion_x *= 0.9800000190734863;
        e->motion_y *= 0.9800000190734863;
        e->motion_z *= 0.9800000190734863;
        if (e->on_ground)
        {
            e->motion_x *= 0.699999988079071;
            e->motion_z *= 0.699999988079071;
            e->motion_y *= -0.5;
        }
        if (o->fuse-- <= 0) o->dead = 1;
        break;
    case PK_DRAGON:
    {
        struct dragon_state *d = &o->dragon->d;
        int dying = o->dragon->ip.health <= 0.0f;
        /* onDeathUpdate's hugeexplosion (death ticks 180 to 200) before its
         * move, on the copy's own Random */
        if (dying && d->death_ticks + 1 >= 180 && d->death_ticks + 1 <= 200)
        {
            float v1 = (det_rng_float(&d->rand) - 0.5F) * 8.0F;
            float v2 = (det_rng_float(&d->rand) - 0.5F) * 4.0F;
            float v3 = (det_rng_float(&d->rand) - 0.5F) * 8.0F;
            surv_client_fx_spawn(cp, "hugeexplosion", d->x + (double)v1, d->y + 2.0 + (double)v2, d->z + (double)v3,
                                 0.0, 0.0, 0.0);
        }
        dragon_client_tick(d, &o->dragon->ip);
        /* onLivingUpdate's largeexplode while the health is gone */
        if (dying)
        {
            float v1 = (det_rng_float(&d->rand) - 0.5F) * 8.0F;
            float v2 = (det_rng_float(&d->rand) - 0.5F) * 4.0F;
            float v3 = (det_rng_float(&d->rand) - 0.5F) * 8.0F;
            surv_client_fx_spawn(cp, "largeexplode", d->x + (double)v1, d->y + 2.0 + (double)v2, d->z + (double)v3,
                                 0.0, 0.0, 0.0);
        }
        e->pos_x = d->x;
        e->pos_y = d->y;
        e->pos_z = d->z;
        e->bounding_box = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
        break;
    }
    default:
        /* the crystal and the hanging entities never move on the client */
        break;
    }
}

void pickobj_client_tick(struct client_player *cp)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return;
    s->client_world = cp->e.world;

    for (int i = s->pend_done; i < s->npend; ++i) handle(s, &s->pend[i]);
    s->npend = 0;
    s->pend_done = 0;
    /* a same-dimension respawn's mark (no world change took it) */
    s->s07_mark = 0;

    for (int i = 0; i < s->nobjs; )
    {
        struct pickobj *o = &s->objs[i];
        update_copy(cp, o);
        if (o->dead)
        {
            remove_copy(s, o->id);
            continue;
        }
        chunk_update(o);
        ++i;
    }
}

void pickobj_client_te_tick(struct client_player *cp)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return;
    /* World.updateEntities walks the tile entities after the entities (a
     * rider's place set by its vehicle's update: pfc-ridench-f54) */
    client_te_tick(&s->ctes, s->det, cp->e.world, cp->cw_rand_known ? &cp->cw_rand : NULL, surv_client_fx(cp),
                   cp->e.pos_x, cp->e.pos_y, cp->e.pos_z);
}

void pickobj_client_flush(struct client_player *cp)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return;
    /* a transfer's S07: the packets after it (the old world's spills the
     * setDead made, sent while the player was still in its playerEntities)
     * reach the new client world */
    int k = s->s07_mark ? s->s07_mark - 1 : s->npend;
    for (int i = s->pend_done; i < k; ++i) handle(s, &s->pend[i]);
    memmove(s->pend, s->pend + k, (size_t)(s->npend - k) * sizeof *s->pend);
    s->npend -= k;
    s->pend_done = 0;
}

void pickobj_client_packets_before(struct client_player *cp, uint64_t seq)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return;
    s->client_world = cp->e.world;
    while (s->pend_done < s->npend && s->pend[s->pend_done].seq < seq) handle(s, &s->pend[s->pend_done++]);
}

void pickobj_client_pump_positions(struct client_player *cp)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return;
    enum { SIM_MAX = 64 };
    struct { int id, gone; int x, y, z; } sim[SIM_MAX];
    int nsim = 0;
    int k = s->s07_mark ? s->s07_mark - 1 : s->npend;
    for (int i = s->pend_done; i < k; ++i)
    {
        struct pickobj_packet *p = &s->pend[i];
        p->pre = 0;
        if (p->kind != PKO_MOVE && p->kind != PKO_TELEPORT && p->kind != PKO_SPAWN &&
            p->kind != PKO_DESTROY && p->kind != PKO_COLLECT) continue;
        int j = 0;
        while (j < nsim && sim[j].id != p->id) ++j;
        struct pickobj *o = obj_find(s, p->id);
        if (j == nsim)
        {
            /* (past SIM_MAX copies the rest run their query late) */
            if (nsim == SIM_MAX) continue;
            sim[nsim].id = p->id;
            sim[nsim].gone = o == NULL;
            if (o != NULL) { sim[nsim].x = o->server_x; sim[nsim].y = o->server_y; sim[nsim].z = o->server_z; }
            ++nsim;
        }
        /* a copy this pump spawns or removes is the late handler's: its
         * moves here are not modelled */
        if (p->kind == PKO_SPAWN || p->kind == PKO_DESTROY || p->kind == PKO_COLLECT) { sim[j].gone = 1; continue; }
        if (sim[j].gone || o == NULL || o->dragon != NULL) continue;
        double yoff = 0.0;
        if (p->kind == PKO_MOVE) { sim[j].x += p->x; sim[j].y += p->y; sim[j].z += p->z; }
        else { sim[j].x = p->x; sim[j].y = p->y; sim[j].z = p->z; yoff = 0.015625; }
        double r[3];
        position2_at(o, (double)sim[j].x / 32.0, (double)sim[j].y / 32.0 + yoff, (double)sim[j].z / 32.0, r);
        p->pre = 1;
        p->pre_x = r[0]; p->pre_y = r[1]; p->pre_z = r[2];
    }
}

void pickobj_client_world_change(struct client_player *cp)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return;
    s->nobjs = 0;
    if (!s->s07_mark) s->npend = 0;
    s->s07_mark = 0;
    s->pend_done = 0;
    s->ctes.n = 0;
    s->client_world = cp->e.world;
    for (int i = 0; i < s->tr.nentries; ++i) s->tr.entries[i].watched = false;
    /* a respawn's knot after its S07 (a forceSpawn sends it at once:
     * pickobj_server_respawned) builds in the new world, its entry watching
     * (pfc-knotrespawn-f55) */
    for (int i = 0; i < s->npend; ++i)
    {
        if (s->pend[i].kind != PKO_SPAWN || s->pend[i].obj_kind != PK_KNOT) continue;
        struct tracker_entry *te = tracker_find(&s->tr, s->pend[i].id);
        if (te) te->watched = true;
    }
}

int pickobj_client_kind(struct client_player *cp, int entity_id)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return 0;
    struct pickobj *o = obj_find(s, entity_id);
    return o != NULL ? o->kind : 0;
}

int pickobj_client_attack(struct client_player *cp, int entity_id, int *knock)
{
    struct pickobj_state *s = cp->pickobj;
    *knock = 0;
    if (s == NULL) return 0;
    struct pickobj *o = obj_find(s, entity_id);
    if (o == NULL)
    {
        for (int i = 0; i < s->nobjs && o == NULL; ++i)
            if (s->objs[i].dragon != NULL && entity_id > s->objs[i].id && entity_id <= s->objs[i].id + 7) o = &s->objs[i];
        if (o == NULL) return 0;
        /* attackTargetEntityWithCurrentItem on a part of the client's copy:
         * no crit and no enchantment (the part is no EntityLivingBase),
         * attackEntityFromPart's three target draws on the copy's Random,
         * true; the knockback, then hitEntity on the dragon: the held sword
         * wears 1, a tool 2 (ItemStack.damageItem on the client player's
         * Random), and a stack worn out goes (destroyCurrentEquippedItem) */
        struct dragon_state *d = &o->dragon->d;
        for (int i = 0; i < 3; ++i) (void)det_rng_float(&d->rand);
        *knock = cp->sprinting ? 1 : 0;
        struct surv_stack *held = &cp->sv.inv[cp->hotbar];
        if (held->count > 0 && held->item >= 0 && held->item < 4096)
        {
            int kind = ITEMS[held->item].kind;
            int wear = kind == ITEM_SWORD ? 1 : (kind == ITEM_TOOL ? 2 : 0);
            int level = surv_ench_level(held, 34);
            int negated = 0;
            for (int i = 0; wear > 0 && level > 0 && i < wear; ++i)
                if (det_rng_int_n(&cp->sv.erand, level + 1) > 0) ++negated;
            wear -= negated;
            if (wear > 0 && ITEMS[held->item].max_damage > 0)
            {
                held->damage += wear;
                if (held->damage > ITEMS[held->item].max_damage)
                {
                    --held->count;
                    held->damage = 0;
                    if (held->count <= 0) memset(held, 0, sizeof *held);
                }
            }
        }
        return 1;
    }

    /* the client's attackTargetEntityWithCurrentItem on its copy:
     * attackEntityFrom is true for the crystal (nothing happens on the
     * client), false for the falling block and the TNT; a hanging entity's
     * hitByEntity ends the attack */
    if (o->kind == PK_CRYSTAL) *knock = cp->sprinting ? 1 : 0;
    return 1;
}

/* The client's interactWith on its copy: EntityItemFrame.interactFirst is
 * true on the client too (the insert and the turn are the server's), so the
 * right click stops there; every other object answers false. */
int pickobj_client_interact(struct client_player *cp, int entity_id)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return 0;
    struct pickobj *o = obj_find(s, entity_id);
    return o != NULL && o->kind == PK_FRAME;
}

/* onValidSurface's EntityHanging test over the client's hanging copies. */
static int client_hanging_meets(void *ctx, const struct aabb *box)
{
    const struct pickobj_state *s = ctx;
    for (int i = 0; s != NULL && i < s->nobjs; ++i)
    {
        const struct pickobj *o = &s->objs[i];
        if ((o->kind == PK_PAINTING || o->kind == PK_FRAME || o->kind == PK_KNOT) && !o->dead &&
            aabb_intersects(&o->e.bounding_box, box))
            return 1;
    }
    return 0;
}

/* ItemHangingEntity.onItemUse's client half: the EntityItemFrame or
 * EntityPainting constructor's draws on the client's streams (the
 * painting's art loop tests every art against the client world's blocks
 * and its hanging copies), then onValidSurface over the same. Returns
 * whether it would hang (the client stack shrinks). */
int pickobj_client_hanging_fits(struct client_player *cp, det_state *det, int kind, int x, int y, int z, int dir)
{
    fh_world *fw = &nw_scratch->pickobj_fw;
    struct pickobj_state *s = cp->pickobj;
    memset(fw, 0, sizeof *fw);
    fw->w = s != NULL && s->client_world != NULL ? s->client_world : cp->e.world;
    fw->det = s != NULL && s->det != NULL ? s->det : det;
    fw->role = DET_CLIENT;
    fw->other_hanging = client_hanging_meets;
    fw->other_ctx = s;
    fh_ent *en = kind == FH_PAINTING ? fh_spawn_painting(fw, x, y, z, dir) : fh_spawn_frame(fw, x, y, z, dir);
    if (en == NULL) return 0;
    int ok = fh_valid_surface(fw, en);
    nbt_free(en->tile_entity_data);
    fh_ent_release(en);
    return ok;
}

int pickobj_client_prevents(const struct client_player *cp, const struct aabb *box)
{
    const struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return 0;
    for (int i = 0; i < s->nobjs; ++i)
    {
        const struct pickobj *o = &s->objs[i];
        if (o->dead) continue;
        /* preventEntitySpawning: EntityFallingBlock, EntityTNTPrimed,
         * EntityEnderCrystal set it, the dragon is an EntityLivingBase; the
         * hanging entities and the dragon's parts leave it false */
        if (o->kind != PK_FALLING && o->kind != PK_TNT && o->kind != PK_CRYSTAL && o->kind != PK_DRAGON) continue;
        if (aabb_intersects(&o->e.bounding_box, box)) return 1;
    }
    return 0;
}

int pickobj_client_candidates(struct client_player *cp, struct pick_entry *pe,
                              struct pick_cand *pc, int cap, int base)
{
    struct pickobj_state *s = cp->pickobj;
    if (s == NULL) return 0;
    int n = 0;
    for (int i = 0; i < s->nobjs && n < cap; ++i)
    {
        const struct pickobj *o = &s->objs[i];
        /* the kinds the client only holds: canBeCollidedWith false */
        if (o->kind == PK_ITEM || o->kind == PK_ORB || o->kind == PK_ARROW || o->kind == PK_THROWN || o->kind == PK_EYE)
            continue;
        pe[n].cx = o->cx;
        pe[n].cy = o->cy;
        pe[n].cz = o->cz;
        pe[n].seq = o->seq;
        pe[n].owner = -1;
        pe[n].box = o->e.bounding_box;
        pc[n].box = o->e.bounding_box;
        pc[n].border = pick_kind_border(o->kind);
        /* EntityDragon.canBeCollidedWith is false; its parts' true */
        pc[n].collidable = !o->dead && o->kind != PK_DRAGON;
        pc[n].riding = 0;
        pc[n].id = o->id;
        int owner = base + n;
        ++n;
        if (o->dragon == NULL) continue;
        /* Chunk.getEntitiesWithinAABBForEntity: the parts, in getParts()
         * order, right after the dragon whose box met the search */
        for (int k = 0; k < 7 && n < cap; ++k)
        {
            const struct dragon_part_state *pt = &o->dragon->d.part[k];
            float half = pt->width / 2.0F;
            struct aabb box = aabb_make(pt->x - (double)half, pt->y, pt->z - (double)half,
                                        pt->x + (double)half, pt->y + (double)pt->height, pt->z + (double)half);
            pe[n].cx = o->cx;
            pe[n].cy = o->cy;
            pe[n].cz = o->cz;
            pe[n].seq = o->seq;
            pe[n].owner = owner;
            pe[n].box = box;
            pc[n].box = box;
            pc[n].border = 0.1F;
            pc[n].collidable = 1;
            pc[n].riding = 0;
            pc[n].id = pt->entity_id;
            ++n;
        }
    }
    return n;
}

/* A snapshot client entity's "f" value: raw bits after "d:"/"f:", an int
 * after "i:"; 0 when absent. */
static double cf_d(const struct jval *f, const char *k)
{
    const char *v = json_str(json_get(f, k));
    if (v && !strncmp(v, "str:", 4)) v += 4;
    if (!v || strncmp(v, "d:", 2)) return 0.0;
    uint64_t b = strtoull(v + 2, NULL, 16);
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

static float cf_f(const struct jval *f, const char *k)
{
    const char *v = json_str(json_get(f, k));
    if (!v || strncmp(v, "f:", 2)) return 0.0F;
    uint32_t b = (uint32_t)strtoul(v + 2, NULL, 16);
    float x;
    memcpy(&x, &b, sizeof x);
    return x;
}

static int cf_i(const struct jval *f, const char *k)
{
    const char *v = json_str(json_get(f, k));
    return v && v[0] && v[1] == ':' ? (int)strtol(v + 2, NULL, 10) : 0;
}

/* The snapshot's tracker entry for id had the player (trkrt's watchedBy). */
static int snap_watched(const struct snapshot *snap, int id)
{
    for (int i = 0; snap && i < snap->nents; ++i)
        if (snap->ents[i].id == id) return cf_i(snap->ents[i].trk_rt, "watchedBy") > 0;
    return 0;
}

/* The snapshot client's EntityDragon (clientents.jsonl) over the copy the
 * bind's spawn made: the client had it long before the snapshot, so its
 * position, its setPositionAndRotation2 target and step count, serverPos,
 * the ring buffer and the health are its own, not a fresh spawn's; the
 * parts are what the end of its last onLivingUpdate placed. */
static int restore_client_dragon(struct pickobj *o, const struct snapshot *snap)
{
    const struct jval *ce = NULL;
    for (int i = 0; snap && i < snap->nclient_ents && !ce; ++i)
    {
        int64_t id = -1;
        if (json_int(json_get(snap->client_ents[i], "id"), &id) && id == o->id) ce = snap->client_ents[i];
    }
    const struct jval *f = ce ? json_get(ce, "f") : NULL;
    if (f == NULL) return 0;
    struct dragon_state *d = &o->dragon->d;
    struct dragon_interp *ip = &o->dragon->ip;
    d->x = cf_d(f, "posX"); d->y = cf_d(f, "posY"); d->z = cf_d(f, "posZ");
    d->bb_min_x = d->x - 8.0; d->bb_max_x = d->x + 8.0;
    d->bb_min_y = d->y;
    d->bb_min_z = d->z - 8.0; d->bb_max_z = d->z + 8.0;
    d->yaw = cf_f(f, "rotationYaw");
    d->render_yaw = cf_f(f, "renderYawOffset");
    d->yaw_velocity = cf_f(f, "randomYawVelocity");
    d->hurt_time = cf_i(f, "hurtTime");
    d->death_ticks = cf_i(f, "deathTicks");
    d->ring_index = cf_i(f, "ringBufferIndex");
    const char *rb = json_str(json_get(f, "ringBuffer"));
    if (rb && !strncmp(rb, "str:", 4)) rb += 4;
    if (rb && !strncmp(rb, "dd:", 3))
    {
        const char *q = rb + 3;
        for (int i = 0; i < 64 && *q; ++i)
            for (int j = 0; j < 3; ++j)
            {
                char *end;
                uint64_t b = strtoull(q, &end, 16);
                if (i < 64) memcpy(&d->ring[i][j], &b, sizeof b);
                q = *end ? end + 1 : end;
            }
    }
    ip->pitch = cf_f(f, "rotationPitch");
    ip->new_x = cf_d(f, "newPosX"); ip->new_y = cf_d(f, "newPosY"); ip->new_z = cf_d(f, "newPosZ");
    ip->new_yaw = cf_d(f, "newRotationYaw"); ip->new_pitch = cf_d(f, "newRotationPitch");
    ip->incr = cf_i(f, "newPosRotationIncrements");
    const char *hp = json_str(json_get(json_get(ce, "dw"), "6"));
    if (hp && !strncmp(hp, "f:", 2))
    {
        uint32_t b = (uint32_t)strtoul(hp + 2, NULL, 16);
        memcpy(&ip->health, &b, sizeof b);
    }
    o->server_x = cf_i(f, "serverPosX"); o->server_y = cf_i(f, "serverPosY"); o->server_z = cf_i(f, "serverPosZ");
    o->e.motion_x = cf_d(f, "motionX"); o->e.motion_y = cf_d(f, "motionY"); o->e.motion_z = cf_d(f, "motionZ");
    o->e.pos_x = d->x; o->e.pos_y = d->y; o->e.pos_z = d->z;
    o->e.bounding_box = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
    dragon_update_parts(d);
    return 1;
}

/* The snapshot client's received packets for a copy restored from its own
 * state (player_client.nbt's "pending", in order): the moves, teleports,
 * velocities and destroys its first tick handles before the update. */
/* clientents.jsonl holds the entity: the snapshot's client already has it */
static int snap_client_has(const struct snapshot *snap, int id)
{
    for (int i = 0; i < snap->nclient_ents; ++i)
    {
        int64_t v;
        if (json_int(json_get(snap->client_ents[i], "id"), &v) && v == id) return 1;
    }
    return 0;
}

/* the client's received queue (player_client.nbt's pending) holds the
 * entity's spawn packet: the client builds it at its first tick */
static int snap_pending_spawn(const struct snapshot *snap, int id)
{
    const nbt *pend = nbt_get(snap->player_client, "pending");
    for (int i = 0; i < (pend ? nbt_list_size(pend) : 0); ++i)
    {
        const nbt *pk = nbt_list_get(pend, i);
        const char *cls = nbt_string_value(nbt_get(pk, "cls"));
        const char *key = !cls ? NULL : !strcmp(cls, "S0EPacketSpawnObject") ? "field_149018_a"
                        : !strcmp(cls, "S11PacketSpawnExperienceOrb") ? "field_148992_a"
                        : !strcmp(cls, "S10PacketSpawnPainting") ? "field_148973_a"
                        : !strcmp(cls, "S0FPacketSpawnMob") ? "field_149042_a" : NULL;
        if (key && nbt_get(pk, key) && (int)nbt_int_value(nbt_get(pk, key)) == id) return 1;
    }
    return 0;
}

static void restore_pending(struct pickobj_state *s, const struct snapshot *snap, const int *ids, int nids)
{
    const nbt *pend = snap ? nbt_get(snap->player_client, "pending") : NULL;
    for (int i = 0; i < (pend ? nbt_list_size(pend) : 0); ++i)
    {
        const nbt *pk = nbt_list_get(pend, i);
        const char *cls = nbt_string_value(nbt_get(pk, "cls"));
        if (!cls) continue;
        struct pickobj_packet q;
        memset(&q, 0, sizeof q);
#define PI(k) ((int)nbt_int_value(nbt_get(pk, k)))
        if (!strcmp(cls, "S15PacketEntityRelMove") || !strcmp(cls, "S16PacketEntityLook") ||
            !strcmp(cls, "S17PacketEntityLookMove"))
        {
            q.kind = PKO_MOVE;
            q.has_pos = cls[2] != '6';
            q.has_rot = cls[2] != '5';
            q.id = PI("field_149074_a");
            q.x = PI("field_149072_b"); q.y = PI("field_149073_c"); q.z = PI("field_149070_d");
            q.yaw = PI("field_149071_e"); q.pitch = PI("field_149068_f");
        }
        else if (!strcmp(cls, "S18PacketEntityTeleport"))
        {
            q.kind = PKO_TELEPORT;
            q.has_pos = q.has_rot = 1;
            q.id = PI("field_149458_a");
            q.x = PI("field_149456_b"); q.y = PI("field_149457_c"); q.z = PI("field_149454_d");
            q.yaw = PI("field_149455_e"); q.pitch = PI("field_149453_f");
        }
        else if (!strcmp(cls, "S12PacketEntityVelocity"))
        {
            q.kind = PKO_VELOCITY;
            q.id = PI("field_149417_a");
            q.mx = PI("field_149415_b"); q.my = PI("field_149416_c"); q.mz = PI("field_149414_d");
        }
        else if (!strcmp(cls, "S13PacketDestroyEntities"))
        {
            int n = 0;
            const int *d = nbt_int_array(nbt_get(pk, "field_149100_a"), &n);
            for (int k = 0; d && k < n; ++k)
                for (int j = 0; j < nids; ++j)
                    if (d[k] == ids[j]) { q.kind = PKO_DESTROY; q.id = d[k]; send(s, &q); }
            continue;
        }
        else continue;
#undef PI
        for (int j = 0; j < nids; ++j)
            if (q.id == ids[j]) { send(s, &q); break; }
    }
}

void pickobj_bind(struct serverreplay *sr, struct client_player *cp)
{
    if (sr->pickobj != NULL) return;
    struct pickobj_state *s = calloc(1, sizeof *s);
    if (!s) abort();
    s->pend = fixed_array(PICKOBJ_MAX_PEND, sizeof *s->pend);
    s->cappend = PICKOBJ_MAX_PEND;
    s->objs = fixed_array(PICKOBJ_MAX_OBJS, sizeof *s->objs);
    s->capobjs = PICKOBJ_MAX_OBJS;
    tracker_init(&s->tr);
    s->det = &SR_DET(sr);
    s->client_world = cp->e.world;
    sr->pickobj = s;
    cp->pickobj = s;
    if (sr->player)
    {
        s->player_watch_initialized = 1;
        s->player_watch_x = sr->player->e.pos_x;
        s->player_watch_y = sr->player->e.pos_y;
        s->player_watch_z = sr->player->e.pos_z;
    }

    /* the entities already in the player's world: the client of a snapshot
     * holds the ones in range (the bind runs with the overworld active) */
    int here = sr->here;
    serverreplay_enter(sr, serverreplay_player_dim(sr));
    for (int k = 0; k < sr->d->nents; ++k)
    {
        struct obj_view v;
        if (!view_of(sr, &sr->d->ents[k], &v)) continue;
        track(sr, s, &v);
        /* EntityTrackerEntry.trackingPlayers as the snapshot recorded it
         * (trkrt's watchedBy): an entry keeps its player while in range
         * after the chunk stops being watched, which a fresh entry's
         * tryStartWachingThis cannot tell */
        struct tracker_entry *te = tracker_find(&s->tr, v.id);
        if (v.kind == PK_DRAGON && sr->current_tick == 0 && te && !te->watched && snap_watched(sr->snap, v.id))
        {
            te->watched = true;
            spawn_packets(s, te, &v);
        }
    }
    if (sr->snap && sr->snap->has_client_ents)
    {
        /* the bind's entries against the snapshot's client: a copy it holds
         * is built from the record with no constructor draw (its det state
         * already has them); one whose spawn waits in its queue is built at
         * its first tick; one it never got is an entry the player has not
         * reached yet */
        det_state *d = s->det;
        det_rng seeder = d->seeder[DET_CLIENT], math = d->math[DET_CLIENT];
        int32_t next_id = d->next_id[DET_CLIENT];
        int keep = 0;
        for (int i = 0; i < s->npend; ++i)
        {
            struct pickobj_packet *q = &s->pend[i];
            if (snap_client_has(sr->snap, q->id)) handle(s, q);
            else if (snap_pending_spawn(sr->snap, q->id)) s->pend[keep++] = *q;
            else if (q->kind == PKO_SPAWN)
            {
                struct tracker_entry *te = tracker_find(&s->tr, q->id);
                if (te) te->watched = false;
            }
        }
        d->seeder[DET_CLIENT] = seeder;
        d->math[DET_CLIENT] = math;
        d->next_id[DET_CLIENT] = next_id;
        s->npend = keep;
        /* a spawn in the client's queue for an entity the server has let go
         * since (an item the same tick's pickup took): the client still
         * builds it at its first tick */
        const nbt *pend = nbt_get(sr->snap->player_client, "pending");
        for (int i = 0; i < (pend ? nbt_list_size(pend) : 0) && s->npend < PICKOBJ_MAX_PEND; ++i)
        {
            const nbt *pk = nbt_list_get(pend, i);
            const char *cls = nbt_string_value(nbt_get(pk, "cls"));
            struct pickobj_packet q;
            memset(&q, 0, sizeof q);
            q.kind = PKO_SPAWN;
#define PI(k) ((int)nbt_int_value(nbt_get(pk, k)))
            if (cls && !strcmp(cls, "S0EPacketSpawnObject"))
            {
                int type = PI("field_149019_j");
                q.id = PI("field_149018_a");
                q.x = PI("field_149016_b"); q.y = PI("field_149017_c"); q.z = PI("field_149014_d");
                q.obj_kind = type == 2 ? PK_ITEM : type == 60 ? PK_ARROW : type == 72 ? PK_EYE :
                             type == 70 ? PK_FALLING : type == 50 ? PK_TNT : type == 51 ? PK_CRYSTAL :
                             type == 71 ? PK_FRAME : type == 77 ? PK_KNOT :
                             (type == 61 || type == 62 || type == 65 || type == 73 || type == 75) ? PK_THROWN : 0;
                if (type == 70) q.data = PI("field_149020_k");
            }
            else if (cls && !strcmp(cls, "S11PacketSpawnExperienceOrb"))
            {
                q.id = PI("field_148992_a");
                q.x = PI("field_148990_b"); q.y = PI("field_148991_c"); q.z = PI("field_148988_d");
                q.obj_kind = PK_ORB;
            }
#undef PI
            if (!q.obj_kind || trk_find(s, q.id)) continue;
            s->pend[s->npend++] = q;
        }
    }
    else
    {
        for (int i = 0; i < s->npend; ++i) handle(s, &s->pend[i]);
        s->npend = 0;
    }
    int restored[4], nrestored = 0;
    for (int i = 0; sr->current_tick == 0 && i < s->nobjs; ++i)
        if (s->objs[i].dragon != NULL && restore_client_dragon(&s->objs[i], sr->snap) && nrestored < 4)
            restored[nrestored++] = s->objs[i].id;
    if (nrestored > 0) restore_pending(s, sr->snap, restored, nrestored);

    /* a snapshot's entity was tracked before server tick 0: its entry's
     * pass count is the world's total time (combat_bind does the same) */
    if (sr->current_tick == 0)
        for (int i = 0; i < s->tr.nentries; ++i)
        {
            struct tracker_entry *te = &s->tr.entries[i];
            te->ticks = (int)sr->w[0].st.total_time;
            /* the entry as the snapshot recorded it (trk, trkd): its count,
             * its last scaled position (an item that crept since its entry
             * was made keeps the old one) and its watch check's position */
            for (int j = 0; j < sr->ntrk; ++j)
            {
                const struct sr_trk *k = &sr->trk[j];
                if (k->id != te->id) continue;
                te->ticks = k->v[0];
                te->ticks_since_forced_teleport = k->v[1];
                te->last_scaled_x = k->v[2];
                te->last_scaled_y = k->v[3];
                te->last_scaled_z = k->v[4];
                te->last_yaw = k->v[5];
                te->last_pitch = k->v[6];
                te->last_head_yaw = k->v[7];
                te->watch_initialized = k->v[8] != 0;
                te->last_motion_x = k->d[0];
                te->last_motion_y = k->d[1];
                te->last_motion_z = k->d[2];
                te->watch_x = k->d[3];
                te->watch_y = k->d[4];
                te->watch_z = k->d[5];
            }
        }
    serverreplay_enter(sr, here);
}

const struct pickobj *pickobj_client_objs(const struct client_player *cp, int *n)
{
    const struct pickobj_state *s = cp->pickobj;
    *n = s != NULL ? s->nobjs : 0;
    return s != NULL ? s->objs : NULL;
}

const struct dragon_state *pickobj_client_dragon(const struct pickobj *o, const struct dragon_interp **ip)
{
    if (o->dragon == NULL) return NULL;
    if (ip) *ip = &o->dragon->ip;
    return &o->dragon->d;
}
