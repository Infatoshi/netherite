/* lane/stronghold: what a player sees that NETH_VEC_STRONGHOLD (mods/macros/stronghold.c) acts on, beside the cast's seen
 * record (rlbind.h neth_rl_state seen holds ore, gravel and logs only, and the livings in view hold no thrown eye).
 *
 *   the thrown eye    the newest EntityEnderEye of the player's world whose middle the eye sees past every block of
 *                     the client world and within SHV_CONE degrees of the look (vis_scan's rule: what is on the
 *                     screen, never through a wall); its position this tick (the flight, one sample a tick)
 *   its drop          the nearest eye of ender lying as an item, the same rule
 *   blocks            within the radius and the cone, the eye seeing the cell itself first along the line to its
 *                     middle (raytrace_blocks over the client world: what the player has been sent, never the
 *                     server's): the nearest stone brick (98, the silverfish's 97, iron bars 101), the end portal
 *                     frames (120, up to SHV_FRAMES of them, their metadata: bit 2 the eye), the nearest lava
 *   a look's ray      the first block along a line from the eye (liquid sources stop it): what the middle of the
 *                     screen shows when the look is along it, out past the crosshair's 4.5 blocks
 *
 * Linux and CUDA builds alike (no device code). The scans run on the env's own worker in its act hook. */
#ifndef NETHERITE_RUNTIME_SHVIEW_H
#define NETHERITE_RUNTIME_SHVIEW_H

#include <stdint.h>

#define SHV_FRAMES 16
#define SHV_CONE 75.0           /* degrees from the look: vis_scan's */
#define SHV_SITE_IN 8.0         /* SHV_LEAVES: leaves at least this far across */

enum { SHV_EYE = 1, SHV_DROP = 2, SHV_BLOCKS = 4, SHV_LEAVES = 8 };

struct shview {
    int32_t eye_id;             /* the thrown eye's entity id, 0 none in view */
    double eye_x, eye_y, eye_z;
    int32_t drop_n;             /* an eye lying as an item in view */
    double drop_x, drop_y, drop_z;
    int32_t brick_n, bx, by, bz, bblock; /* the nearest stone brick seen (its block) */
    float bd;
    int32_t nframe;             /* the frames seen, nearest first */
    struct { int32_t x, y, z, meta; } frame[SHV_FRAMES];
    int32_t lava_n, lx, ly, lz; /* the nearest lava seen */
    float ld;
    int32_t portal_n, px, py, pz; /* lane/stronghold2: the nearest end portal block seen (119: the frames lit) */
    int32_t leaves_n;           /* lane/stronghold2 (SHV_LEAVES): leaves seen SHV_SITE_IN to the radius away across
                                 * (where a thrown eye hovers, 12 out), up from the eye's height less 2 (a canopy) */
    double leaves_x, leaves_z;  /* ... their mean place */
    int32_t trace_drop;       /* debugging only (SH_TRACE builds; never read by the macro): the eye's shatterOrDrop */
};

struct pipe;

/* env id's view from the eye (ex, ey, ez) along (yaw, pitch) degrees, the parts in mask (SHV_*), blocks within radius.
 * 0, -1 no client world */
int shview_scan(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int radius, int mask,
                struct shview *out);

/* the first block from the eye toward (tx, ty, tz), a liquid source included: 1 with its cell, id and metadata, 0 none
 * on the line, -1 no client world */
int shview_ray(struct pipe *p, int id, double ex, double ey, double ez, double tx, double ty, double tz, int *x, int *y,
               int *z, int *block, int *meta);

#endif
