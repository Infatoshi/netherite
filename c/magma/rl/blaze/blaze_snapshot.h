/* blaze_snapshot.h - the .bsnp state-snapshot format, shared between the
 * writer (game/rl_mode.c, "snapshot":"<path>" action key + --snapshot-in
 * restore) and the batched-env reader (rl/blaze). The CODE is the canonical
 * format; keep this header in sync with BOTH sides or round-trips break.
 *
 * File layout (little-endian, packed):
 *   RlSnapHead | n_items x RlSnapItem | rnx*rny*rnz u16 packed (id<<4)|meta
 *   (index (ix*rny+iy)*rnz+iz) | u32 ncoal | ncoal x (i32 wx,wy,wz)
 *
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

#define BLAZE_FILE_MAX_ITEMS 48   /* v1 .bsnp stores live active entities */
#define BLAZE_SNAP_MAX_ITEMS 80   /* internal capture: 48 active + 32 FIFO */
#define BLAZE_SNAP_MAX_CONT  64   /* container-list cap (ids 58/61/62); more
                                   * than this in one region -> ncont = -1 and
                                   * consumers fall back to the full window
                                   * scan (value-identical, just slow) */

#pragma pack(push, 1)
typedef struct {
    char magic[4];                 /* "BSNP" */
    unsigned version;              /* 1 */
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
#pragma pack(pop)

/* ---- host-side loader (rl/blaze/blaze_snapshot.c; NOT linked into the
 * game binary - rl_mode.c uses only the structs above) ---- */
typedef struct {
    RlSnapHead     head;
    RlSnapItem     items[BLAZE_SNAP_MAX_ITEMS];
    unsigned       nactive;       /* leading active items; remainder overflow */
    unsigned short *cells;         /* malloc'd rnx*rny*rnz packed states */
    int            *coal;          /* malloc'd ncoal x 3 (wx,wy,wz) */
    unsigned       ncoal;
    int            *xz_off;        /* malloc'd rnx*rnz+1 CSR offsets into coal
                                    * by region (ix,iz) column, with y descending
                                    * inside each column to match rl_emit_obs */
    int            has_liquid;     /* any id 8..11 in the region */
    int            *cont;          /* malloc'd container cells (wx,wy,wz x
                                    * ncont; ids 58/61/62), derived from the
                                    * region at load - NOT part of .bsnp */
    int            ncont;          /* -1 = > BLAZE_SNAP_MAX_CONT (fallback) */
} CuSnapshot;

/* Load a .bsnp into *out (mallocs cells/coal; blaze_snapshot_free releases).
 * Returns 1 on success, else 0 with a message in err. */
int  blaze_snapshot_load(const char *path, CuSnapshot *out,
                         char *err, int err_cap);
void blaze_snapshot_free(CuSnapshot *s);

/* Normalize a static ore list to rl_emit_obs scan order (x ascending, z
 * ascending, y descending), then build its (ix,iz)-column CSR. This preserves
 * the real environment's 512-entry coal scratch truncation even for legacy
 * snapshots whose trailing ore list was written in x/y/z order. */
int  blaze_build_ore_xz(int *ore, int nore,
                        int rx0, int ry0, int rz0,
                        int rnx, int rny, int rnz, int *off);

/* Scan packed region cells for container ids (58/61/62) and emit their world
 * coords into out[cap*3]. Returns the count, or -1 when the region holds
 * more than cap (consumers fall back to the full window scan). Host-side,
 * init-time only. */
int  blaze_build_containers(const unsigned short *cells,
                            int rx0, int ry0, int rz0,
                            int rnx, int rny, int rnz, int *out, int cap);

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_SNAPSHOT_H */
