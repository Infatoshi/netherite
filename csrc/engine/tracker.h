#ifndef NETHERITE_TRACKER_H
#define NETHERITE_TRACKER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

enum tracker_pkt_kind {
    TRACKER_PKT_S0D,        /* Collect Item */
    TRACKER_PKT_S0E,        /* Spawn Object */
    TRACKER_PKT_S0F,        /* Spawn Mob */
    TRACKER_PKT_S11,        /* Spawn Experience Orb */
    TRACKER_PKT_S12,        /* Entity Velocity */
    TRACKER_PKT_S13,        /* Destroy Entities */
    TRACKER_PKT_S15,        /* Entity Rel Move */
    TRACKER_PKT_S16,        /* Entity Look */
    TRACKER_PKT_S17,        /* Entity Look Move */
    TRACKER_PKT_S18,        /* Entity Teleport */
    TRACKER_PKT_S19,        /* Entity Head Look */
    TRACKER_PKT_S1C,        /* Entity Metadata */
    TRACKER_PKT_S1B,        /* Entity Attach */
    TRACKER_PKT_S04,        /* Entity Equipment */
    TRACKER_PKT_S20,        /* Entity Properties */
    TRACKER_PKT_S22,        /* Multi Block Change */
    TRACKER_PKT_S23,        /* Block Change */
    TRACKER_PKT_S26,        /* Map Chunk Bulk */
    TRACKER_PKT_S21,        /* Chunk Data */
    TRACKER_PKT_S28,        /* Effect */
    TRACKER_PKT_S1A,        /* Entity Status 3: onDeath's setEntityState */
    TRACKER_PKT_OTHER
};

struct tracker_packet {
    enum tracker_pkt_kind kind;
    int id;
    int type;                 /* S0E, S0F */
    int data;                 /* S0E; S0F and S1C: DataWatcher 16 */
    int flags0, kindw;        /* S0F and S1C: DataWatcher 0, and the kind's own values (combat.c copy_kindw) */
    int x, y, z;              /* scaled or relative dx, dy, dz */
    int yaw, pitch, head_yaw; /* byte angles */
    int mx, my, mz;           /* scaled velocity * 8000 */
    int num_ids;              /* S13 */
    int slot;                 /* S04 */
    int leash;                /* S1B */
    int vehicle_id;           /* S1B */
    int effect;               /* S28 */
    int num_chunks;           /* S26 */
    int block, meta;          /* S23 */
    int num_records;          /* S22 */
    /* the arrays, last: read only up to their counts (tracker_packet_copy) */
    int ids[127];             /* S13 (EntityPlayerMP.onUpdate sends at most 127 ids a packet) */
    int16_t chunk_x[512];      /* S26 */
    int16_t chunk_z[512];      /* S26 */
    int rx[64], ry[64], rz[64], rblock[64], rmeta[64]; /* S22 */
};

/* The entity class an entry was made for (tracker_class_name its Java
 * name, which the phase digests hash): the four the trackers make, the
 * ones addEntityToTracker gives their own range and frequency, and any
 * other a test hands tracker_add. */
enum tracker_class {
    TRK_OBJECT, TRK_LIVING, TRK_SMALL_FIREBALL, TRK_LARGE_FIREBALL, TRK_FIREBALL, TRK_PLAYER_MP, TRK_FISH_HOOK,
    TRK_ARROW, TRK_ITEM, TRK_FALLING_BLOCK, TRK_XP_ORB, TRK_OTHER
};
const char *tracker_class_name(int cls);
/* The class of a Java class name: TRK_OTHER for one of none of the four. */
int tracker_class_of(const char *name);

/* A packet's scalar fields, before its arrays: a packet of none of the
 * three array kinds is these bytes (3.9 KB were cleared and copied for each
 * move packet; lane/villscale) */
#define TRACKER_PKT_HEAD offsetof(struct tracker_packet, ids)
static inline void tracker_packet_clear(struct tracker_packet *p)
{
    memset(p, 0, TRACKER_PKT_HEAD);
}
/* the head and each array up to its count; an array's entries past its
 * count are never read */
static inline void tracker_packet_copy(struct tracker_packet *d, const struct tracker_packet *s)
{
    memcpy(d, s, TRACKER_PKT_HEAD);
    if (s->num_ids > 0) memcpy(d->ids, s->ids, (size_t)(s->num_ids < 127 ? s->num_ids : 127) * sizeof s->ids[0]);
    if (s->num_chunks > 0)
    {
        size_t n = (size_t)(s->num_chunks < 512 ? s->num_chunks : 512);
        memcpy(d->chunk_x, s->chunk_x, n * sizeof s->chunk_x[0]);
        memcpy(d->chunk_z, s->chunk_z, n * sizeof s->chunk_z[0]);
    }
    if (s->num_records > 0)
    {
        size_t n = (size_t)(s->num_records < 64 ? s->num_records : 64) * sizeof s->rx[0];
        memcpy(d->rx, s->rx, n);
        memcpy(d->ry, s->ry, n);
        memcpy(d->rz, s->rz, n);
        memcpy(d->rblock, s->rblock, n);
        memcpy(d->rmeta, s->rmeta, n);
    }
}

struct tracker_entry {
    int id;
    int dim;                  /* the world whose EntityTracker holds it */
    int cls;                  /* enum tracker_class */
    double x, y, z;
    double motion_x, motion_y, motion_z;
    float yaw, pitch, head_yaw;

    int last_scaled_x, last_scaled_y, last_scaled_z;
    int last_yaw, last_pitch, last_head_yaw;
    double last_motion_x, last_motion_y, last_motion_z;

    int ticks;
    int ticks_since_forced_teleport;
    int update_frequency;
    int tracking_range;
    bool send_velocity_updates;
    bool riding_entity;       /* EntityTrackerEntry.ridingEntity: riding at the last update */
    bool is_riding;           /* the entity's ridingEntity != null now (the caller's) */
    int attached_id;          /* field_85178_v: the vehicle the last S1B named, 0 none */
    bool is_airborne;
    bool watch_initialized;
    bool watched;
    double watch_x, watch_y, watch_z;
    bool velocity_changed;    /* Entity.velocityChanged, setBeenAttacked: the
                               * tick's tail answers it with an S12 (a fireball
                               * deflect, sendVelocityUpdates false) */
    bool is_fireball;         /* the S0E-first fireball entry (type 63) */
    int s0e_type;             /* S0E type, 0 = spawn as a living (S0F) */
    int s0e_data;             /* S0E data (shooter id, 0 when none) */
    double s0e_accel_x, s0e_accel_y, s0e_accel_z; /* S0E: the accel fields
                                                   * at the first spawn */
    /* Entity.myEntitySize's ordinal + 1 (1..6, from setSize's width); 0 is
     * the constructor's SIZE_2 */
    int size;
    /* DataWatcher.hasChanges(): a watched value changed since the last
     * update block (whose func_111190_b takes the changes) */
    bool dw_dirty;
    int sent_dw16;            /* DataWatcher 16 as the watcher last got it */
    bool sent_dying;          /* the watcher has the living's health at 0 */
    bool before_player;       /* ahead of the player in the entity pass (at the last tracker update) */
    int sent_flags0, sent_kindw;  /* DataWatcher 0 and copy_kindw's, the same */
    /* the watched values last seen (DataWatcher 0, 1, 6, 9 and 16) */
    /* the DataWatcher values whose change DataWatcher.hasChanges reports:
     * the living's own, then each kind's (the creeper's 16-18, the
     * enderman's 16-18, the zombie's 12-14, the witch's 21, the potion
     * colour 7) folded into kind */
    struct tracker_watched { float health; int flags0, air, arrows, dw16, kind, age; } dw;
};

/* EntityTracker.trackedEntities: one entry per entity it tracks, at most
 * one per slot of the living world's entity list (living.h AN_MAX_ENTITIES,
 * which the livings and the mob pool's projectiles share); past it the
 * program stops (tracker_add). A village's or a desert's falling sand alone
 * passes 512 entries. */
#define TRACKER_MAX_ENTRIES 4096

#define TRACKER_INDEX (2 * TRACKER_MAX_ENTRIES)

struct tracker {
    struct tracker_entry entries[TRACKER_MAX_ENTRIES];
    int nentries;
    /* the entries by id, linear probing (tracker_find), kept by tracker_add
     * and tracker_remove: per slot the id and the entry's index + 1 (0
     * empty), side by side so that a probe reads one line and no entry
     * (lane/mobtick, lane/coldaudit), over the first index_mask + 1 slots:
     * a power of two, 64 or more and at least twice the entries (grown with
     * them; 0 before the first entry reads as 63), so a few dozen entries
     * probe a table of a few lines rather than 48 KB */
    struct tracker_slot { int32_t id; uint16_t entry; uint16_t pad_; } index[TRACKER_INDEX];
    uint32_t index_mask;

    /* Negative check hook: send S15 instead of S17 */
    bool negative_check_s15_for_s17;
};

void tracker_init(struct tracker *tr);
struct tracker_entry *tracker_add(struct tracker *tr, int id, int cls, double x, double y, double z, float yaw, float pitch, float head_yaw);
struct tracker_entry *tracker_find(struct tracker *tr, int id);
void tracker_remove(struct tracker *tr, int id);

/* Multiplies by 32 and rounds, exactly matching Entity.EnumEntitySize.SIZE_2 */
int tracker_multiply_by_32_and_round(double p);
/* EnumEntitySize.multiplyBy32AndRound for size 1..6 (0 is SIZE_2) */
int tracker_mul32_size(double p, int size);
/* Entity.setSize's EnumEntitySize for a width, as tracker_entry.size */
int tracker_entity_size(float width);

/* Ticks one entity tracker entry, emitting any packets generated into out_pkts.
 * Returns number of packets emitted (up to max_pkts). */
int tracker_entry_tick(struct tracker *tr, struct tracker_entry *e, struct tracker_packet *out_pkts, int max_pkts);

#endif
