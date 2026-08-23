/* blaze_snapshot.h - the .bsnp state-snapshot format, shared between the
 * writer (game/rl_mode.c, "snapshot":"<path>" action key + --snapshot-in
 * restore) and the batched-env reader (blaze/env). The CODE is the canonical
 * format; keep this header in sync with BOTH sides or round-trips break.
 *
 * File layout (little-endian, packed):
 *   RlSnapHead | n_items x RlSnapItem | rnx*rny*rnz u16 packed (id<<4)|meta
 *   (index (ix*rny+iy)*rnz+iz) | u32 ncoal | ncoal x (i32 wx,wy,wz)
 *   | rnx*rny*rnz u8 packed light (sky<<4)|block       [version >= 2]
 *   | u32 n_mobs | n_mobs x on-disk mob record          [version >= 3]
 *     v3-v6: BLAZE_SNAP_MOB_SIZE_V6 (544 bytes)
 *     v7+:   BLAZE_SNAP_MOB_SIZE_V7 (572 bytes through teleport_time).
 *     In-memory RlSnapMob is 604: witch extras sit after teleport_time
 *     (v7 disk 572 zero-extends them). v10 disk is 604 with sidecars.
 *   | u32 n_orbs | n_orbs x RlSnapOrb                   [version >= 4]
 *   | u64 world_rand_seed (48-bit JavaRandom cursor)    [version >= 5]
 *   | i32 update_lcg (World.updateLCG)                  [version >= 6]
 *   | rnx*rnz u8 biome plane (one id per x,z column)    [version >= 8]
 *   | i32 player_fire, i32 player_air                   [version >= 9]
 *   | v10 trailer: world clock + rt mutations            [version >= 10]
 *     i64 ww_total, ww_world; i32 rain_time, thunder_time,
 *     raining, thundering; u64 ww_rand_seed48; u32 rt_mutations.
 *     On-disk mob records grow to BLAZE_SNAP_MOB_SIZE_V10 (604) and
 *     carry repath/despawn/fire sidecars. v9 loads those as 0.
 *     After clock: projectiles, falls, TE/player/fluid/boat/explosion pose,
 *     then RlSnapV10Xtra (xp pickups, spawn RNGs, blast counters, dead,
 *     boat extras, enchant payload). v9 loads xtra as 0.
 * Loader reads v7, v8, v9, and v10 on this lane. New writes use version 10.
 * v1/v2 files load with n_mobs = 0. v3 loads with n_orbs = 0.
 * v4 loads with world_rand_seed = jrand_set(0) internal cursor.
 * v5 loads with update_lcg = 0.
 * v6 loads enderman extras as 0.
 * v7 loads biome plane = plains (id 1) so old fixtures keep spawn/freeze
 * semantics (HS_BIOME / rt_live_biome used to hardcode plains), and
 * player fire=0 air=300.
 * v8 loads player fire=0 air=300 (Entity.java:256 AIR default).
 * v9 loads v10 clock from ww_init(seed) and rt_mutations=0.
 * Biome index is ix * rnz + iz (ix = wx - rx0, iz = wz - rz0). Java
 * Chunk.blockBiomeArray is (z&15)<<4 | (x&15) per chunk (Chunk.java:1273-1278);
 * the magma writer copies LChunk.biome[x + z*16] (light.c) into this plane.
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
#define BLAZE_SNAP_MAX_CONT  64   /* container-list cap (ids 58/61/62/54); more
                                   * than this in one region -> ncont = -1 and
                                   * consumers fall back to the full window
                                   * scan (value-identical, just slow) */
#define BLAZE_SNAP_MAX_MOBS  96   /* == EW_MAX_ENTITIES (ew_entity_store.h:21) */
#define BLAZE_SNAP_PATH_CAP  48   /* magma det_nav_* cap (mob_live.h:90) */
#define BLAZE_SNAP_MAX_ORBS  64   /* == GM_XP_ORBS (mob_live.h:10) */

#define BLAZE_SNAP_VERSION_LIGHT 2  /* packed light plane after coal */
#define BLAZE_SNAP_VERSION_MOBS  3  /* + per-mob trailer after light */
#define BLAZE_SNAP_VERSION_ORBS  4  /* + XP-orb trailer after mobs */
#define BLAZE_SNAP_VERSION_WORLD_RAND 5 /* + World.rand 48-bit cursor after orbs */
#define BLAZE_SNAP_VERSION_UPDATE_LCG 6 /* + World.updateLCG after world_rand */
#define BLAZE_SNAP_VERSION_ENDER 7      /* + enderman fields on RlSnapMob */
#define BLAZE_SNAP_VERSION_BIOME 8      /* + rnx*rnz u8 column biome plane */
#define BLAZE_SNAP_VERSION_HAZARDS 9    /* + player fire ticks and air */
#define BLAZE_SNAP_VERSION_RESUME 10    /* + clock/rt mutations + mob sidecars */
#define BLAZE_SNAP_VERSION 10           /* v10 on this lane; not a final pin */
#define BLAZE_SNAP_MOB_SIZE_V6 544      /* packed RlSnapMob through v6 */
#define BLAZE_SNAP_MOB_SIZE_V7 572      /* packed through teleport_time */
#define BLAZE_SNAP_MOB_SIZE_V10 604     /* packed through fire_ticks */
#define BLAZE_SNAP_BIOME_PLAINS 1       /* Biomes.PLAINS; v7 load default */
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
    int screaming;                 /* EntityEnderman SCREAMING */
    int carried;                   /* carried block id; 0 = none */
    int carried_meta;
    int target_change_time;        /* EntityEnderman.targetChangeTime */
    int ticks_existed;             /* Entity.ticksExisted */
    int find_aggro;                /* AIFindPlayer.aggroTime */
    int teleport_time;             /* AIFindPlayer.teleportTime */
    /* In-memory extras. On-disk v7 stays BLAZE_SNAP_MOB_SIZE_V7 (572);
     * the loader zero-extends these without a version bump. v10 writes
     * the full 604-byte record including the sidecars below. */
    int witch_attack_timer;        /* EntityWitch.witchAttackTimer */
    int witch_drink;               /* 0 none; else pending drink kind */
    int effect_id;                 /* Potion.getIdFromPotion; 0 = none */
    int effect_duration;
    int effect_amplifier;
    int repath_timer;              /* EwStore.repath_timer / blaze mob_repath */
    int despawn_ticks;             /* EntityMob despawn >32 blocks */
    int fire_ticks;                /* Entity.fire / daylight burn */
} RlSnapMob;
#define BLAZE_SNAP_MAX_PROJ 32
#define BLAZE_SNAP_MAX_FALL 48
#define BLAZE_SNAP_MAX_FALL_UPD 128
#define BLAZE_SNAP_MAX_FURN 16
#define BLAZE_SNAP_MAX_CHEST 64
#define BLAZE_SNAP_CHEST_SLOTS 27
#define BLAZE_SNAP_FLUID_REGS 4
#pragma pack(push, 1)
typedef struct RlSnapProj {
    int active, type, age;
    double x, y, z, vx, vy, vz;
    int in_ground, shake, pickup, ground_ticks;
} RlSnapProj;
typedef struct RlSnapFall {
    int active, type;
    double x, y, z, mx, my, mz;
    int on_ground, age, item, count, meta, pickup_delay, lifespan;
} RlSnapFall;
typedef struct RlSnapFallUpdate {
    int active, x, y, z, block_id;
    long long due_tick;
} RlSnapFallUpdate;
typedef struct RlSnapFallLanding {
    int active, x, y, z, block_id, block_meta;
    long long due_tick;
} RlSnapFallLanding;
typedef struct RlSnapFurnace {
    int active, wx, wy, wz;
    int in_item, in_count, in_meta;
    int fuel_item, fuel_count, fuel_meta;
    int out_item, out_count, out_meta;
    int burn_time, current_burn_time, cook_time, total_cook;
} RlSnapFurnace;
typedef struct RlSnapEnch {
    int n;
    short id[8];
    short level[8];
} RlSnapEnch;
typedef struct RlSnapChest {
    int active, wx, wy, wz, num_using;
    int slot[BLAZE_SNAP_CHEST_SLOTS][3];
    RlSnapEnch slot_ench[BLAZE_SNAP_CHEST_SLOTS];
} RlSnapChest;
typedef struct RlSnapFluidReg {
    int active, x0, y0, z0, x1, y1, z1, has_water, quiet_steps;
} RlSnapFluidReg;
/* Packed after explosion pose. v9 has none; v10 on this lane always writes it. */
typedef struct RlSnapV10Xtra {
    unsigned xp_pickups;
    int next_orb_id;
    int next_mob_id;
    unsigned long long spawn_world_seed48;
    unsigned long long spawn_math_seed48;
    unsigned long long spawn_shuffle_seed48;
    unsigned parity_ex_blasts;
    unsigned parity_ex_destroyed;
    unsigned parity_ex_drop_n;
    unsigned long long parity_ex_drop_ids;
    float parity_ex_damage;
    double parity_ex_kb_x, parity_ex_kb_y, parity_ex_kb_z;
    unsigned long long parity_ex_rays;
    double parity_ex_last_x, parity_ex_last_y, parity_ex_last_z;
    float parity_ex_last_size;
    int player_dead;
    int death_screen_ticks;
    int player_hurt_resistant;
    int player_attack_cooldown;
    float player_last_damage;
    float boat_delta_rot[BLAZE_SNAP_MAX_MOBS];
    float boat_glide[BLAZE_SNAP_MAX_MOBS];
    RlSnapEnch inv_ench[37];
    RlSnapEnch armor_ench[4];
    RlSnapEnch craft_ench[9];
    RlSnapEnch cursor_ench;
    /* Live blaze sidecars (digest still hashes packed repath which can lag). */
    int sidecar_repath[BLAZE_SNAP_MAX_MOBS];
    int sidecar_despawn[BLAZE_SNAP_MAX_MOBS];
    int sidecar_fire[BLAZE_SNAP_MAX_MOBS];
    /* Magma EwStore AI waypoint (not in RlSnapMob). Packed index order. */
    unsigned ew_ai_state[BLAZE_SNAP_MAX_MOBS];
    unsigned ew_path_len[BLAZE_SNAP_MAX_MOBS];
    double ew_path_tx[BLAZE_SNAP_MAX_MOBS];
    double ew_path_ty[BLAZE_SNAP_MAX_MOBS];
    double ew_path_tz[BLAZE_SNAP_MAX_MOBS];
    double look_px, look_py, look_pz;
    int look_have;
    int mob_tick;
    int entity_age[BLAZE_SNAP_MAX_MOBS];
} RlSnapV10Xtra;
#pragma pack(pop)

/* One live XP orb. World coords. v3 files omit this trailer -> n_orbs=0. */
typedef struct RlSnapOrb {
    double x, y, z, mx, my, mz;
    int on_ground;
    int xpOrbAge;
    int delayBeforeCanPickup;
    int xpValue;
    int eid;
    int xpColor;
    int xpTargetColor;
    int has_closest;
    int dead;
} RlSnapOrb;
#pragma pack(pop)

typedef char RlSnapMob_must_be_604_bytes
    [(sizeof(RlSnapMob) == 604) ? 1 : -1];
typedef char RlSnapMob_v7_disk_is_572
    [(BLAZE_SNAP_MOB_SIZE_V7 == 572) ? 1 : -1];
typedef char RlSnapMob_v10_disk_is_604
    [(BLAZE_SNAP_MOB_SIZE_V10 == 604) ? 1 : -1];
typedef char RlSnapOrb_must_be_84_bytes
    [(sizeof(RlSnapOrb) == 84) ? 1 : -1];
typedef char RlSnapEnch_must_be_36_bytes
    [(sizeof(RlSnapEnch) == 36) ? 1 : -1];

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
    unsigned       n_orbs;         /* v4: live XP orbs; 0 on v1/v2/v3 */
    RlSnapOrb      orbs[BLAZE_SNAP_MAX_ORBS];
    unsigned long long world_rand_seed; /* v5: JavaRandom internal seed48 */
    int                update_lcg;      /* v6: World.updateLCG; 0 on v<=5 */
    unsigned char      *biome;          /* v8: rnx*rnz column ids; v7 load
                                         * fills plains 1. Index ix*rnz+iz. */
    int                player_fire;     /* v9: Entity.fire; v8 load 0 */
    int                player_air;      /* v9: Entity air; v8 load 300 */
    long long          ww_total_time;   /* v10: WorldInfo.totalTime */
    long long          ww_world_time;   /* v10: WorldInfo.worldTime */
    int                ww_rain_time;
    int                ww_thunder_time;
    int                ww_raining;
    int                ww_thundering;
    unsigned long long ww_rand_seed48;  /* v10: isolated weather JavaRandom */
    unsigned           rt_mutations;    /* v10: BP_RANDOM_TICKS mutation count */
    unsigned           n_proj;
    RlSnapProj         proj[BLAZE_SNAP_MAX_PROJ];
    unsigned           parity_proj_hits;
    unsigned           n_fall;
    RlSnapFall         falls[BLAZE_SNAP_MAX_FALL];
    unsigned           n_fall_upd;
    RlSnapFallUpdate   fall_upd[BLAZE_SNAP_MAX_FALL_UPD];
    unsigned           n_fall_land;
    RlSnapFallLanding  fall_land[BLAZE_SNAP_MAX_FALL];
    unsigned           fall_mutations;
    int                live_ticks;
    unsigned           n_furn;
    RlSnapFurnace      furn[BLAZE_SNAP_MAX_FURN];
    int                active_furnace;
    unsigned           n_chest;
    RlSnapChest        chest[BLAZE_SNAP_MAX_CHEST];
    int                active_chest;
    int                craft[9][3];
    int                cursor[3];
    unsigned           craft_attempts, craft_successes, container_opens;
    int                left_click_counter, eat_ticks, eat_item;
    int                bow_ticks, bow_drawing;
    int                xp_level, xp_total, xp_cooldown;
    float              xp_experience;
    int                armor[4][3];
    int                fluid_dim;
    RlSnapFluidReg     fluid[BLAZE_SNAP_FLUID_REGS];
    unsigned           fluid_mutations;
    int                boat_ride;
    int                explosion_pending, explosion_smoking, explosion_flaming;
    double             explosion_x, explosion_y, explosion_z;
    float              explosion_size;
    RlSnapV10Xtra      xtra;
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
    h = bp_hash_double(h, m->gauss);
    h = bp_hash_i32(h, m->screaming);
    h = bp_hash_i32(h, m->carried);
    h = bp_hash_i32(h, m->carried_meta);
    h = bp_hash_i32(h, m->target_change_time);
    h = bp_hash_i32(h, m->ticks_existed);
    h = bp_hash_i32(h, m->find_aggro);
    h = bp_hash_i32(h, m->teleport_time);
    h = bp_hash_i32(h, m->witch_attack_timer);
    h = bp_hash_i32(h, m->witch_drink);
    h = bp_hash_i32(h, m->effect_id);
    h = bp_hash_i32(h, m->effect_duration);
    h = bp_hash_i32(h, m->effect_amplifier);
    h = bp_hash_i32(h, m->repath_timer);
    h = bp_hash_i32(h, m->despawn_ticks);
    return bp_hash_i32(h, m->fire_ticks);
}

BLAZE_SNAP_HD static inline uint64_t blaze_snap_mobs_digest(
    const RlSnapMob *mobs, unsigned n) {
    uint64_t h = bp_hash_begin();
    unsigned i;
    h = bp_hash_u32(h, UINT32_C(0x34424f4d)); /* "MOB4" sidecars */
    h = bp_hash_i32(h, (int32_t)n);
    for (i = 0; i < n; ++i)
        h = blaze_snap_hash_one_mob(h, &mobs[i]);
    return h;
}

/* Combat extras on BP_MOBS: player i-frames + live item drops. */
BLAZE_SNAP_HD static inline uint64_t blaze_snap_mobs_digest_ext(
    const RlSnapMob *mobs, unsigned n,
    float player_health, int32_t hurt_res, int32_t atk_cd,
    uint64_t items_h, int32_t n_items) {
    uint64_t h = blaze_snap_mobs_digest(mobs, n);
    /* "MBM4": combat extras here; witch + sidecar fields are in hash_one_mob;
     * callers then fold the v8 biome plane via bp_hash_biome_plane. */
    h = bp_hash_u32(h, UINT32_C(0x344d424d)); /* "MBM4" */
    h = bp_hash_float(h, player_health);
    h = bp_hash_i32(h, hurt_res);
    h = bp_hash_i32(h, atk_cd);
    h = bp_hash_i32(h, n_items);
    h = bp_hash_u64(h, items_h);
    return h;
}

#undef BLAZE_SNAP_HD

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_SNAPSHOT_H */
