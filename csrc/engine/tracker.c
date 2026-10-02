#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tracker.h"
#include "arena.h"
#include "jmath.h"

static inline int j_ceil(double d)
{
    int i = (int)d;
    return d > (double)i ? i + 1 : i;
}

/* S12PacketEntityVelocity(Entity)'s build: the motion clamped to +/-3.9,
 * then *8000. */
static inline int s12_scaled(double m)
{
    if (m < -3.9) m = -3.9;
    if (m > 3.9) m = 3.9;
    return (int)(m * 8000.0);
}

int tracker_multiply_by_32_and_round(double p)
{
    return tracker_mul32_size(p, 2);
}

int tracker_mul32_size(double p, int size)
{
    double var3 = p - ((double)mh_floor(p) + 0.5);
    switch (size)
    {
    case 1:
        if (var3 < 0.0) { if (var3 < -0.3125) return j_ceil(p * 32.0); }
        else if (var3 < 0.3125) return j_ceil(p * 32.0);
        return mh_floor(p * 32.0);
    case 0:
    case 2:
        if (var3 < 0.0) { if (var3 < -0.3125) return mh_floor(p * 32.0); }
        else if (var3 < 0.3125) return mh_floor(p * 32.0);
        return j_ceil(p * 32.0);
    case 3:
        return var3 > 0.0 ? mh_floor(p * 32.0) : j_ceil(p * 32.0);
    case 4:
        if (var3 < 0.0) { if (var3 < -0.1875) return j_ceil(p * 32.0); }
        else if (var3 < 0.1875) return j_ceil(p * 32.0);
        return mh_floor(p * 32.0);
    case 5:
        if (var3 < 0.0) { if (var3 < -0.1875) return mh_floor(p * 32.0); }
        else if (var3 < 0.1875) return mh_floor(p * 32.0);
        return j_ceil(p * 32.0);
    default:
        return var3 > 0.0 ? j_ceil(p * 32.0) : mh_floor(p * 32.0);
    }
}

int tracker_entity_size(float width)
{
    /* fmodf is exact and returns a width in [0, 2) as it is */
    float r = width >= 0.0F && width < 2.0F ? width : fmodf(width, 2.0F);
    if ((double)r < 0.375) return 1;
    if ((double)r < 0.75) return 2;
    if ((double)r < 1.0) return 3;
    if ((double)r < 1.375) return 4;
    if ((double)r < 1.75) return 5;
    return 6;
}

void tracker_init(struct tracker *tr)
{
    memset(tr, 0, sizeof(*tr));
    tr->nentries = 0;
    tr->negative_check_s15_for_s17 = false;
}

static inline uint32_t tracker_mask(const struct tracker *tr)
{
    return tr->index_mask | 63u;
}

static inline uint32_t tracker_slot(const struct tracker *tr, int id)
{
    return ((uint32_t)id * 0x9E3779B1u) >> 19 & tracker_mask(tr);
}

/* the index slot holding id's entry, or the empty slot that ends its probe */
static uint32_t tracker_probe(const struct tracker *tr, int id)
{
    uint32_t m = tracker_mask(tr), h = tracker_slot(tr, id);
    while (tr->index[h].entry != 0 && tr->index[h].id != id) h = (h + 1) & m;
    return h;
}

/* the index over mask + 1 slots, every entry in it again */
static void tracker_rehash(struct tracker *tr, uint32_t mask)
{
    memset(tr->index, 0, (size_t)(mask + 1) * sizeof *tr->index);
    tr->index_mask = mask;
    for (int i = 0; i < tr->nentries; ++i)
    {
        uint32_t h = tracker_probe(tr, tr->entries[i].id);
        tr->index[h].id = tr->entries[i].id;
        tr->index[h].entry = (uint16_t)(i + 1);
    }
}

struct tracker_entry *tracker_find(struct tracker *tr, int id)
{
    uint32_t h = tracker_probe(tr, id);
    return tr->index[h].entry ? &tr->entries[tr->index[h].entry - 1] : NULL;
}

void tracker_remove(struct tracker *tr, int id)
{
    uint32_t m = tracker_mask(tr), h = tracker_probe(tr, id);
    if (tr->index[h].entry == 0) return;
    int i = tr->index[h].entry - 1, last = tr->nentries - 1;
    /* the slot emptied, the probe runs after it closed up (backward shift) */
    for (uint32_t j = (h + 1) & m; tr->index[j].entry != 0; j = (j + 1) & m)
    {
        uint32_t home = tracker_slot(tr, tr->index[j].id);
        if (((j - home) & m) >= ((j - h) & m))
        {
            tr->index[h] = tr->index[j];
            h = j;
        }
    }
    tr->index[h].entry = 0;
    /* the last entry into the gap, as before */
    if (i != last)
    {
        tr->index[tracker_probe(tr, tr->entries[last].id)].entry = (uint16_t)(i + 1);
        tr->entries[i] = tr->entries[last];
    }
    tr->nentries--;
}

static const char *const TRACKER_CLASS_NAMES[] = {
    "EntityObject", "EntityLivingBase", "EntitySmallFireball", "EntityLargeFireball", "EntityFireball", "EntityPlayerMP",
    "EntityFishHook", "EntityArrow", "EntityItem", "EntityFallingBlock", "EntityXPOrb", "?"};
_Static_assert(sizeof TRACKER_CLASS_NAMES / sizeof *TRACKER_CLASS_NAMES == TRK_OTHER + 1, "a name a class");

const char *tracker_class_name(int cls)
{
    return TRACKER_CLASS_NAMES[cls >= 0 && cls < TRK_OTHER ? cls : TRK_OTHER];
}

int tracker_class_of(const char *name)
{
    for (int k = 0; k < TRK_OTHER; ++k)
        if (name != NULL && !strcmp(name, TRACKER_CLASS_NAMES[k])) return k;
    return TRK_OTHER;
}

struct tracker_entry *tracker_add(struct tracker *tr, int id, int cls, double x, double y, double z, float yaw, float pitch, float head_yaw)
{
    struct tracker_entry *e = tracker_find(tr, id);
    if (!e)
    {
        if (tr->nentries >= TRACKER_MAX_ENTRIES) list_full("tracked entities", TRACKER_MAX_ENTRIES);
        /* the table at least twice the entries */
        uint32_t m = tracker_mask(tr);
        if ((uint32_t)(tr->nentries + 1) * 2 > m + 1 || tr->index_mask == 0) tracker_rehash(tr, (uint32_t)(tr->nentries + 1) * 2 > m + 1 ? 2 * m + 1 : m);
        uint32_t h = tracker_probe(tr, id);
        tr->index[h].id = id;
        tr->index[h].entry = (uint16_t)(tr->nentries + 1);
        e = &tr->entries[tr->nentries++];
        memset(e, 0, sizeof(*e));
    }
    e->id = id;
    e->cls = cls;
    e->x = x;
    e->y = y;
    e->z = z;
    e->yaw = yaw;
    e->pitch = pitch;
    e->head_yaw = head_yaw;

    /* the EntityTrackerEntry constructor floors, unlike the updates */
    e->last_scaled_x = mh_floor(x * 32.0);
    e->last_scaled_y = mh_floor(y * 32.0);
    e->last_scaled_z = mh_floor(z * 32.0);
    e->last_yaw = mh_floor(yaw * 256.0f / 360.0f);
    e->last_pitch = mh_floor(pitch * 256.0f / 360.0f);
    e->last_head_yaw = mh_floor(head_yaw * 256.0f / 360.0f);

    /* Default ranges and frequencies */
    if (cls == TRK_PLAYER_MP)
    {
        e->tracking_range = 512;
        e->update_frequency = 2;
        e->send_velocity_updates = false;
    }
    else if (cls == TRK_FISH_HOOK)
    {
        e->tracking_range = 64;
        e->update_frequency = 5;
        e->send_velocity_updates = true;
    }
    else if (cls == TRK_ARROW)
    {
        e->tracking_range = 64;
        e->update_frequency = 20;
        e->send_velocity_updates = false;
    }
    else if (cls == TRK_SMALL_FIREBALL || cls == TRK_FIREBALL || cls == TRK_LARGE_FIREBALL)
    {
        /* EntityTracker.addEntityToTracker's EntityFireball branch (the small
         * fireball's own branch reads the same) */
        e->tracking_range = 64;
        e->update_frequency = 10;
        e->send_velocity_updates = false;
    }
    else if (cls == TRK_ITEM)
    {
        e->tracking_range = 64;
        e->update_frequency = 20;
        e->send_velocity_updates = true;
    }
    else if (cls == TRK_FALLING_BLOCK || cls == TRK_XP_ORB)
    {
        e->tracking_range = 160;
        e->update_frequency = 20;
        e->send_velocity_updates = true;
    }
    else
    {
        /* Mobs, animals, etc. */
        e->tracking_range = 80;
        e->update_frequency = 3;
        e->send_velocity_updates = true;
    }

    return e;
}

int tracker_entry_tick(struct tracker *tr, struct tracker_entry *e, struct tracker_packet *out_pkts, int max_pkts)
{
    int n = 0;
    if ((e->ticks % e->update_frequency == 0 || e->is_airborne || e->dw_dirty) && e->is_riding)
    {
        /* sendLocationToAllClients while riding: the look only; the scaled
         * position follows the entity silently, and the client places it on
         * its vehicle */
        e->dw_dirty = false;
        int syaw = mh_floor(e->yaw * 256.0f / 360.0f);
        int spitch = mh_floor(e->pitch * 256.0f / 360.0f);
        if (abs(syaw - e->last_yaw) >= 4 || abs(spitch - e->last_pitch) >= 4)
        {
            if (n < max_pkts)
            {
                tracker_packet_clear(&out_pkts[n]);
                out_pkts[n].kind = TRACKER_PKT_S16;
                out_pkts[n].id = e->id;
                out_pkts[n].yaw = (int8_t)syaw;
                out_pkts[n].pitch = (int8_t)spitch;
                n++;
            }
            e->last_yaw = syaw;
            e->last_pitch = spitch;
        }
        e->last_scaled_x = tracker_mul32_size(e->x, e->size);
        e->last_scaled_y = mh_floor(e->y * 32.0);
        e->last_scaled_z = tracker_mul32_size(e->z, e->size);
        e->riding_entity = true;

        int head_yaw = mh_floor(e->head_yaw * 256.0f / 360.0f);
        if (abs(head_yaw - e->last_head_yaw) >= 4)
        {
            if (n < max_pkts)
            {
                tracker_packet_clear(&out_pkts[n]);
                out_pkts[n].kind = TRACKER_PKT_S19;
                out_pkts[n].id = e->id;
                out_pkts[n].head_yaw = (int8_t)head_yaw;
                n++;
            }
            e->last_head_yaw = head_yaw;
        }
        e->is_airborne = false;
    }
    else if (e->ticks % e->update_frequency == 0 || e->is_airborne || e->dw_dirty)
    {
        e->dw_dirty = false;
        ++e->ticks_since_forced_teleport;
        int sx = tracker_mul32_size(e->x, e->size);
        int sy = mh_floor(e->y * 32.0);
        int sz = tracker_mul32_size(e->z, e->size);
        int syaw = mh_floor(e->yaw * 256.0f / 360.0f);
        int spitch = mh_floor(e->pitch * 256.0f / 360.0f);
        int dx = sx - e->last_scaled_x;
        int dy = sy - e->last_scaled_y;
        int dz = sz - e->last_scaled_z;
        bool pos_changed = abs(dx) >= 4 || abs(dy) >= 4 || abs(dz) >= 4 || (e->ticks % 60 == 0);
        bool rot_changed = abs(syaw - e->last_yaw) >= 4 || abs(spitch - e->last_pitch) >= 4;

        if (e->ticks > 0)
        {
            if (dx >= -128 && dx < 128 && dy >= -128 && dy < 128 && dz >= -128 && dz < 128 &&
                e->ticks_since_forced_teleport <= 400 && !e->riding_entity)
            {
                if (pos_changed && rot_changed)
                {
                    if (tr->negative_check_s15_for_s17)
                    {
                        if (n < max_pkts)
                        {
                            tracker_packet_clear(&out_pkts[n]);
                            out_pkts[n].kind = TRACKER_PKT_S15;
                            out_pkts[n].id = e->id;
                            out_pkts[n].x = (int8_t)dx;
                            out_pkts[n].y = (int8_t)dy;
                            out_pkts[n].z = (int8_t)dz;
                            n++;
                        }
                    }
                    else
                    {
                        if (n < max_pkts)
                        {
                            tracker_packet_clear(&out_pkts[n]);
                            out_pkts[n].kind = TRACKER_PKT_S17;
                            out_pkts[n].id = e->id;
                            out_pkts[n].x = (int8_t)dx;
                            out_pkts[n].y = (int8_t)dy;
                            out_pkts[n].z = (int8_t)dz;
                            out_pkts[n].yaw = (int8_t)syaw;
                            out_pkts[n].pitch = (int8_t)spitch;
                            n++;
                        }
                    }
                }
                else if (pos_changed)
                {
                    if (n < max_pkts)
                    {
                        tracker_packet_clear(&out_pkts[n]);
                        out_pkts[n].kind = TRACKER_PKT_S15;
                        out_pkts[n].id = e->id;
                        out_pkts[n].x = (int8_t)dx;
                        out_pkts[n].y = (int8_t)dy;
                        out_pkts[n].z = (int8_t)dz;
                        n++;
                    }
                }
                else if (rot_changed)
                {
                    if (n < max_pkts)
                    {
                        tracker_packet_clear(&out_pkts[n]);
                        out_pkts[n].kind = TRACKER_PKT_S16;
                        out_pkts[n].id = e->id;
                        out_pkts[n].yaw = (int8_t)syaw;
                        out_pkts[n].pitch = (int8_t)spitch;
                        n++;
                    }
                }
            }
            else
            {
                e->ticks_since_forced_teleport = 0;
                if (n < max_pkts)
                {
                    tracker_packet_clear(&out_pkts[n]);
                    out_pkts[n].kind = TRACKER_PKT_S18;
                    out_pkts[n].id = e->id;
                    out_pkts[n].x = sx;
                    out_pkts[n].y = sy;
                    out_pkts[n].z = sz;
                    out_pkts[n].yaw = (int8_t)syaw;
                    out_pkts[n].pitch = (int8_t)spitch;
                    n++;
                }
            }
        }

        if (e->send_velocity_updates)
        {
            double vdx = e->motion_x - e->last_motion_x;
            double vdy = e->motion_y - e->last_motion_y;
            double vdz = e->motion_z - e->last_motion_z;
            double dist_sq = vdx * vdx + vdy * vdy + vdz * vdz;
            if (dist_sq > 0.0004 || (dist_sq > 0.0 && e->motion_x == 0.0 && e->motion_y == 0.0 && e->motion_z == 0.0))
            {
                e->last_motion_x = e->motion_x;
                e->last_motion_y = e->motion_y;
                e->last_motion_z = e->motion_z;
                if (n < max_pkts)
                {
                    tracker_packet_clear(&out_pkts[n]);
                    out_pkts[n].kind = TRACKER_PKT_S12;
                    out_pkts[n].id = e->id;
                    out_pkts[n].mx = (int)(e->motion_x * 8000.0);
                    out_pkts[n].my = (int)(e->motion_y * 8000.0);
                    out_pkts[n].mz = (int)(e->motion_z * 8000.0);
                    n++;
                }
            }
        }

        if (pos_changed)
        {
            e->last_scaled_x = sx;
            e->last_scaled_y = sy;
            e->last_scaled_z = sz;
        }
        if (rot_changed)
        {
            e->last_yaw = syaw;
            e->last_pitch = spitch;
        }
        e->riding_entity = false;

        int head_yaw = mh_floor(e->head_yaw * 256.0f / 360.0f);
        if (abs(head_yaw - e->last_head_yaw) >= 4)
        {
            if (n < max_pkts)
            {
                tracker_packet_clear(&out_pkts[n]);
                out_pkts[n].kind = TRACKER_PKT_S19;
                out_pkts[n].id = e->id;
                out_pkts[n].head_yaw = (int8_t)head_yaw;
                n++;
            }
            e->last_head_yaw = head_yaw;
        }
        e->is_airborne = false;
    }
    ++e->ticks;

    /* EntityTrackerEntry's tail: every tick, whatever the cadence, an
     * entity whose velocityChanged flag is on answers with an S12. The
     * flag carries Entity.setBeenAttacked, which a player's sword hit on a
     * fireball sets; the fireball's tracker entry has sendVelocityUpdates
     * false, so this is the only S12 path it has. */
    if (e->velocity_changed)
    {
        if (n < max_pkts)
        {
            tracker_packet_clear(&out_pkts[n]);
            out_pkts[n].kind = TRACKER_PKT_S12;
            out_pkts[n].id = e->id;
            out_pkts[n].mx = s12_scaled(e->motion_x);
            out_pkts[n].my = s12_scaled(e->motion_y);
            out_pkts[n].mz = s12_scaled(e->motion_z);
            n++;
        }
        e->velocity_changed = false;
    }
    return n;
}
