/* blaze_snapshot.h - the .bsnp state-snapshot format, shared between the
 * writer (game/rl_mode.c, "snapshot":"<path>" action key + --snapshot-in
 * restore) and the batched-env reader (blaze/env). The CODE is the canonical
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

#define BLAZE_SNAP_MAX_ITEMS 48   /* == GM_LIVE_MAX (game/live_sim.h) */
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

/* ---- host-side loader (blaze/env/blaze_snapshot.c; NOT linked into the
 * game binary - rl_mode.c uses only the structs above) ---- */
typedef struct {
    RlSnapHead     head;
    RlSnapItem     items[BLAZE_SNAP_MAX_ITEMS];
    unsigned short *cells;         /* malloc'd rnx*rny*rnz packed states */
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
} CuSnapshot;

/* Load a .bsnp into *out (mallocs cells/coal; blaze_snapshot_free releases).
 * Returns 1 on success, else 0 with a message in err. */
int  blaze_snapshot_load(const char *path, CuSnapshot *out,
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

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_SNAPSHOT_H */
