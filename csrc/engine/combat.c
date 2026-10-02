#include "combat.h"
#include "env.h"
#include "riding.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "aabb.h"
#include "entity.h"
#include "blocks.h"
#include "chunkload.h"
#include "combatench.h"
#include "dragon.h"
#include "ghasts.h"
#include "items.h"
#include "jmath.h"
#include "living.h"
#include "pick.h"
#include "particles_live.h"
#include "pickobj.h"
#include "player.h"
#include "potion.h"
#include "raytrace.h"
#include "serverreplay.h"
#include "arena.h"
#include "snapshot.h"
#include "tape.h"
#include "survival.h"

/* ItemSaddle */
enum { ITEM_SADDLE_ID = 329 };

/* Minecraft 1.7.10's tracker sends the server entity's scaled position. The
 * client interpolates each S15/S17 over three client entity ticks. The
 * lockstep delivers a server tick's packets to the client connection, and the
 * next runTick handles them in updateController, after its pick: a spawn or a
 * destroy is not under the crosshair until the tick after that. */
/* the most tracker packets one tick queues */
#define COMBAT_MAX_PENDING 8192

/* a packet on the env's stack, its head cleared (tracker.h) */
static struct tracker_packet *pkt_take(void)
{
    struct tracker_packet *p = envstack_take(sizeof *p);
    tracker_packet_clear(p);
    return p;
}

static inline void st_dbl(double *d, double v)
{
    if (memcmp(d, &v, sizeof v)) *d = v;
}

static inline void st_flt(float *d, float v)
{
    if (memcmp(d, &v, sizeof v)) *d = v;
}

static void send(struct combat_state *c, const struct tracker_packet *pkt,
                 float width, float height, int kind)
{
    if (c->npending == COMBAT_MAX_PENDING) abort();
    struct combat_pending *q = &c->pending[c->npending++];
    tracker_packet_copy(&q->pkt, pkt);
    q->width = width;
    q->height = height;
    q->age_width = q->age_height = 0.0F;
    q->kind = kind;
    q->seq = ++nw_env->combat.pkt_seq;
}

/* EntityPlayerMP.func_152339_d: the id waits in destroyedItemsNetCache */
static void destroy_later(struct combat_state *c, int id)
{
    if (c->ndestroyed < (int)(sizeof c->destroyed / sizeof c->destroyed[0])) c->destroyed[c->ndestroyed++] = id;
}

/* the player's onUpdate: the cached ids leave as S13s (in order), before
 * this tick's tracker packets */
static void flush_destroyed(struct combat_state *c)
{
    for (int i = 0; i < c->ndestroyed; )
    {
        struct tracker_packet *pkt ENV_LOCAL = pkt_take();
        pkt->kind = TRACKER_PKT_S13;
        while (i < c->ndestroyed && pkt->num_ids < (int)(sizeof pkt->ids / sizeof pkt->ids[0]))
            pkt->ids[pkt->num_ids++] = c->destroyed[i++];
        send(c, pkt, 0.0F, 0.0F, -1);
    }
    c->ndestroyed = 0;
}

/* the living's DataWatcher values a server mob changes: the flags byte, the
 * air, the health, the arrow count and slot 16 (the spider's climb flag, the
 * pig's saddle, the sheep's fleece) */
static struct tracker_watched watched(const struct living *l)
{
    struct tracker_watched w;
    memset(&w, 0, sizeof w);
    w.health = l->health;
    w.flags0 = l->flags0;
    w.air = l->air;
    w.arrows = l->arrow_count_in_entity;
    w.dw16 = l->data_watcher_16;
    /* EntityAgeable's growing age (12): an ageing child or a parent's
     * cooldown changes it every tick, so its entry updates every tick */
    w.age = l->growing_age;
    if (l->kind == HK_CREEPER)
        w.kind = (l->creeper_state & 255) | l->creeper_powered << 8 | l->creeper_ignited << 9;
    else if (l->kind == HK_ENDERMAN)
        w.kind = (l->enderman_carried_block & 255) | (l->enderman_carrying_data & 255) << 8 | l->enderman_screaming << 16;
    else if (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN)
        w.kind = l->zombie_is_child | l->zombie_is_villager << 1 | l->zombie_is_converting << 2;
    else if (l->kind == HK_WITCH)
        w.kind = l->witch_aggressive;
    w.kind ^= l->potion_liquid_color << 10;
    return w;
}

static void apply_one(struct combat_state *c, const struct combat_pending *q)
{
    clientworld_handle_packet(&c->client, &q->pkt);
    if (q->pkt.kind != TRACKER_PKT_S0F) return;
    struct client_entity *ce = clientworld_get_entity(&c->client, q->pkt.id);
    if (ce)
    {
        ce->width = q->width;
        ce->height = q->height;
        ce->age_width = q->age_width;
        ce->age_height = q->age_height;
        ce->kind = q->kind;
    }
}

/* The kinds' own watched values the client copy renders from: the
 * creeper's state (16, a signed byte), powered (17) and ignited (18); the
 * enderman's carried block (16), its metadata (17) and scream (18); the
 * zombie's child (12), villager (13) and converting (14) flags. */
static int copy_kindw(const struct living *l)
{
    if (l->kind == HK_CREEPER)
        return (l->creeper_state & 255) | l->creeper_powered << 8 | l->creeper_ignited << 9;
    if (l->kind == HK_ENDERMAN)
        return (l->enderman_carried_block & 255) | (l->enderman_carrying_data & 255) << 8 | l->enderman_screaming << 16;
    if (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN)
        return l->zombie_is_child | l->zombie_is_villager << 1 | l->zombie_is_converting << 2;
    return 0;
}

static void apply_pending(struct combat_state *c)
{
    for (int i = 0; i < c->npending; ++i) apply_one(c, &c->pending[i]);
    c->npending = 0;
}

/* EntityTrackerEntry.tryStartWachingThis for the one player: the S0F from
 * the PREVIOUS scaled tracker position's range test. */
/* EntityTracker.addEntityToTracker's branches ahead of IAnimals' 80, 3,
 * true: EntitySquid 64, 3, true and EntityBat 80, 3, false (the bat's client
 * copy moves on its own, seed-1 S7 rows 3561 and 3578; pf-twinrand-g13
 * passes either way since the client bat's own motion) */
static void living_tracker_params(struct tracker_entry *te, const struct living *l)
{
    if (l->kind == AK_SQUID) te->tracking_range = 64;
    if (l->kind == AK_BAT) te->send_velocity_updates = false;
}

static void try_start_watching(struct serverreplay *sr, struct tracker_entry *te,
                               struct living *l)
{
    struct combat_state *c = sr->combat;
    const struct server_player *p = sr->player;
    /* through the exit portal World.removeEntity took the player out of
     * playerEntities too: sendEventsToPlayers reaches no one */
    if (p->conquered_end || p->limbo || p->offworld) return;
    double dx = p->e.pos_x - (double)(te->last_scaled_x / 32);
    double dz = p->e.pos_z - (double)(te->last_scaled_z / 32);
    /* EntityTrackerEntry.isPlayerWatchingThisChunk: the player manager's
     * instance for the entity's chunk holds the player and the chunk has
     * been sent (it is off EntityPlayerMP.loadedChunks) */
    int chunk_watched = cl_player_watching(sr->watch_cl ? sr->watch_cl : sr->d->cl, l->chunk_coord_x, l->chunk_coord_z);
    if (!te->watched && fabs(dx) <= (double)te->tracking_range &&
        fabs(dz) <= (double)te->tracking_range && chunk_watched)
    {
        te->watched = true;
        struct tracker_packet *spawn ENV_LOCAL = pkt_take();
        spawn->kind = TRACKER_PKT_S0F;
        spawn->id = l->entity_id;
        spawn->x = tracker_mul32_size(l->e.pos_x, te->size);
        spawn->y = (int)floor(l->e.pos_y * 32.0);
        spawn->z = tracker_mul32_size(l->e.pos_z, te->size);
        spawn->yaw = (int8_t)(int)(l->rotation_yaw * 256.0F / 360.0F);
        spawn->pitch = (int8_t)(int)(l->rotation_pitch * 256.0F / 360.0F);
        spawn->head_yaw = (int8_t)(int)(l->rotation_yaw_head * 256.0F / 360.0F);
        spawn->mx = (int)(l->e.motion_x * 8000.0);
        spawn->my = (int)(l->e.motion_y * 8000.0);
        spawn->mz = (int)(l->e.motion_z * 8000.0);
        spawn->data = l->data_watcher_16;
        te->sent_dw16 = l->data_watcher_16;
        spawn->flags0 = l->flags0;
        spawn->kindw = copy_kindw(l);
        te->sent_flags0 = spawn->flags0;
        te->sent_kindw = spawn->kindw;
        /* only the server's setChild calls func_146071_k: a baby zombie's
         * client copy keeps the constructor's adult size */
        int adult = l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN;
        if (l->base_width > 0.0F)
        {
            /* an EntityAgeable's copy is built at the constructor's size;
             * its first onLivingUpdate scales it for its age */
            send(c, spawn, l->base_width, l->base_height, l->kind);
            c->pending[c->npending - 1].age_width = l->e.width;
            c->pending[c->npending - 1].age_height = l->e.height;
        }
        else send(c, spawn, adult ? 0.6F : l->e.width, adult ? 1.8F : l->e.height, l->kind);
        /* tryStartWachingThis: a riding entity's new watcher gets its S1B
         * (the client drops it while the vehicle is not there yet) */
        if (lv_get(l->riding_entity) != NULL)
        {
            struct tracker_packet *attach ENV_LOCAL = pkt_take();
            attach->kind = TRACKER_PKT_S1B;
            attach->id = l->entity_id;
            attach->vehicle_id = lv_get(l->riding_entity)->entity_id;
            send(c, attach, 0.0F, 0.0F, -1);
        }
    }
    else if (te->watched && (fabs(dx) > (double)te->tracking_range ||
                              fabs(dz) > (double)te->tracking_range))
    {
        destroy_later(c, l->entity_id);
        te->watched = false;
    }
}

/* The world being ticked is the player's: its EntityTracker's entries try
 * the player. Every other world's tracker runs its entries with no player
 * (their watch positions, scaled positions and counts still move). */
static int combat_player_world(const struct serverreplay *sr)
{
    return sr->player != NULL && sr->here == serverreplay_player_dim(sr);
}

/* A snapshot's entry as it recorded it (trk, trkd): the count of passes,
 * the last scaled position and angles, the watch check's position. */
static void combat_restore_entry(const struct serverreplay *sr, struct tracker_entry *te)
{
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
        return;
    }
    te->ticks = (int)sr->w[0].st.total_time;
}

/* An entry made for an entity of another world at the replay's first tick
 * existed in the snapshot's server all along */
static void combat_entry_made(const struct serverreplay *sr, struct tracker_entry *te)
{
    te->dim = sr->here;
    if (!combat_player_world(sr) && sr->combat->first_quiet_tick == sr->current_tick) combat_restore_entry(sr, te);
}

void combat_track_spawn(struct serverreplay *sr, struct living *l)
{
    if (!sr->combat || !sr->player) return;
    struct combat_state *c = sr->combat;
    if (tracker_find(&c->tracker, l->entity_id)) return;
    struct tracker_entry *te = tracker_add(&c->tracker, l->entity_id, TRK_LIVING,
                                           l->e.pos_x, l->e.pos_y, l->e.pos_z,
                                           l->rotation_yaw, l->rotation_pitch,
                                           l->rotation_yaw_head);
    if (!te) return;
    living_tracker_params(te, l);
    te->size = tracker_entity_size(l->e.width);
    te->dw = watched(l);
    te->sent_flags0 = l->flags0;
    te->sent_kindw = copy_kindw(l);
    combat_entry_made(sr, te);
    if (combat_player_world(sr)) try_start_watching(sr, te, l);
}

/* Java's world order in a tick (the overworld, the Nether, the End) */
static int world_rank(int dim)
{
    return dim == 0 ? 0 : dim == -1 ? 1 : 2;
}

void combat_untrack(struct serverreplay *sr, int id)
{
    if (!sr->combat) return;
    struct combat_state *c = sr->combat;
    struct tracker_entry *te = tracker_find(&c->tracker, id);
    if (te == NULL) return;
    if (te->watched && sr->player != NULL)
    {
        /* the player's onUpdate sends the cache: still ahead of it this tick
         * when the traveller was ahead of the player in the same pass, or
         * the player's world ticks after this one */
        int pdim = serverreplay_player_dim(sr);
        int now = pdim == te->dim ? te->before_player : world_rank(pdim) > world_rank(te->dim);
        if (now) destroy_later(c, id);
        else if (c->ndestroyed_late < (int)(sizeof c->destroyed_late / sizeof c->destroyed_late[0]))
            c->destroyed_late[c->ndestroyed_late++] = id;
    }
    tracker_remove(&c->tracker, id);
}

/* EntityTracker.func_85172_a: the chunk just went to the player in an S26
 * bulk, and every tracked entity standing in it tries the player again (its
 * spawn packet follows the chunk). */
/* EntityTracker.removeEntityFromAllTrackingPlayers for a player ghost's
 * unload (serverreplay.c): every entry lets the player go without a
 * destroy (the ghost object's destroyedItemsNetCache takes the ids, and the
 * live player's world update, which would send them, no longer runs). */
static void fireball_chunk_sent(struct serverreplay *sr, ie_ent *ie);
void combat_chunk_sent(struct serverreplay *sr, int cx, int cz)
{
    pickobj_chunk_sent(sr, cx, cz);
    if (!sr->combat || !sr->player || !combat_player_world(sr)) return;
    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (!en->is_living)
        {
            ie_ent *ie = ie_get(en->ieh);
            if ((ie->kind == IE_LARGE_FIREBALL || ie->kind == IE_SMALL_FIREBALL) && !ie->is_dead &&
                ie->chunk_x == cx && ie->chunk_z == cz)
                fireball_chunk_sent(sr, ie);
            continue;
        }
        struct living *l = lv_get(en->livh);
        if (l->chunk_coord_x != cx || l->chunk_coord_z != cz) continue;
        struct tracker_entry *te = tracker_find(&sr->combat->tracker, l->entity_id);
        if (te) try_start_watching(sr, te, l);
    }
}

static void track_one(struct serverreplay *sr, struct living *l)
{
    struct combat_state *c = sr->combat;
    struct tracker_entry *te = tracker_find(&c->tracker, l->entity_id);
    const struct server_player *p = sr->player;
    if (!p) return;

    if (!te)
    {
        te = tracker_add(&c->tracker, l->entity_id, TRK_LIVING,
                         l->e.pos_x, l->e.pos_y, l->e.pos_z,
                         l->rotation_yaw, l->rotation_pitch, l->rotation_yaw_head);
        if (!te) return;
        living_tracker_params(te, l);
        te->dw = watched(l);
        te->sent_flags0 = l->flags0;
        te->sent_kindw = copy_kindw(l);
        combat_entry_made(sr, te);
    }
    te->size = tracker_entity_size(l->e.width);

    /* EntityTrackerEntry.sendLocationToAllClients calls sendEventsToPlayers
     * when its entity has moved four blocks since the previous range check.
     * tryStartWachingThis checks the PREVIOUS scaled tracker position, before
     * this tick's S14/S18 updates. Keep entries for distant mobs so their
     * tick cadence and that previous position survive a MobMove into view. */
    double wx = l->e.pos_x - te->watch_x;
    double wy = l->e.pos_y - te->watch_y;
    double wz = l->e.pos_z - te->watch_z;
    if (!te->watch_initialized || wx*wx + wy*wy + wz*wz > 16.0)
    {
        te->watch_initialized = true;
        te->watch_x = l->e.pos_x;
        te->watch_y = l->e.pos_y;
        te->watch_z = l->e.pos_z;
        if (combat_player_world(sr)) try_start_watching(sr, te, l);
    }

    /* sendLocationToAllClients: a changed vehicle, or every 60th update
     * while riding, is an S1B to the watchers */
    int vehicle_id = lv_get(l->riding_entity) != NULL ? lv_get(l->riding_entity)->entity_id : 0;
    if (te->attached_id != vehicle_id || (vehicle_id != 0 && te->ticks % 60 == 0))
    {
        te->attached_id = vehicle_id;
        if (te->watched)
        {
            struct tracker_packet *attach ENV_LOCAL = pkt_take();
            attach->kind = TRACKER_PKT_S1B;
            attach->id = l->entity_id;
            attach->vehicle_id = vehicle_id > 0 ? vehicle_id : -1;
            send(c, attach, 0.0F, 0.0F, -1);
        }
    }
    /* the entry's copies stored only when their bits change: a still
     * entity's entry lines stay clean (lane/villscale) */
    bool riding = lv_get(l->riding_entity) != NULL;
    if (te->is_riding != riding) te->is_riding = riding;

    st_dbl(&te->x, l->e.pos_x);
    st_dbl(&te->y, l->e.pos_y);
    st_dbl(&te->z, l->e.pos_z);
    st_flt(&te->yaw, l->rotation_yaw);
    st_flt(&te->pitch, l->rotation_pitch);
    st_flt(&te->head_yaw, l->rotation_yaw_head);
    st_dbl(&te->motion_x, l->e.motion_x);
    st_dbl(&te->motion_y, l->e.motion_y);
    st_dbl(&te->motion_z, l->e.motion_z);
    if (te->is_airborne != (bool)l->is_air_borne) te->is_airborne = l->is_air_borne;
    /* DataWatcher.hasChanges: a watched value changed since the last update */
    struct tracker_watched w = watched(l);
    if (memcmp(&w, &te->dw, sizeof w))
    {
        te->dw_dirty = true;
        te->dw = w;
    }
    else if (l->dw_touched) te->dw_dirty = true;
    if (l->dw_touched) l->dw_touched = 0;
    struct tracker_packet *pkt ENV_LOCAL = envstack_take(4 * sizeof *pkt);   /* tracker_entry_tick clears each it fills */
    int n = tracker_entry_tick(&c->tracker, te, pkt, 4);
    l->is_air_borne = te->is_airborne;
    if (te->watched)
        for (int i = 0; i < n; ++i) send(c, &pkt[i], 0.0F, 0.0F, -1);
    /* func_111190_b: a change marked the entry dirty, so its update block
     * ran this tick and sent the changed values as an S1C */
    if (l->data_watcher_16 != te->sent_dw16 || l->flags0 != te->sent_flags0 || copy_kindw(l) != te->sent_kindw)
    {
        te->sent_dw16 = l->data_watcher_16;
        te->sent_flags0 = l->flags0;
        te->sent_kindw = copy_kindw(l);
        struct tracker_packet *meta ENV_LOCAL = pkt_take();
        meta->kind = TRACKER_PKT_S1C;
        meta->id = l->entity_id;
        meta->data = l->data_watcher_16;
        meta->flags0 = l->flags0;
        meta->kindw = copy_kindw(l);
        if (te->watched) send(c, meta, 0.0F, 0.0F, -1);
    }

    /* EntityLivingBase.onDeath's setEntityState(this, 3), or the health an
     * S0F carried: the client copy runs its own deathTime from this tick's
     * packets */
    if (te->watched && l->health <= 0.0F && !te->sent_dying)
    {
        te->sent_dying = true;
        struct tracker_packet *st ENV_LOCAL = pkt_take();
        st->kind = TRACKER_PKT_S1A;
        st->id = l->entity_id;
        send(c, st, 0.0F, 0.0F, -1);
    }

    /* sendLocationToAllClients' tail: a setBeenAttacked entity's velocity */
    if (l->e.velocity_changed)
    {
        struct tracker_packet *v ENV_LOCAL = pkt_take();
        v->kind = TRACKER_PKT_S12;
        v->id = l->entity_id;
        v->mx = (int)(fmin(fmax(l->e.motion_x, -3.9), 3.9) * 8000.0);
        v->my = (int)(fmin(fmax(l->e.motion_y, -3.9), 3.9) * 8000.0);
        v->mz = (int)(fmin(fmax(l->e.motion_z, -3.9), 3.9) * 8000.0);
        if (te->watched) send(c, v, 0.0F, 0.0F, -1);
        l->e.velocity_changed = 0;
    }
}

static void fireball_spawn(struct serverreplay *sr, struct tracker_entry *te, ie_ent *ie);

/* The fireball's entry: EntityTracker.addEntityToTracker made it when the
 * fireball joined the world; here at the latest. */
static struct tracker_entry *fireball_entry(struct serverreplay *sr, ie_ent *ie)
{
    struct combat_state *c = sr->combat;
    struct tracker_entry *te = tracker_find(&c->tracker, ie->entity_id);
    if (!te)
    {
        int cls = ie->kind == IE_SMALL_FIREBALL ? TRK_SMALL_FIREBALL : TRK_LARGE_FIREBALL;
        te = tracker_add(&c->tracker, ie->entity_id, cls,
                         ie->e.pos_x, ie->e.pos_y, ie->e.pos_z,
                         ie->rotation_yaw, ie->rotation_pitch, ie->rotation_yaw);
        if (!te) return NULL;
        te->is_fireball = true;
        /* Entity.setSize's myEntitySize: EntityFireball's setSize(1.0F, 1.0F)
         * is SIZE_4, EntitySmallFireball's (0.3125F) SIZE_1 */
        te->size = tracker_entity_size(ie->kind == IE_SMALL_FIREBALL ? 0.3125F : 1.0F);
        te->s0e_data = ie->shooter ? lv_get(ie->shooter)->entity_id : 0;
        te->s0e_accel_x = ie->accel_x;
        te->s0e_accel_y = ie->accel_y;
        te->s0e_accel_z = ie->accel_z;
        combat_entry_made(sr, te);
    }
    return te;
}

/* The non-living fireball of the an_world's own ie pool, at EntityTracker's
 * EntityFireball branch: range 64, frequency 10, sendVelocityUpdates false.
 * The first watch sends an S0E (type 63, data = the shooter's id, the motion
 * fields the acceleration *8000); the tick runs tracker_entry_tick, whose
 * movement block sends the same S14-family packets a living's would, rounded
 * by the fireball's own myEntitySize (fireball_entry). */
static void track_fireball(struct serverreplay *sr, ie_ent *ie)
{
    struct combat_state *c = sr->combat;
    struct server_player *p = sr->player;
    if (!p) return;
    struct tracker_entry *te = fireball_entry(sr, ie);
    if (!te) return;

    double wx = ie->e.pos_x - te->watch_x;
    double wy = ie->e.pos_y - te->watch_y;
    double wz = ie->e.pos_z - te->watch_z;
    if (!te->watch_initialized || wx*wx + wy*wy + wz*wz > 16.0)
    {
        te->watch_initialized = true;
        te->watch_x = ie->e.pos_x;
        te->watch_y = ie->e.pos_y;
        te->watch_z = ie->e.pos_z;
        double dx = p->e.pos_x - (double)(te->last_scaled_x / 32);
        double dz = p->e.pos_z - (double)(te->last_scaled_z / 32);
        int pcx = (int)floor(p->e.pos_x / 16.0);
        int pcz = (int)floor(p->e.pos_z / 16.0);
        int chunk_watched = combat_player_world(sr) && !p->conquered_end && !p->limbo && !p->offworld && abs(ie->chunk_x - pcx) <= sr->view &&
                            abs(ie->chunk_z - pcz) <= sr->view;
        if (!te->watched && fabs(dx) <= (double)te->tracking_range &&
            fabs(dz) <= (double)te->tracking_range && chunk_watched)
            fireball_spawn(sr, te, ie);
        else if (te->watched && (fabs(dx) > (double)te->tracking_range ||
                                  fabs(dz) > (double)te->tracking_range))
        {
            destroy_later(c, ie->entity_id);
            te->watched = false;
        }
    }

    te->x = ie->e.pos_x;
    te->y = ie->e.pos_y;
    te->z = ie->e.pos_z;
    te->yaw = ie->rotation_yaw;
    te->pitch = ie->rotation_pitch;
    te->head_yaw = ie->rotation_yaw;
    te->motion_x = ie->e.motion_x;
    te->motion_y = ie->e.motion_y;
    te->motion_z = ie->e.motion_z;
    /* EntityFireball's burning flag turns on at the fireball's second
     * update (the first setFire(1) comes after that tick's flag write):
     * the DataWatcher change forces the update block once */
    if (ie->ticks_in_air == 2) te->dw_dirty = true;
    /* the tail's velocityChanged: setBeenAttacked from a punch, and from
     * Entity.onEntityUpdate's onFire damage (item_entity.c's base update:
     * each tick from the second while fire is 20, each twentieth after
     * lava's 300), which EntityFireball.attackEntityFrom answers with
     * setBeenAttacked before it looks at the source; EntitySmallFireball's
     * attackEntityFrom sets nothing */
    if (ie->e.velocity_changed || ie->velocity_changed)
        te->velocity_changed = true;
    ie->e.velocity_changed = 0;
    ie->velocity_changed = 0;
    struct tracker_packet *pkt ENV_LOCAL = envstack_take(4 * sizeof *pkt);   /* tracker_entry_tick clears each it fills */
    int n = tracker_entry_tick(&c->tracker, te, pkt, 4);
    if (te->watched)
        for (int i = 0; i < n; ++i) send(c, &pkt[i], 0.0F, 0.0F, -1);
}

/* The first watch of a fireball's entry: te is watched from here on. */
static void fireball_spawn(struct serverreplay *sr, struct tracker_entry *te, ie_ent *ie)
{
    struct combat_state *c = sr->combat;
    te->watched = true;
    /* EntityTrackerEntry.func_151260_c's fireball branch: the S0E's
     * fields are the entity's own, its data the shooter's id (0
     * when none), and the motion fields the ACCELERATION *8000,
     * which handleSpawnObject's data > 0 setVelocity then hands the
     * client entity as its motion (the packet's only velocity the
     * fireball ever receives; the S12 on velocityChanged later
     * overwrites it). No S12 follows the spawn: the entry's
     * sendVelocityUpdates is false. */
    struct tracker_packet *spawn ENV_LOCAL = pkt_take();
    spawn->kind = TRACKER_PKT_S0E;
    spawn->type = ie->kind == IE_SMALL_FIREBALL ? 64 : 63;
    spawn->data = te->s0e_data;
    spawn->id = ie->entity_id;
    spawn->x = (int)floor(ie->e.pos_x * 32.0);
    spawn->y = (int)floor(ie->e.pos_y * 32.0);
    spawn->z = (int)floor(ie->e.pos_z * 32.0);
    /* the packet's rotation order: pitch first, then yaw */
    spawn->yaw = (int8_t)(int)(ie->rotation_pitch * 256.0F / 360.0F);
    spawn->pitch = (int8_t)(int)(ie->rotation_yaw * 256.0F / 360.0F);
    spawn->mx = (int)(te->s0e_accel_x * 8000.0);
    spawn->my = (int)(te->s0e_accel_y * 8000.0);
    spawn->mz = (int)(te->s0e_accel_z * 8000.0);
    if (ie->kind == IE_SMALL_FIREBALL) send(c, spawn, 0.3125F, 0.3125F, -1);
    else send(c, spawn, 1.0F, 1.0F, -1);
}

/* EntityTracker.func_85172_a for a fireball standing in the chunk just sent:
 * tryStartWachingThis (the range from the entry's last scaled position, the
 * chunk now off the player's send queue). A fireball read back with its
 * chunk joined the world before this send, so its entry and its S0E come
 * here, ahead of its first update (which may already end it). */
static void fireball_chunk_sent(struct serverreplay *sr, ie_ent *ie)
{
    const struct server_player *p = sr->player;
    if (p->conquered_end || p->limbo || p->offworld) return;
    struct tracker_entry *te = fireball_entry(sr, ie);
    if (!te || te->watched) return;
    double dx = p->e.pos_x - (double)(te->last_scaled_x / 32);
    double dz = p->e.pos_z - (double)(te->last_scaled_z / 32);
    if (fabs(dx) <= (double)te->tracking_range && fabs(dz) <= (double)te->tracking_range &&
        cl_player_watching(sr->watch_cl ? sr->watch_cl : sr->d->cl, ie->chunk_x, ie->chunk_z))
        fireball_spawn(sr, te, ie);
}

/* ------------------------------------------------ a mid-run snapshot */

static const char *rt_str(const struct jval *o, const char *key)
{
    const char *v = json_str(json_get(o, key));
    if (v && !strncmp(v, "str:", 4)) v += 4;
    return v;
}

static int rt_i(const struct jval *o, const char *key, int dflt)
{
    int64_t v;
    return json_int(json_get(o, key), &v) ? (int)v : dflt;
}

static double rt_d(const struct jval *o, const char *key, double dflt)
{
    uint64_t b;
    if (!json_double(json_get(o, key), &b)) return dflt;
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

static float rt_f(const struct jval *o, const char *key, float dflt)
{
    uint32_t b;
    if (!json_float(json_get(o, key), &b)) return dflt;
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

/* "e:<id>" as an id, 0 for null */
static int rt_ref(const struct jval *o, const char *key)
{
    const char *v = rt_str(o, key);
    if (!v || strncmp(v, "e:", 2)) return 0;
    int id = (int)strtol(v + 2, NULL, 10);
    return id < 0 ? 0 : id;
}

/* The tracker entries and the client's entities as a mid-run snapshot
 * recorded them (entities.jsonl's trk, clientents.jsonl), in place of the
 * join model combat_bind builds: EntityTrackerEntry's last sent positions,
 * angles and motion, its counter and its range check, and the client
 * world's own copies, interpolation included, in the client's chunk-list
 * order; then the entity packets the client had not pumped yet
 * (player_client.nbt's pending), which its first tick handles. */
static void combat_restore_snapshot(struct serverreplay *sr, struct combat_state *c)
{
    const struct snapshot *snap = sr->snap;
    /* the tracker the player is in: its world's livings get their entries
     * now (the bind ran in the overworld), then the recorded state */
    int here = sr->here;
    serverreplay_enter(sr, serverreplay_player_dim(sr));
    for (int i = 0; i < sr->d->anw.n; ++i)
        if (an_ent_at(sr->d->anw.slot[i])->used && an_ent_at(sr->d->anw.slot[i])->is_living && !tracker_find(&c->tracker, lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->entity_id))
            track_one(sr, lv_get(an_ent_at(sr->d->anw.slot[i])->livh));
    /* and the fireballs', so the recorded watch holds (a watched one is not
     * spawned again: its S0E is the client's or in its queue) */
    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (!en->used || en->is_living) continue;
        ie_ent *ie = ie_get(en->ieh);
        if (ie->kind == IE_LARGE_FIREBALL || ie->kind == IE_SMALL_FIREBALL) fireball_entry(sr, ie);
    }
    c->npending = 0;

    for (int i = 0; i < snap->nents; ++i)
    {
        const struct snap_entity *se = &snap->ents[i];
        if (!se->trk_rt || se->player) continue;
        struct tracker_entry *te = tracker_find(&c->tracker, se->id);
        if (!te) continue;
        const struct jval *t = se->trk_rt;
        te->last_scaled_x = rt_i(t, "lastScaledXPosition", te->last_scaled_x);
        te->last_scaled_y = rt_i(t, "lastScaledYPosition", te->last_scaled_y);
        te->last_scaled_z = rt_i(t, "lastScaledZPosition", te->last_scaled_z);
        te->last_yaw = rt_i(t, "lastYaw", te->last_yaw);
        te->last_pitch = rt_i(t, "lastPitch", te->last_pitch);
        te->last_head_yaw = rt_i(t, "lastHeadMotion", te->last_head_yaw);
        te->last_motion_x = rt_d(t, "motionX", te->last_motion_x);
        te->last_motion_y = rt_d(t, "motionY", te->last_motion_y);
        te->last_motion_z = rt_d(t, "motionZ", te->last_motion_z);
        te->ticks = rt_i(t, "ticks", te->ticks);
        te->ticks_since_forced_teleport = rt_i(t, "ticksSinceLastForcedTeleport", te->ticks_since_forced_teleport);
        te->send_velocity_updates = rt_i(t, "sendVelocityUpdates", te->send_velocity_updates) != 0;
        te->riding_entity = rt_i(t, "ridingEntity", te->riding_entity) != 0;
        te->attached_id = rt_ref(t, "field_85178_v");
        te->watch_initialized = rt_i(t, "isDataInitialized", te->watch_initialized) != 0;
        te->watch_x = rt_d(t, "posX", te->watch_x);
        te->watch_y = rt_d(t, "posY", te->watch_y);
        te->watch_z = rt_d(t, "posZ", te->watch_z);
        te->watched = rt_i(t, "watchedBy", te->watched) > 0;
        /* DataWatcher.hasChanges: a change the last tracker pass has not
         * sent (made in the network tick after it) */
        {
            const struct jval *dw = se->rt ? json_get(se->rt, "dw") : NULL;
            te->dw_dirty = dw && rt_i(dw, "changed", 0) != 0;
        }
    }

    struct clientworld *cw = &c->client;
    cw->nents = 0;
    uint64_t base = entity_chunk_stamp + 1, top = base;
    for (int i = 0; i < snap->nclient_ents; ++i)
    {
        const struct jval *o = snap->client_ents[i];
        const char *cls = json_str(json_get(o, "class"));
        const struct jval *f = json_get(o, "f");
        int64_t id;
        if (!cls || !f || !json_int(json_get(o, "id"), &id)) continue;
        /* the chunk-list stamps continue past every copy the snapshot holds */
        if (base + (uint64_t)rt_i(o, "cidx", i) > top) top = base + (uint64_t)rt_i(o, "cidx", i);
        /* the copies of what this tracker tracks (the livings, the dragon,
         * the fireballs); the items, orbs and the rest are other trackers' */
        int kind = sr_living_kind(cls);
        int fireball = !strcmp(cls, "EntityLargeFireball") || !strcmp(cls, "EntitySmallFireball");
        if (kind < 0 && strcmp(cls, "EntityDragon") && !fireball) continue;
        if (cw->nents >= CW_MAX_ENTITIES) list_full("entities in the client world", CW_MAX_ENTITIES);
        struct client_entity *e = &cw->ents[cw->nents++];
        memset(e, 0, sizeof *e);
        e->id = (int)id;
        e->kind = kind;
        e->is_living = e->kind >= 0 || !strcmp(cls, "EntityDragon");
        e->is_fireball = fireball;
        e->is_large_fireball = !strcmp(cls, "EntityLargeFireball");
        e->is_dead = rt_i(f, "isDead", 0) != 0;
        e->x = rt_d(f, "posX", 0.0);
        e->y = rt_d(f, "posY", 0.0);
        e->z = rt_d(f, "posZ", 0.0);
        e->prev_x = rt_d(f, "prevPosX", e->x);
        e->prev_y = rt_d(f, "prevPosY", e->y);
        e->prev_z = rt_d(f, "prevPosZ", e->z);
        e->prev_yaw = rt_f(f, "prevRotationYaw", 0.0F);
        e->prev_pitch = rt_f(f, "prevRotationPitch", 0.0F);
        e->width = rt_f(f, "width", 0.0F);
        e->height = rt_f(f, "height", 0.0F);
        e->motion_x = rt_d(f, "motionX", 0.0);
        e->motion_y = rt_d(f, "motionY", 0.0);
        e->motion_z = rt_d(f, "motionZ", 0.0);
        e->accel_x = rt_d(f, "accelerationX", 0.0);
        e->accel_y = rt_d(f, "accelerationY", 0.0);
        e->accel_z = rt_d(f, "accelerationZ", 0.0);
        e->fall_distance = rt_f(f, "fallDistance", 0.0F);
        e->y_size = rt_f(f, "ySize", 0.0F);
        e->dist_walked = rt_f(f, "distanceWalkedModified", 0.0F);
        e->dist_walked_step = rt_f(f, "distanceWalkedOnStepModified", 0.0F);
        e->next_step = rt_i(f, "nextStepDistance", 1);
        e->on_ground = rt_i(f, "onGround", 0) != 0;
        e->in_water = rt_i(f, "inWater", 0) != 0;
        e->yaw = rt_f(f, "rotationYaw", 0.0F);
        e->pitch = rt_f(f, "rotationPitch", 0.0F);
        e->head_yaw = rt_f(f, "rotationYawHead", 0.0F);
        e->render_yaw_offset = rt_f(f, "renderYawOffset", 0.0F);
        e->riding_id = rt_ref(f, "ridingEntity");
        e->ridden_by_id = rt_ref(f, "riddenByEntity");
        e->server_pos_x = rt_i(f, "serverPosX", 0);
        e->server_pos_y = rt_i(f, "serverPosY", 0);
        e->server_pos_z = rt_i(f, "serverPosZ", 0);
        e->new_pos_rotation_increments = rt_i(f, "newPosRotationIncrements", 0);
        e->new_pos_x = rt_d(f, "newPosX", 0.0);
        e->new_pos_y = rt_d(f, "newPosY", 0.0);
        e->new_pos_z = rt_d(f, "newPosZ", 0.0);
        e->new_rotation_yaw = rt_d(f, "newRotationYaw", 0.0);
        e->new_rotation_pitch = rt_d(f, "newRotationPitch", 0.0);
        e->chunk_x = rt_i(f, "chunkCoordX", 0);
        int cy = rt_i(f, "chunkCoordY", 0);
        e->chunk_y = cy < 0 ? 0 : cy > 15 ? 15 : cy;
        e->chunk_z = rt_i(f, "chunkCoordZ", 0);
        /* the slice's list order (Chunk.entityLists) as the snapshot saw it */
        e->chunk_seq = base + (uint64_t)rt_i(o, "cidx", i);
        if (e->chunk_seq > top) top = e->chunk_seq;
        e->no_chunk = rt_i(f, "addedToChunk", 1) == 0;
        const struct jval *dw = json_get(o, "dw");
        e->dw16 = rt_i(dw, "16", 0);
        e->dying = e->is_living && rt_f(dw, "6", 1.0F) <= 0.0F;
        e->death_time = rt_i(f, "deathTime", 0);
        e->flags0 = rt_i(dw, "0", 0);
        if (!strcmp(cls, "EntityCreeper"))
        {
            e->creeper_since = rt_i(f, "timeSinceIgnited", 0);
            e->creeper_last = rt_i(f, "lastActiveTime", 0);
        }
        if (!strcmp(cls, "EntityCreeper"))
            e->kindw = (rt_i(dw, "16", 0) & 255) | rt_i(dw, "17", 0) << 8 | rt_i(dw, "18", 0) << 9;
        else if (!strcmp(cls, "EntityEnderman"))
            e->kindw = (rt_i(dw, "16", 0) & 255) | (rt_i(dw, "17", 0) & 255) << 8 | rt_i(dw, "18", 0) << 16;
        else if (!strcmp(cls, "EntityZombie") || !strcmp(cls, "EntityPigZombie"))
            e->kindw = rt_i(dw, "12", 0) | rt_i(dw, "13", 0) << 1 | rt_i(dw, "14", 0) << 2;
        const struct jval *body = json_get(o, "body");
        if (body)
        {
            e->body_yaw = rt_f(body, "field_75667_c", 0.0F);
            e->body_counter = rt_i(body, "field_75666_b", 0);
        }
    }
    clientworld_stamp_at_least(top + 1);

    /* the client player's own vehicle is its riding_id (S1B), kept apart */
    c->npending = 0;
    const nbt *pend = nbt_get(snap->player_client, "pending");
    int self = sr->player ? sr->player_entity_id : -1;
    for (int i = 0; i < (pend ? nbt_list_size(pend) : 0); ++i)
    {
        const nbt *pk = nbt_list_get(pend, i);
        const char *cls = nbt_string_value(nbt_get(pk, "cls"));
        if (!cls) continue;
        struct tracker_packet q;
        memset(&q, 0, sizeof q);
        float width = 0.0F, height = 0.0F;
        int kind = -1;
#define PI(k) ((int)nbt_int_value(nbt_get(pk, k)))
        if (!strcmp(cls, "S15PacketEntityRelMove") || !strcmp(cls, "S16PacketEntityLook") ||
            !strcmp(cls, "S17PacketEntityLookMove"))
        {
            q.kind = cls[2] == '5' ? TRACKER_PKT_S15 : cls[2] == '6' ? TRACKER_PKT_S16 : TRACKER_PKT_S17;
            q.id = PI("field_149074_a");
            q.x = PI("field_149072_b");
            q.y = PI("field_149073_c");
            q.z = PI("field_149070_d");
            q.yaw = PI("field_149071_e");
            q.pitch = PI("field_149068_f");
        }
        else if (!strcmp(cls, "S18PacketEntityTeleport"))
        {
            q.kind = TRACKER_PKT_S18;
            q.id = PI("field_149458_a");
            q.x = PI("field_149456_b");
            q.y = PI("field_149457_c");
            q.z = PI("field_149454_d");
            q.yaw = PI("field_149455_e");
            q.pitch = PI("field_149453_f");
        }
        else if (!strcmp(cls, "S19PacketEntityHeadLook"))
        {
            q.kind = TRACKER_PKT_S19;
            q.id = PI("field_149384_a");
            q.head_yaw = PI("field_149383_b");
        }
        else if (!strcmp(cls, "S19PacketEntityStatus"))
        {
            if (PI("field_149163_b") != 3) continue;
            q.kind = TRACKER_PKT_S1A;
            q.id = PI("field_149164_a");
        }
        else if (!strcmp(cls, "S12PacketEntityVelocity"))
        {
            q.kind = TRACKER_PKT_S12;
            q.id = PI("field_149417_a");
            q.mx = PI("field_149415_b");
            q.my = PI("field_149416_c");
            q.mz = PI("field_149414_d");
        }
        else if (!strcmp(cls, "S1CPacketEntityMetadata"))
        {
            if (nbt_get(pk, "dw16") == NULL) continue;
            q.kind = TRACKER_PKT_S1C;
            q.id = PI("field_149379_a");
            q.data = PI("dw16");
            for (int k = 0; k < sr->d->anw.n; ++k)
            {
                struct an_ent *en = an_ent_at(sr->d->anw.slot[k]);
                if (en->used && en->is_living && en->livh && lv_get(en->livh)->entity_id == q.id)
                {
                    q.flags0 = lv_get(en->livh)->flags0;
                    q.kindw = copy_kindw(lv_get(en->livh));
                }
            }
        }
        else if (!strcmp(cls, "S1BPacketEntityAttach"))
        {
            q.kind = TRACKER_PKT_S1B;
            q.leash = PI("field_149408_a");
            q.id = PI("field_149406_b");
            q.vehicle_id = PI("field_149407_c");
        }
        else if (!strcmp(cls, "S13PacketDestroyEntities"))
        {
            int n = 0;
            const int *ids = nbt_int_array(nbt_get(pk, "field_149100_a"), &n);
            q.kind = TRACKER_PKT_S13;
            if (n > 127) list_full("ids in one S13 packet", 127);
            for (int k = 0; ids && k < n; ++k) q.ids[q.num_ids++] = ids[k];
        }
        else if (!strcmp(cls, "S0DPacketCollectItem"))
        {
            q.kind = TRACKER_PKT_S0D;
            q.id = PI("field_149357_a");
        }
        else if (!strcmp(cls, "S0EPacketSpawnObject"))
        {
            /* the fireballs' (63, 64) are this tracker's; the other objects
             * are the object mirror's (pickobj.c). Fields as fireball_spawn
             * fills them: the packet's pitch first, then its yaw */
            int type = PI("field_149019_j");
            if (type != 63 && type != 64) continue;
            q.kind = TRACKER_PKT_S0E;
            q.type = type;
            q.id = PI("field_149018_a");
            q.x = PI("field_149016_b");
            q.y = PI("field_149017_c");
            q.z = PI("field_149014_d");
            q.yaw = PI("field_149021_h");
            q.pitch = PI("field_149022_i");
            q.mx = PI("field_149015_e");
            q.my = PI("field_149012_f");
            q.mz = PI("field_149013_g");
            q.data = PI("field_149020_k");
            width = height = type == 64 ? 0.3125F : 1.0F;
        }
        else if (!strcmp(cls, "S0FPacketSpawnMob"))
        {
            /* the dragon's (63) is the object mirror's (pickobj.c) */
            if (PI("field_149040_b") == 63) continue;
            q.kind = TRACKER_PKT_S0F;
            q.id = PI("field_149042_a");
            q.type = PI("field_149040_b");
            q.x = PI("field_149041_c");
            q.y = PI("field_149038_d");
            q.z = PI("field_149039_e");
            q.mx = PI("field_149036_f");
            q.my = PI("field_149037_g");
            q.mz = PI("field_149047_h");
            q.yaw = PI("field_149048_i");
            q.pitch = PI("field_149045_j");
            q.head_yaw = PI("field_149046_k");
            q.data = nbt_get(pk, "dw16") ? PI("dw16") : 0;
            /* the client model's size and kind: the server living's */
            for (int k = 0; k < sr->d->anw.n; ++k)
            {
                struct an_ent *en = an_ent_at(sr->d->anw.slot[k]);
                if (en->used && en->is_living && en->livh && lv_get(en->livh)->entity_id == q.id)
                {
                    kind = lv_get(en->livh)->kind;
                    /* a baby zombie's client copy keeps the adult size, as
                     * at a live spawn (try_start_watching) */
                    int adult = kind == HK_ZOMBIE || kind == HK_PIGMAN;
                    width = adult ? 0.6F : lv_get(en->livh)->e.width;
                    height = adult ? 1.8F : lv_get(en->livh)->e.height;
                    /* the recorded packet keeps watcher 16 only: the flag
                     * byte and the kind's values are the living's, which a
                     * tick-old spawn still holds */
                    q.flags0 = lv_get(en->livh)->flags0;
                    q.kindw = copy_kindw(lv_get(en->livh));
                }
            }
        }
        else continue;
#undef PI
        if (q.id == self && q.kind != TRACKER_PKT_S13) continue;
        send(c, &q, width, height, kind);
    }
    serverreplay_enter(sr, here);
}

void combat_bind(struct serverreplay *sr, struct client_player *cp)
{
    /* a run whose mobs start with a dev summon binds then (dev.c) */
    sr->combat_cp = cp;
    pickobj_bind(sr, cp);
    if (!sr->mobs_enabled || sr->combat) return;
    sr->combat = calloc(1, sizeof *sr->combat);
    if (!sr->combat) abort();
    sr->combat->pending = fixed_array(COMBAT_MAX_PENDING, sizeof *sr->combat->pending);
    sr->combat->cap_pending = COMBAT_MAX_PENDING;
    cp->combat = sr->combat;
    clientworld_init(&sr->combat->client, cp->e.world, &SR_DET(sr), 0, 0, false);
    sr->combat->client.simulate_living_physics = true;
    tracker_init(&sr->combat->tracker);
    /* the join already ran the player's own entry's first check */
    if (sr->player)
    {
        sr->combat->player_watch_initialized = 1;
        sr->combat->player_watch_x = sr->player->e.pos_x;
        sr->combat->player_watch_y = sr->player->e.pos_y;
        sr->combat->player_watch_z = sr->player->e.pos_z;
    }

    /* A snapshot may already contain visible mobs. MobFree's immediate
     * snapshot is empty, while longer setups leave tracker entries behind. */
    for (int i = 0; i < sr->d->anw.n; ++i)
        if (an_ent_at(sr->d->anw.slot[i])->is_living) track_one(sr, lv_get(an_ent_at(sr->d->anw.slot[i])->livh));
    /* the snapshot's client already holds them, and its det state has
     * their constructions: building them here spends no client draw. A
     * snapshot whose harness recorded no client queue (no pending key) was
     * taken before the client built them: their draws are its first tick's */
    if (sr->snap && !nbt_get(sr->snap->player_client, "pending")) apply_pending(sr->combat);
    else
    {
        det_state *d = &SR_DET(sr);
        det_rng seeder = d->seeder[DET_CLIENT], math = d->math[DET_CLIENT];
        int32_t next_id = d->next_id[DET_CLIENT];
        apply_pending(sr->combat);
        d->seeder[DET_CLIENT] = seeder;
        d->math[DET_CLIENT] = math;
        d->next_id[DET_CLIENT] = next_id;
    }

    /* EntityTrackerEntry.ticks counts the updateTrackedEntities passes since
     * the entry was made. A snapshot entity was tracked before server tick 0
     * (the spawn area's population), so it enters the first replayed tick
     * with the snapshot's total time as its count, which sets the phase of
     * its every-third-tick position and velocity packets. A dev summon binds
     * mid-run on its own fresh entries and keeps them. */
    if (sr->current_tick == 0)
        for (int i = 0; i < sr->combat->tracker.nentries; ++i)
        {
            struct tracker_entry *te = &sr->combat->tracker.entries[i];
            const struct sr_trk *k = NULL;
            for (int j = 0; j < sr->ntrk && !k; ++j)
                if (sr->trk[j].id == te->id) k = &sr->trk[j];
            if (!k)
            {
                te->ticks = (int)sr->w[0].st.total_time;
                continue;
            }
            /* the snapshot recorded the entry itself (a checkpoint's
             * entities were tracked at their load, not before tick 0) */
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

    /* a snapshot that records the client's own entities (every one since
     * the any-tick sweep) resumes the tracker and the client world as they
     * were */
    if (sr->snap && sr->snap->has_client_ents) combat_restore_snapshot(sr, sr->combat);
}

static void apply_deflect(struct combat_state *c);

/* updateController's packet handling, the click's deflect, then
 * updateEntities. */
/* The tick's S13s come before the entities update (Java's packets are all
 * handled in updateController, at the head of runTick): a destroyed vehicle
 * drops its rider (World.removeEntity's riddenByEntity.mountEntity(null))
 * before the rider's own update decides whether it rides. */
void combat_client_vehicle_gone(struct client_player *cp, int vehicle_id)
{
    struct combat_state *c = cp->combat;
    if (!c || vehicle_id <= 0) return;
    for (int i = 0; i < c->npending; ++i)
    {
        const struct tracker_packet *k = &c->pending[i].pkt;
        if (k->kind != TRACKER_PKT_S13) continue;
        for (int j = 0; j < k->num_ids; ++j)
            if (k->ids[j] == vehicle_id) { ride_client_s1b(cp, 0); return; }
    }
}

void combat_client_tick(struct client_player *cp)
{
    struct combat_state *c = cp->combat;
    /* the pump: the living and the object trackers' packets in the order
     * the server sent them (a spawner's S0F ahead of a death's S11s), then
     * the object copies' updates */
    if (c)
    {
        for (int i = 0; i < c->npending; ++i)
        {
            pickobj_client_packets_before(cp, c->pending[i].seq);
            apply_one(c, &c->pending[i]);
        }
        c->npending = 0;
    }
    pickobj_client_tick(cp);
    if (!c) return;
    apply_deflect(c);
    c->client.walk_fx = surv_client_fx(cp);
    clientworld_tick_entities(&c->client, cp->e.pos_x, cp->e.pos_y, cp->e.pos_z);
    c->client.walk_fx = NULL;
}

/* World.checkNoEntityCollision on the client world: the living copies (every
 * EntityLivingBase sets preventEntitySpawning) and the object copies that
 * set it. */
int combat_client_prevents(const struct client_player *cp, const struct aabb *box)
{
    if (cp->combat)
    {
        const struct clientworld *cw = &cp->combat->client;
        for (int i = 0; i < cw->nents; ++i)
        {
            const struct client_entity *ce = &cw->ents[i];
            if (!ce->is_living || ce->is_dead || ce->width <= 0.0F) continue;
            struct aabb eb = client_entity_box(ce);
            if (aabb_intersects(&eb, box)) return 1;
        }
    }
    return pickobj_client_prevents(cp, box);
}

/* EntityRenderer.getMouseOver's entity pass (pick.c) over the client
 * world's entities in their chunk-list order: the living copies and the
 * collidable objects (pickobj.c). reach is var2, block_dist var4. */
int combat_pick_entity(const struct client_player *cp, double sx, double sy, double sz,
                       double dx, double dy, double dz, double reach, double block_dist,
                       int have_mouse_over)
{
    enum { CAP = PICK_MAX_CANDIDATES(CW_MAX_ENTITIES) };
    struct pick_entry *pe = nw_scratch->pick_pe;
    struct pick_cand *pc = nw_scratch->pick_pc, *list = nw_scratch->pick_list;
    int *order = nw_scratch->pick_order;
    int n = 0;

    if (cp->combat)
    {
        const struct clientworld *cw = &cp->combat->client;
        for (int i = 0; i < cw->nents && n < CAP; ++i)
        {
            const struct client_entity *ce = &cw->ents[i];
            /* canBeCollidedWith: the living copies and EntityLargeFireball
             * (EntitySmallFireball answers false) */
            if (!(ce->is_living || ce->is_large_fireball) || ce->width <= 0.0F) continue;
            /* in no chunk list: getEntitiesWithinAABBExcludingEntity misses it */
            if (ce->no_chunk) continue;
            struct aabb box = client_entity_box(ce);
            pe[n].cx = ce->chunk_x;
            pe[n].cy = ce->chunk_y;
            pe[n].cz = ce->chunk_z;
            pe[n].seq = ce->chunk_seq;
            pe[n].owner = -1;
            pe[n].box = box;
            pc[n].box = box;
            /* getCollisionBorderSize: EntityFireball's 1.0F, Entity's 0.1F */
            pc[n].border = ce->is_large_fireball ? 1.0F : 0.1F;
            pc[n].collidable = !ce->is_dead;
            pc[n].riding = ce->id == ride_client_vehicle_id(cp);
            pc[n].id = ce->id;
            ++n;
        }
    }
    n += pickobj_client_candidates((struct client_player *)cp, pe + n, pc + n, CAP - n, n);
    if (n == 0) return 0;

    struct aabb search = aabb_expand(aabb_add_coord(cp->e.bounding_box, dx * reach, dy * reach, dz * reach),
                                     1.0, 1.0, 1.0);
    int m = pick_order(pe, n, &search, order, CAP);
    if (m > CAP) m = CAP;
    for (int i = 0; i < m; ++i) list[i] = pc[order[i]];
    struct pick_result r = pick_entity_pass(list, m, sx, sy, sz, dx, dy, dz, reach, block_dist, have_mouse_over);
    return r.replaced ? list[r.chosen].id : 0;
}

/* ints ascending, in place: an ascending run is left as it is (the pass's
 * ids come in spawn order in the recordings), else an LSD radix sort by
 * bytes over the env's scratch (a pool's snapshot-loaded list is in no
 * order: a heapsort a tick was its biggest cost). The same array as any
 * sort gives. */
static void sort_ints(int *a, int n)
{
    int run = 1;
    while (run < n && a[run - 1] <= a[run]) ++run;
    if (run >= n) return;

    uint32_t *tmp ENV_LOCAL = envstack_take((size_t)n * sizeof *tmp);
    uint32_t *src = (uint32_t *)a, *dst = tmp;

    /* the sign bit flipped: unsigned order is the ints' order */
    for (int i = 0; i < n; ++i) src[i] ^= 0x80000000u;
    for (int shift = 0; shift < 32; shift += 8)
    {
        int count[257] = {0};
        for (int i = 0; i < n; ++i) ++count[(src[i] >> shift & 255) + 1];
        if (count[(src[0] >> shift & 255) + 1] == n) continue;   /* one value in this byte: no move */
        for (int b = 0; b < 256; ++b) count[b + 1] += count[b];
        for (int i = 0; i < n; ++i) dst[count[src[i] >> shift & 255]++] = src[i];
        uint32_t *t = src; src = dst; dst = t;
    }
    if (src != (uint32_t *)a) memcpy(a, src, (size_t)n * sizeof *a);
    for (int i = 0; i < n; ++i) a[i] = (int)((uint32_t)a[i] ^ 0x80000000u);
}

/* id is in the ascending a[0..n): bsearch's answer without a comparator
 * call a step */
static int has_int(const int *a, int n, int id)
{
    int lo = 0, hi = n;
    while (lo < hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] < id) lo = mid + 1;
        else hi = mid;
    }
    return lo < n && a[lo] == id;
}

void combat_server_quiet(struct serverreplay *sr)
{
    combat_server_finish(sr);
}

void combat_server_finish(struct serverreplay *sr)
{
    if (!sr->combat)
    {
        pickobj_server_tick(sr);
        return;
    }
    if (!sr->combat->first_pass_done)
    {
        sr->combat->first_pass_done = 1;
        sr->combat->first_quiet_tick = sr->current_tick;
    }
    struct tracker *tr = &sr->combat->tracker;
    /* the ids a tracker entry stays for (the living and the fireballs),
     * sorted once rather than walked once per entry */
    int *live_ids = nw_env->combat.live_ids;
    int nlive = 0;
    for (int j = 0; j < sr->d->anw.n; ++j)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[j]);
        if (en->is_living) live_ids[nlive++] = lv_get(en->livh)->entity_id;
        else if (ie_get(en->ieh)->kind == IE_LARGE_FIREBALL || ie_get(en->ieh)->kind == IE_SMALL_FIREBALL)
            live_ids[nlive++] = ie_get(en->ieh)->entity_id;
    }
    sort_ints(live_ids, nlive);
    /* World.onEntityRemoved put a removed entity's id in the player's
     * destroyedItemsNetCache at once: one ahead of the player in the pass
     * leaves with this tick's onUpdate, after the ids the last tick left */
    for (int i = 0; i < tr->nentries; )
    {
        int id = tr->entries[i].id;
        /* another world's entries are its own tracker's */
        if (tr->entries[i].dim != sr->here) { ++i; continue; }
        if (!tr->entries[i].before_player || has_int(live_ids, nlive, id)) { ++i; continue; }
        if (tr->entries[i].watched) destroy_later(sr->combat, id);
        tracker_remove(tr, id);
    }
    /* (EntityPlayerMP.onUpdate runs in the player's world's pass only) */
    if (combat_player_world(sr))
    {
        flush_destroyed(sr->combat);
        for (int i = 0; i < sr->combat->ndestroyed_late; ++i) destroy_later(sr->combat, sr->combat->destroyed_late[i]);
        sr->combat->ndestroyed_late = 0;
    }
    /* EntityTracker.trackEntity ran when each entity joined the world, in
     * the world's add order, before this update pass: the S1B a rider sends
     * below then finds a vehicle that joined after it */
    for (int i = 0; i < sr->d->anw.n; ++i)
        if (an_ent_at(sr->d->anw.slot[i])->is_living) combat_track_spawn(sr, lv_get(an_ent_at(sr->d->anw.slot[i])->livh));
    /* the objects' tracker after the new livings' spawns: a living the tick
     * spawned (the spawners run ahead of the entity pass) reaches the client
     * ahead of the objects the pass made (a death's orbs) */
    pickobj_server_tick(sr);
    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (en->is_living) track_one(sr, lv_get(en->livh));
        else if (ie_get(en->ieh)->kind == IE_LARGE_FIREBALL || ie_get(en->ieh)->kind == IE_SMALL_FIREBALL)
            track_fireball(sr, ie_get(en->ieh));
    }
    /* EntityTracker.updateTrackedEntities' second loop: the player's own
     * entry ran sendLocationToAllClients too, and when it moved four blocks
     * since that entry's last check every other entry tries the player */
    struct combat_state *c = sr->combat;
    const struct server_player *pl = sr->player;
    /* none while the player is out of the world, a new one at the respawn
     * (pickobj_server_tick); the player's own entry is gone while a ghost
     * has it unlisted */
    if (pl && pl->sv.removed) c->player_watch_initialized = 2;
    else if (pl && combat_player_world(sr) && !sr->player_unlisted)
    {
        double px = pl->e.pos_x - c->player_watch_x;
        double py = pl->e.pos_y - c->player_watch_y;
        double pz = pl->e.pos_z - c->player_watch_z;
        if (c->player_watch_initialized != 1 || px*px + py*py + pz*pz > 16.0)
        {
            int first = !c->player_watch_initialized;
            c->player_watch_initialized = 1;
            c->player_watch_x = pl->e.pos_x;
            c->player_watch_y = pl->e.pos_y;
            c->player_watch_z = pl->e.pos_z;
            for (int i = 0; !first && i < sr->d->anw.n; ++i)
            {
                struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
                if (!en->is_living || en == an_deref(sr->player_enth)) continue;
                struct tracker_entry *te = tracker_find(&c->tracker, lv_get(en->livh)->entity_id);
                if (te) try_start_watching(sr, te, lv_get(en->livh));
            }
        }
    }
    /* the rest left behind the player: its next onUpdate sends them */
    for (int i = 0; i < tr->nentries; )
    {
        int id = tr->entries[i].id;
        /* another world's entries are its own tracker's */
        if (tr->entries[i].dim != sr->here) { ++i; continue; }
        if (has_int(live_ids, nlive, id)) { ++i; continue; }
        if (tr->entries[i].watched) destroy_later(sr->combat, id);
        tracker_remove(tr, id);
    }
    /* each entry's place against the player in the pass order, for its
     * removal */
    int player = -1;
    for (int k = 0; k < sr->d->nents; ++k)
        if (sr->d->ents[k].pool == 3) { player = k; break; }
    for (int k = 0; k < sr->d->nents; ++k)
    {
        if (sr->d->ents[k].pool != 2) continue;
        struct an_ent *en = sr_ent_p(sr, &sr->d->ents[k]);
        int id = en->is_living ? lv_get(en->livh)->entity_id
               : ie_get(en->ieh)->kind == IE_LARGE_FIREBALL || ie_get(en->ieh)->kind == IE_SMALL_FIREBALL
                 ? ie_get(en->ieh)->entity_id : -1;
        struct tracker_entry *te = id >= 0 ? tracker_find(tr, id) : NULL;
        if (te) te->before_player = player >= 0 && k < player;
    }
}

/* NetHandlerPlayServer.processUseEntity's ATTACK on a fireball, through
 * EntityPlayer.attackTargetEntityWithCurrentItem: the fireball's
 * canAttackWithItem() is true and hitByEntity() false, so the attack
 * damage attribute (1.0 bare hand, +the sword/tool modifier) goes into
 * EntityFireball.attackEntityFrom: setBeenAttacked, then motion from the
 * attacker's getLookVec(), the acceleration from it at *0.1, and the
 * shootingEntity adoption. No crit path (the target is not an
 * EntityLivingBase), no tool wear (hitEntity runs only on a living), no
 * damageDealt stat; the sprint knockback lands on the fireball and the
 * 0.3 hunger exhaust lands on the player. */
static void combat_server_attack_fireball(struct serverreplay *sr, ie_ent *fb)
{
    if (fb->is_dead) return;
    struct server_player *p = sr->player;
    struct rt_mop mop;
    /* canEntityBeSeen: the eye-to-eye raytrace; the fireball's eye height is
     * Entity.getEyeHeight()'s 0.0F */
    int seen = !raytrace_blocks(p->e.world, p->e.pos_x, p->e.pos_y + 1.62F,
                                p->e.pos_z, fb->e.pos_x, fb->e.pos_y, fb->e.pos_z,
                                0, 0, 0, &mop);
    double dx = p->e.pos_x - fb->e.pos_x;
    double dy = p->e.pos_y - fb->e.pos_y;
    double dz = p->e.pos_z - fb->e.pos_z;
    double reach = seen ? 36.0 : 9.0;
    if (dx*dx + dy*dy + dz*dz >= reach) return;

    /* the attack damage attribute: the base 1.0 plus the modifier the
     * equipment loop last put in the map (the living path's rule) */
    float damage = 1.0F;
    if (p->held_attr_item >= 0 && p->held_attr_item < 4096)
        damage = (float)(1.0 + (double)ITEMS[p->held_attr_item].damage);
    if (damage <= 0.0F) return;

    /* EntityFireball.attackEntityFrom: the deflect */
    fb->e.velocity_changed = 1;
    double lx, ly, lz;
    /* EntityLivingBase.getLookVec() is getLook(1.0F), the same math the
     * ghast lane's ghast_look_vec carries */
    struct living *twin ENV_LOCAL = envstack_zeroed(sizeof *twin);
    twin->e.pos_x = p->e.pos_x;
    twin->e.pos_y = p->e.pos_y;
    twin->e.pos_z = p->e.pos_z;
    twin->rotation_yaw = p->rotation_yaw;
    twin->rotation_pitch = p->rotation_pitch;
    ghast_look_vec(twin, &lx, &ly, &lz);
    fb->e.motion_x = lx;
    fb->e.motion_y = ly;
    fb->e.motion_z = lz;
    fb->accel_x = lx * 0.1;
    fb->accel_y = ly * 0.1;
    fb->accel_z = lz * 0.1;
    fb->shooter = lv_ref(lv_get(sr->player_livh));
    fb->shooter_is_player = 1;

    /* attackTargetEntityWithCurrentItem's tail, on the landed hit: the
     * sprint knockback addVelocity lands on the fireball (var3 = sprint's
     * +1), then the player's own 0.6 x/z damping and setSprinting(false) */
    if (p->sprinting)
    {
        float yaw = (p->rotation_yaw * 3.1415927F) / 180.0F;
        fb->e.motion_x += (double)(-mh_sin(yaw) * 0.5F);
        fb->e.motion_y += 0.1;
        fb->e.motion_z += (double)(mh_cos(yaw) * 0.5F);
        p->e.motion_x *= 0.6;
        p->e.motion_z *= 0.6;
        /* setSprinting(false): the S20's dirt only when a modifier goes */
        if (p->sprinting) p->attr_dirty = 1;
        p->sprinting = 0;
        p->move_speed = player_move_speed(0, p->pot_slow, p->pot_speed);
    }

    /* addExhaustion(0.3F) (no tool wear, no crit, no damageDealt stat: the
     * target is no EntityLivingBase) */
    surv_add_exhaustion(p, 0.3F);
}

/* NetHandlerPlayServer.processUseEntity's ATTACK on one of the dragon's
 * parts (WorldServer's entityIdMap holds them), through
 * EntityPlayer.attackTargetEntityWithCurrentItem: the part is no
 * EntityLivingBase, so no enchantment modifier, knockback enchantment or
 * crit; EntityDragonPart.attackEntityFrom is attackEntityFromPart with the
 * player's damage source, which answers true. hitEntity wears the held
 * item on the dragon itself; no damageDealt stat (that test asks the part).
 * 1 when entity_id is a part. */
static int combat_server_attack_part(struct serverreplay *sr, int entity_id)
{
    struct dragon_state *d = serverreplay_dragon(sr);
    if (d == NULL || d->dead || entity_id <= d->entity_id || entity_id > d->entity_id + 7) return 0;
    int part = entity_id - d->entity_id - 1;
    const struct dragon_part_state *pt = &d->part[part];
    struct server_player *p = sr->player;
    if (p == NULL || sr->player_livh == 0) return 1;
    /* canEntityBeSeen: to the part's eye, Entity.getEyeHeight's 0 */
    struct rt_mop mop;
    int blocked = raytrace_blocks(p->e.world, p->e.pos_x, p->e.pos_y + 1.62F, p->e.pos_z,
                                  pt->x, pt->y, pt->z, 0, 0, 0, &mop);
    double x = p->e.pos_x - pt->x, y = p->e.pos_y - pt->y, z = p->e.pos_z - pt->z;
    if (x * x + y * y + z * z >= (blocked ? 9.0 : 36.0)) return 1;

    struct living *pl = lv_get(sr->player_livh);
    struct attr_instance attack = pl->attrs.a[ATTR_ATTACK_DAMAGE];
    if (p->held_attr_item >= 0 && p->held_attr_item < 4096 &&
        (ITEMS[p->held_attr_item].kind == ITEM_SWORD || ITEMS[p->held_attr_item].kind == ITEM_TOOL))
    {
        struct attr_mod weapon;
        memset(&weapon, 0, sizeof weapon);
        weapon.amount = (double)ITEMS[p->held_attr_item].damage;
        weapon.operation = 0;
        weapon.uuid_msb = -3801225194067177672LL;
        weapon.uuid_lsb = -6586624321849018929LL;
        attrs_apply(&attack, &weapon);
    }
    attack.needs_update = 1;
    float damage = (float)attrs_value(&attack);
    if (!(damage > 0.0F)) return 1;

    /* the knockBack inside reads the attacker's position */
    d->player_x = p->e.pos_x;
    d->player_y = p->e.pos_y;
    d->player_z = p->e.pos_z;
    /* onDeath's addToPlayerScore: the mobKills counter */
    if (dragon_attack_part(d, part, damage, 1)) surv_add_stat(p, STAT_MOB_KILLS, 1);
    /* the killing blow's S19 status 3 goes out now, in the network tick */
    if (d->health <= 0.0F) pickobj_server_dragon_death(sr, d->entity_id);

    if (p->sprinting)
    {
        /* the addVelocity lands on the part, which never moves by it; the
         * player slows and stops sprinting */
        p->e.motion_x *= 0.6;
        p->e.motion_z *= 0.6;
        /* setSprinting(false): the S20's dirt only when a modifier goes */
        if (p->sprinting) p->attr_dirty = 1;
        p->sprinting = 0;
        p->move_speed = player_move_speed(0, p->pot_slow, p->pot_speed);
    }
    if (damage >= 18.0F) surv_add_stat(p, ACH_OVERKILL, 1);

    struct surv_stack *held = &p->sv.inv[p->sv.current_item];
    if (held->count > 0 && held->item >= 0 && held->item < 4096)
    {
        const struct item_def *item = &ITEMS[held->item];
        int wear = item->kind == ITEM_SWORD ? 1 : (item->kind == ITEM_TOOL ? 2 : 0);
        if (wear)
        {
            pl->rand = p->sv.erand;
            (void)surv_damage_stack(p, held, wear);
            pl->rand = p->sv.erand;
            surv_add_stat(p, SURV_STAT_USE(held->item), 1);
            if (held->count <= 0) surv_destroy_current_item(p);
        }
        /* ItemSaddle.hitEntity on the dragon: itemInteractionForEntity
         * refuses a non-pig, the answer is true all the same */
        else if (held->item == ITEM_SADDLE_ID) surv_add_stat(p, SURV_STAT_USE(held->item), 1);
    }
    surv_add_exhaustion(p, 0.3F);
    return 1;
}

void combat_server_attack(struct serverreplay *sr, int entity_id)
{
    if (sr && pickobj_server_attack(sr, entity_id)) return;
    if (sr && combat_server_attack_part(sr, entity_id)) return;
    if (!sr || !sr->mobs_enabled || !sr->player || !sr->player_livh) return;
    struct living *target = NULL;
    ie_ent *fb = NULL;
    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (en->is_living && lv_get(en->livh)->entity_id == entity_id) {
            target = lv_get(en->livh);
            break;
        }
        if (!en->is_living && ie_get(en->ieh)->kind == IE_LARGE_FIREBALL &&
            ie_get(en->ieh)->entity_id == entity_id)
        {
            fb = ie_get(en->ieh);
            break;
        }
    }
    if (fb)
    {
        combat_server_attack_fireball(sr, fb);
        return;
    }
    if (!target || target->is_dead) return;
    struct server_player *p = sr->player;
    struct rt_mop mop;
    int blocked = raytrace_blocks(p->e.world, p->e.pos_x, p->e.pos_y + 1.62F,
        p->e.pos_z, target->e.pos_x,
        target->e.pos_y + (double)(target->e.height * 0.85F), target->e.pos_z,
        0, 0, 0, &mop);
    double x = p->e.pos_x - target->e.pos_x;
    double y = p->e.pos_y - target->e.pos_y;
    double z = p->e.pos_z - target->e.pos_z;
    if (x*x + y*y + z*z >= (blocked ? 9.0 : 36.0)) return;

    struct living *pl = lv_get(sr->player_livh);
    pl->e.pos_x = p->e.pos_x;
    pl->e.pos_y = p->e.pos_y;
    pl->e.pos_z = p->e.pos_z;
    pl->rotation_yaw = p->rotation_yaw;

    /* EntityPlayer.attackTargetEntityWithCurrentItem. var2 is
     * getEntityAttribute(attackDamage): the base 1.0, whatever the equipment
     * loop last put in the map (p->held_attr_item: an item acquired since the
     * last game tick has no modifier yet) and the potion modifiers the twin
     * carries (weakness, strength) */
    struct attr_instance attack = pl->attrs.a[ATTR_ATTACK_DAMAGE];
    if (p->held_attr_item >= 0 && p->held_attr_item < 4096 &&
        (ITEMS[p->held_attr_item].kind == ITEM_SWORD || ITEMS[p->held_attr_item].kind == ITEM_TOOL))
    {
        struct attr_mod weapon;
        memset(&weapon, 0, sizeof weapon);
        weapon.amount = (double)ITEMS[p->held_attr_item].damage;
        weapon.operation = 0;
        weapon.uuid_msb = -3801225194067177672LL;
        weapon.uuid_lsb = -6586624321849018929LL;
        attrs_apply(&attack, &weapon);
    }
    attack.needs_update = 1;
    float damage = (float)attrs_value(&attack);
    /* getEnchantmentModifierLiving and getKnockbackModifier read the stack in
     * hand now (getHeldItem) */
    float ench = ench_modifier_living(pl, target);
    int knockback = ench_knockback(pl) + (p->sprinting ? 1 : 0);

    if (!(damage > 0.0F || ench > 0.0F)) return;

    int critical = p->e.fall_distance > 0.0F && !p->e.on_ground && !p->e.in_water &&
                   !living_is_potion_active(pl, POT_BLINDNESS) && p->ridingh == 0;
    /* isOnLadder: only relevant while standing in a climbable cell */
    int feet = world_get_block(p->e.world, mh_floor(p->e.pos_x),
                               mh_floor(p->e.pos_y), mh_floor(p->e.pos_z)) & 4095;
    if (feet == 65 || feet == 106) critical = 0;
    if (critical && damage > 0.0F) damage *= 1.5F;
    damage += ench;

    int fire_aspect = ench_fire_aspect(pl);
    int lit = 0;
    struct entity *target_fire = living_fire_entity(target);
    int burning = !target->immune_to_fire && target_fire->fire > 0;   /* Entity.isBurning */
    if (fire_aspect > 0 && !burning)
    {
        lit = 1;
        living_set_fire(target, 1);
    }

    int landed = living_attack_entity_from_attacker(target, pl, DMG_MOB, damage, &SR_DET(sr));
    if (!landed)
    {
        /* Entity.extinguish */
        if (lit) target_fire->fire = 0;
        return;
    }

    if (knockback > 0)
    {
        /* Entity.addVelocity on the target, the attacker slows and stops
         * sprinting */
        float yaw = (p->rotation_yaw * 3.1415927F) / 180.0F;
        target->e.motion_x += (double)(-mh_sin(yaw) * (float)knockback * 0.5F);
        target->e.motion_y += 0.1;
        target->e.motion_z += (double)(mh_cos(yaw) * (float)knockback * 0.5F);
        target->is_air_borne = 1;
        p->e.motion_x *= 0.6;
        p->e.motion_z *= 0.6;
        /* setSprinting(false): the S20's dirt only when a modifier goes */
        if (p->sprinting) p->attr_dirty = 1;
        p->sprinting = 0;
        p->move_speed = player_move_speed(0, p->pot_slow, p->pot_speed);
    }

    /* onCriticalHit / onEnchantmentCritical: EntityTracker.func_151248_b
     * sends the S0B animation to the player too, whose client builds an
     * EntityCrit2FX on its copy of the target */
    for (int anim = 4; anim <= 5; ++anim)
    {
        if (anim == 4 ? !critical : !(ench > 0.0F)) continue;
        struct s2c_pkt *s0b = s2c_add(s2c_out());
        s0b->kind = PK_S0B;
        s0b->i0 = anim;
        s0b->i1 = target->entity_id;
    }
    if (damage >= 18.0F) surv_add_stat(p, ACH_OVERKILL, 1);

    /* EnchantmentHelper.func_151384_a(target, this): thorns on the target's
     * equipment; func_151385_b(this, target): bane of arthropods */
    ench_hurt_iter(target, pl, &SR_DET(sr));
    ench_damage_iter(pl, target, &SR_DET(sr));

    /* ItemStack.hitEntity: a sword wears 1, a tool 2; a stack worn out of
     * existence is destroyCurrentEquippedItem */
    struct surv_stack *held = &p->sv.inv[p->sv.current_item];
    if (held->count > 0 && held->item >= 0 && held->item < 4096)
    {
        const struct item_def *item = &ITEMS[held->item];
        int wear = item->kind == ITEM_SWORD ? 1 : (item->kind == ITEM_TOOL ? 2 : 0);
        if (wear)
        {
            pl->rand = p->sv.erand;
            (void)surv_damage_stack(p, held, wear);
            pl->rand = p->sv.erand;
        }
        /* ItemSaddle.hitEntity: itemInteractionForEntity(stack, null,
         * target) saddles an unsaddled adult pig and spends the saddle,
         * and answers true whatever it hit */
        int saddle = held->item == ITEM_SADDLE_ID;
        if (saddle) (void)ride_saddle_interact(held, target, 1);
        /* ItemSword.hitEntity, ItemTool.hitEntity and ItemSaddle.hitEntity
         * answer true, so ItemStack.hitEntity adds the item's use stat
         * before the destroy */
        if (wear || saddle) surv_add_stat(p, SURV_STAT_USE(held->item), 1);
        if ((wear || saddle) && held->count <= 0) surv_destroy_current_item(p);
    }

    surv_add_stat(p, STAT_DAMAGE_DEALT, (int)(damage * 10.0F + 0.5F));
    if (fire_aspect > 0) living_set_fire(target, fire_aspect * 4);
    surv_add_exhaustion(p, 0.3F);
}

/* PlayerControllerMP.attackEntity's client half: after the C02 it runs
 * attackTargetEntityWithCurrentItem on the client's own entity. A living
 * answers attackEntityFrom false on a remote world; EntityFireball has no
 * such check, so the client's fireball is deflected along the client
 * player's getLookVec (the sprint knockback lands on it too). The player's
 * own half happens here; the fireball's waits for the tick's packets
 * (combat_client_tick). */
void combat_client_attack(struct client_player *cp, int entity_id)
{
    if (!cp->combat) return;
    struct client_entity *ce = clientworld_get_entity(&cp->combat->client, entity_id);
    if (!ce || ce->is_dead || ce->is_living || !ce->is_fireball) return;
    struct combat_state *c = cp->combat;
    struct living *twin ENV_LOCAL = envstack_zeroed(sizeof *twin);
    twin->rotation_yaw = cp->rotation_yaw;
    twin->rotation_pitch = cp->rotation_pitch;
    ghast_look_vec(twin, &c->deflect_x, &c->deflect_y, &c->deflect_z);
    c->deflect_id = entity_id;
    c->deflect_sprint = cp->sprinting;
    c->deflect_yaw = cp->rotation_yaw;
    if (cp->sprinting)
    {
        cp->e.motion_x *= 0.6;
        cp->e.motion_z *= 0.6;
        /* EntityPlayerSP.setSprinting(false) */
        cp->sprinting = 0;
        cp->sprinting_ticks_left = 0;
        cp->mod_sprint = 0;
        cp->move_speed = player_move_speed(0, cp->mod_slow, cp->mod_speed);
    }
}

static void apply_deflect(struct combat_state *c)
{
    if (!c->deflect_id) return;
    struct client_entity *ce = clientworld_get_entity(&c->client, c->deflect_id);
    c->deflect_id = 0;
    if (!ce || ce->is_dead) return;
    ce->motion_x = c->deflect_x;
    ce->motion_y = c->deflect_y;
    ce->motion_z = c->deflect_z;
    ce->accel_x = c->deflect_x * 0.1;
    ce->accel_y = c->deflect_y * 0.1;
    ce->accel_z = c->deflect_z * 0.1;
    if (c->deflect_sprint)
    {
        float yaw = (c->deflect_yaw * 3.1415927F) / 180.0F;
        ce->motion_x += (double)(-mh_sin(yaw) * 0.5F);
        ce->motion_y += 0.1;
        ce->motion_z += (double)(mh_cos(yaw) * 0.5F);
    }
}

/* EntityTracker.addEntityToTracker(EntityPlayerMP) at a transfer's
 * spawnEntityInWorld: every entry of the new world tries the player at once
 * (the caller sets sr->watch_cl to the old world's player manager). An
 * entity that joined during the transfer (a chunk the portal search loaded)
 * has had its entry since it joined (trackEntity at onEntityAdded), which
 * native makes here. */
void combat_player_spawned(struct serverreplay *sr)
{
    if (!sr->combat || !sr->player) return;
    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (!en->is_living || en == an_deref(sr->player_enth)) continue;
        struct living *l = lv_get(en->livh);
        struct tracker_entry *te = tracker_find(&sr->combat->tracker, l->entity_id);
        if (te) try_start_watching(sr, te, l);
        else combat_track_spawn(sr, l);
    }
}

void combat_player_removed(struct serverreplay *sr)
{
    /* an entry that passes its four-block check (or a sent chunk's) later
     * spawns its entity on the client again */
    if (sr->combat)
        for (int i = 0; i < sr->combat->tracker.nentries; ++i) sr->combat->tracker.entries[i].watched = false;
    if (sr->pickobj)
        for (int i = 0; i < sr->pickobj->tr.nentries; ++i) sr->pickobj->tr.entries[i].watched = false;
}

int combat_fx_target(void *ctx, int id, struct plive_target *out)
{
    const struct client_player *cp = ctx;
    const struct client_entity *e = cp->combat ? clientworld_get_entity(&cp->combat->client, id) : NULL;
    if (e == NULL || !e->is_living) return 0;
    out->x = e->x;
    out->min_y = e->y;
    out->z = e->z;
    out->width = e->width;
    out->height = e->height;
    return 1;
}

void combat_client_flush(struct client_player *cp)
{
    pickobj_client_flush(cp);
    struct combat_state *c = cp->combat;
    if (!c) return;
    if (!c->s07_mark)
    {
        apply_pending(c);
        return;
    }
    /* the packets ahead of the transfer's S07 only; the rest wait for the
     * new client world */
    int n = c->npending, k = c->s07_mark - 1;
    c->npending = k;
    apply_pending(c);
    memmove(c->pending, c->pending + k, (size_t)(n - k) * sizeof *c->pending);
    c->npending = n - k;
}

void combat_client_world_change(struct client_player *cp)
{
    pickobj_client_world_change(cp);
    struct combat_state *c = cp->combat;
    if (!c) return;
    bool simulate = c->client.simulate_living_physics;
    clientworld_init(&c->client, cp->e.world, c->client.det, 0, 0, false);
    c->client.simulate_living_physics = simulate;
    c->deflect_id = 0;
    for (int i = 0; i < c->tracker.nentries; ++i) c->tracker.entries[i].watched = false;
    if (!c->s07_mark)
    {
        c->npending = 0;
        return;
    }
    /* the transfer's spawns after its S07 (combat_player_spawned) build in
     * the new world at this tick's apply, their entries watching */
    c->s07_mark = 0;
    for (int i = 0; i < c->npending; ++i)
    {
        if (c->pending[i].pkt.kind != TRACKER_PKT_S0F) continue;
        struct tracker_entry *te = tracker_find(&c->tracker, c->pending[i].pkt.id);
        if (te) te->watched = true;
    }
}
