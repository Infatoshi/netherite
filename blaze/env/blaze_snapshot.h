/* blaze_snapshot.h - the .bsnp state-snapshot format, shared between the
 * writer (game/rl_mode.c, "snapshot":"<path>" action key + --snapshot-in
 * restore) and the batched-env reader (blaze/env). The CODE is the canonical
 * format; keep this header in sync with BOTH sides or round-trips break.
 *
 * File layout (little-endian, packed):
 *   RlSnapHead | n_items x RlSnapItem | rnx*rny*rnz u16 packed (id<<4)|meta
 *   (index (ix*rny+iy)*rnz+iz) | u32 ncoal | ncoal x (i32 wx,wy,wz)
 *   | rnx*rny*rnz u8 packed light (sky<<4)|block       [version >= 2]
 *   | u32 n_mobs | n_mobs x RlSnapMob                   [version >= 3]
 * v1/v2 files load with n_mobs = 0. New writes use version 3.
 * Player pose/box are WINDOW-LOCAL doubles plus the ox/oz origin: restoring
 * local+origin reproduces the exact double bits (world = local + origin
 * rounds, so world-coord storage would lose low mantissa bits; the box is
 * stored in full for the same reason - never rebuild it from pos). Region is
 * 64x128x64 world blocks at rx0 = floor(world px)-32, rz0-32, ry0 = 0. The
 * coal list is a convenience mirror (derivable from the region). */
#ifndef BLAZE_SNAPSHOT_H
#define BLAZE_SNAPSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#define BLAZE_SNAP_MAX_ITEMS 48   /* == GM_LIVE_MAX (game/live_sim.h) */
#define BLAZE_SNAP_MAX_CONT  64   /* container-list cap (ids 58/61/62); more
                                   * than this in one region -> ncont = -1 and
                                   * consumers fall back to the full window
                                   * scan (value-identical, just slow) */
#define BLAZE_SNAP_MAX_MOBS  96   /* == EW_MAX_ENTITIES (ew_entity_store.h:21) */
#define BLAZE_SNAP_PATH_CAP  48   /* magma det_nav_* cap (mob_live.h:90) */

#define BLAZE_SNAP_VERSION_LIGHT 2  /* packed light plane after coal */
#define BLAZE_SNAP_VERSION 3        /* + per-mob trailer after light */
#pragma pack(push, 1)
typedef struct {
    char magic[4];                 /* "BSNP" */
    unsigned version;              /* BLAZE_SNAP_VERSION */
    long long seed, tick;
    int ox, oz;                    /* physics window origin (world blocks) */
    double px, py, pz;             /* player pos, window-local */
    double box[6];                 /* minX,minY,minZ,maxX,maxY,maxZ, local */
    float yaw, pitch;
    double mx, my, mz;
    int on_ground, collided_h, collided_v, is_collided;
    float fall_distance;
    int sprinting, sprint_toggle_timer, jump_factor_sprint, jump_ticks;
    float prev_move_forward;
    int prev_sneak;
    float health;                  /* vitals (PvStats) */
    int food;
    float saturation, exhaustion;
    int food_timer;
    float dig_progress;            /* player_ctl statics (GmPlayerCtlSnap) */
    int dig_hx, dig_hy, dig_hz;    /* window-local */
    int dig_hitting, dig_delay, atk_prev, rc_delay, use_prev;
    int hurt_vel_reset;
    double server_motion_x, server_motion_z;
    int container, container_wx, container_wy, container_wz;
    int world_dirty;               /* rl_mode scan-dirty countdown; restored
                                    * so cache-rebuild cadence (and any world
                                    * change it would catch) matches the
                                    * continued run */
    int hotbar_sel;                /* inv.current_item */
    int inv[37][3];                /* 36 main + offhand: item,count,meta */
    unsigned n_items;              /* live item entities that follow */
    int rx0, ry0, rz0, rnx, rny, rnz;
} RlSnapHead;
typedef struct {
    double x, y, z, mx, my, mz;    /* world coords */
    int item, count, meta, age, pickup_delay, lifespan, on_ground;
} RlSnapItem;
/* One occupied living slot. Field cites: magma/game/mob_live.h (and
 * EwStore via entity_hostile_spine.h). Mob pose is WORLD coords, unlike
 * the player window-local head. v2 files omit this trailer -> n_mobs=0. */
typedef struct RlSnapMob {
    int slot, id;                  /* EwStore slot + id; slot 0 reserved */
    int type;                      /* EwStore.type / EW_TYPE_* */
    int alive;                     /* EwStore.alive */
    int persist;                   /* det_persist (EntityLiving.persistenceRequired) */
    double x, y, z;                /* EwStore x/y/z */
    float yaw, pitch, yaw_body;    /* yaw; passive_head_pitch; passive_render_yaw */
    double mx, my, mz;             /* EwStore vx/vy/vz */
    int on_ground;                 /* EwStore.on_ground */
    float health;                  /* EwStore.health */
    int hurt_time, death_time;     /* EntityLivingBase.hurtTime / deathTime */
    unsigned task_bits;            /* passive_tasks */
    unsigned target_tasks;         /* det_target_tasks */
    double wander_x, wander_z;     /* passive_idle_x/z */
    int panic;                     /* panic_ticks */
    int target_idx;                /* det_has_target (0 none, 1 player slot 0) */
    int see_time;                  /* det_see_time */
    int stime;                     /* det_strafe_time */
    int melee_delay;               /* det_melee_delay */
    int bow_attack_time;           /* det_bow_attack_time */
    int attack_time;               /* EwStore.attack_time */
    int swell;                     /* creeper_fuse */
    int anger;                     /* anger */
    short path_x[BLAZE_SNAP_PATH_CAP];
    short path_y[BLAZE_SNAP_PATH_CAP];
    short path_z[BLAZE_SNAP_PATH_CAP];
    unsigned char path_n, path_i;  /* det_nav_n / det_nav_i */
    int nav_ticks;                 /* det_nav_ticks (PathNavigate.totalTicks) */
    int nav_stuck_at;              /* det_nav_stuck_at (ticksAtLastPos) */
    double nav_stuck_x, nav_stuck_y, nav_stuck_z; /* lastPosCheck */
    unsigned char box_on;          /* det_box_on */
    double box_minx, box_miny, box_minz, box_maxx, box_maxy, box_maxz;
    unsigned long long seed48;     /* ent_jr_seed, 48-bit LCG cursor */
    unsigned char have_gauss;      /* ent_jr_have_gauss */
    double gauss;                  /* ent_jr_gauss */
} RlSnapMob;
#pragma pack(pop)

typedef char RlSnapMob_must_be_544_bytes
    [(sizeof(RlSnapMob) == 544) ? 1 : -1];

/* ---- host-side loader (blaze/env/blaze_snapshot.c; NOT linked into the
 * game binary - rl_mode.c uses only the structs above) ---- */
typedef struct {
    RlSnapHead     head;
    RlSnapItem     items[BLAZE_SNAP_MAX_ITEMS];
    unsigned short *cells;         /* malloc'd rnx*rny*rnz packed states */
    unsigned char  *light;         /* v2: malloc'd packed (sky<<4)|block;
                                    * NULL for legacy v1 snapshots */
    int            *coal;          /* malloc'd ncoal x 3 (wx,wy,wz) */
    unsigned       ncoal;
    int            *xy_off;        /* malloc'd rnx*rny+1 CSR offsets into coal
                                    * by region (ix,iy) column; NULL when the
                                    * list is not writer-ordered (full-scan
                                    * fallback) */
    int            has_liquid;     /* any id 8..11 in the region */
    int            *cont;          /* malloc'd container cells (wx,wy,wz x
                                    * ncont; ids 58/61/62), derived from the
                                    * region at load - NOT part of .bsnp */
    int            ncont;          /* -1 = > BLAZE_SNAP_MAX_CONT (fallback) */
    unsigned       n_mobs;         /* v3: occupied living slots; 0 on v1/v2 */
    RlSnapMob      mobs[BLAZE_SNAP_MAX_MOBS];
} CuSnapshot;

/* Load a .bsnp into *out (mallocs cells/coal; blaze_snapshot_free releases).
 * Returns 1 on success, else 0 with a message in err.
 * no_ore_xy != 0 skips the ore spatial index (legacy full-scan A/B; was
 * BLAZE_NO_ORE_XY=1). Pass 0 for the normal path. */
int  blaze_snapshot_load(const char *path, CuSnapshot *out,
                         char *err, int err_cap, int no_ore_xy);
int  blaze_snapshot_write(const char *path, const CuSnapshot *s,
                          char *err, int err_cap);
void blaze_snapshot_free(CuSnapshot *s);

/* Build the CSR (ix,iy)-column index over a static ore list: off[ix*rny+iy]
 * .. off[ix*rny+iy+1] spans the ores of region column (ix,iy) in ORIGINAL
 * list order. Valid ONLY when the list is strictly lex-ascending in region
 * coords (x,y,z) and fully in-region - exactly the order rl_snapshot_write
 * emits - so walking columns ascending reproduces the full scan's ore-index
 * accept order byte-for-byte. Returns 1 and fills off[rnx*rny+1] on success;
 * 0 when the list violates that order (consumer stays on the full scan).
 * Host-side, init-time only. */
int  blaze_build_ore_xy(const int *ore, int nore,
                        int rx0, int ry0, int rz0,
                        int rnx, int rny, int rnz, int *off);

/* Scan packed region cells for container ids (58/61/62) and emit their world
 * coords into out[cap*3]. Returns the count, or -1 when the region holds
 * more than cap (consumers fall back to the full window scan). Host-side,
 * init-time only. */
int  blaze_build_containers(const unsigned short *cells,
                            int rx0, int ry0, int rz0,
                            int rnx, int rny, int rnz, int *out, int cap);

#include "../core/port_parity.h"
#if defined(__CUDACC__)
#define BLAZE_SNAP_HD __host__ __device__
#else
#define BLAZE_SNAP_HD
#endif

/* Canonical BP_MOBS digest over packed records (slot-ascending writer order). */
BLAZE_SNAP_HD static inline uint64_t blaze_snap_hash_one_mob(
    uint64_t h, const RlSnapMob *m) {
    unsigned k;
    h = bp_hash_i32(h, m->slot);
    h = bp_hash_i32(h, m->id);
    h = bp_hash_i32(h, m->type);
    h = bp_hash_i32(h, m->alive);
    h = bp_hash_i32(h, m->persist);
    h = bp_hash_double(h, m->x);
    h = bp_hash_double(h, m->y);
    h = bp_hash_double(h, m->z);
    h = bp_hash_float(h, m->yaw);
    h = bp_hash_float(h, m->pitch);
    h = bp_hash_float(h, m->yaw_body);
    h = bp_hash_double(h, m->mx);
    h = bp_hash_double(h, m->my);
    h = bp_hash_double(h, m->mz);
    h = bp_hash_i32(h, m->on_ground);
    h = bp_hash_float(h, m->health);
    h = bp_hash_i32(h, m->hurt_time);
    h = bp_hash_i32(h, m->death_time);
    h = bp_hash_u32(h, m->task_bits);
    h = bp_hash_u32(h, m->target_tasks);
    h = bp_hash_double(h, m->wander_x);
    h = bp_hash_double(h, m->wander_z);
    h = bp_hash_i32(h, m->panic);
    h = bp_hash_i32(h, m->target_idx);
    h = bp_hash_i32(h, m->see_time);
    h = bp_hash_i32(h, m->stime);
    h = bp_hash_i32(h, m->melee_delay);
    h = bp_hash_i32(h, m->bow_attack_time);
    h = bp_hash_i32(h, m->attack_time);
    h = bp_hash_i32(h, m->swell);
    h = bp_hash_i32(h, m->anger);
    for (k = 0; k < BLAZE_SNAP_PATH_CAP; ++k) {
        h = bp_hash_u16(h, (uint16_t)m->path_x[k]);
        h = bp_hash_u16(h, (uint16_t)m->path_y[k]);
        h = bp_hash_u16(h, (uint16_t)m->path_z[k]);
    }
    h = bp_hash_u8(h, m->path_n);
    h = bp_hash_u8(h, m->path_i);
    h = bp_hash_i32(h, m->nav_ticks);
    h = bp_hash_i32(h, m->nav_stuck_at);
    h = bp_hash_double(h, m->nav_stuck_x);
    h = bp_hash_double(h, m->nav_stuck_y);
    h = bp_hash_double(h, m->nav_stuck_z);
    h = bp_hash_i32(h, (int32_t)m->box_on);
    h = bp_hash_double(h, m->box_minx);
    h = bp_hash_double(h, m->box_miny);
    h = bp_hash_double(h, m->box_minz);
    h = bp_hash_double(h, m->box_maxx);
    h = bp_hash_double(h, m->box_maxy);
    h = bp_hash_double(h, m->box_maxz);
    h = bp_hash_u64(h, m->seed48);
    h = bp_hash_i32(h, (int32_t)m->have_gauss);
    return bp_hash_double(h, m->gauss);
}

BLAZE_SNAP_HD static inline uint64_t blaze_snap_mobs_digest(
    const RlSnapMob *mobs, unsigned n) {
    uint64_t h = bp_hash_begin();
    unsigned i;
    h = bp_hash_u32(h, UINT32_C(0x33424f4d)); /* "MOB3" */
    h = bp_hash_i32(h, (int32_t)n);
    for (i = 0; i < n; ++i)
        h = blaze_snap_hash_one_mob(h, &mobs[i]);
    return h;
}

#undef BLAZE_SNAP_HD

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_SNAPSHOT_H */
