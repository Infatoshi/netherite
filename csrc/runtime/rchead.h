/* The ray cast on the device (lane/rcdev): what the CPU hands the GPU for
 * one env's cast, plain data that both raycast.c's side (C, the env's
 * worker) and cuda/raycast (CUDA) read with the same layout.
 *
 *   struct rcd_head   one cast's state as raycast.c's cast() sets it up
 *                     before its rays: the camera, the lightmap, sky and
 *                     fog, the eye's liquid, the entity boxes (raycast_head)
 *   struct rcd_col    a chunk column of the env's device copy of its client
 *                     world: what the cast reads of a struct chunk (the mask,
 *                     which sections have storage, the height map, biomes)
 *   struct rcd_sec    a section's cells: ids, then the metadata, sky and
 *                     block light nibbles (world.h's arrays, gathered)
 *
 * The device copy of an env's world is a ring of RCD_R x RCD_R columns:
 * chunk (cx, cz) lives at column (cx mod R) * R + (cz mod R), and its section
 * s at that column's slot s. A cast's window (raycast.c sections(): the
 * chunks within range + 1 of the eye, at most R = 2 (range + 1) / 16 + 2 on
 * a side) never holds two chunks of one column. rcmirror.c keeps the ring
 * equal to the client world at every cast: a column or section whose bytes
 * changed since the last cast is staged as a record and copied in before the
 * cast's rays run. */
#ifndef NETHERITE_RUNTIME_RCHEAD_H
#define NETHERITE_RUNTIME_RCHEAD_H

#include <stddef.h>
#include <stdint.h>

/* what a cell's block is to a ray */
enum { RK_AIR = 0, RK_CUBE, RK_BOX, RK_CROSS, RK_WATER, RK_LAVA };
/* the colour multiplier a block's faces take (render_blocks.c color_multiplier) */
enum { TN_NONE = 0, TN_GRASS_TOP, TN_GRASS, TN_FOLIAGE, TN_OLDLEAF, TN_WATER, TN_TALLGRASS, TN_STEM, TN_DPLANT };

/* the fixed instance's grid (raycast.c RC_FIX, cast.cu's fixed kernel): the eye's offset and the direction rounded to
 * RC_FIXF and RC_FIXG fractional bits (a measurement's build may set others, on the host alone) */
#ifndef RC_FIXF
#define RC_FIXF 24
#endif
#ifndef RC_FIXG
#define RC_FIXG 24
#endif
#define RCD_FP_FIXED 1          /* rcd_head.fp of the fixed instance (raycast.h RAYCAST_FP_FIXED) */
#define RCD_FP_FAST16 17        /* training-optimized native FP16 DDA + half2 shading (cast_fast.cu) */
#define RCD_FP_MIXED16 18       /* training-optimized fast FP32 DDA + half2 shading (cast_fast.cu) */
#define RCD_FP_FAST32 33        /* training-optimized fast FP32 DDA + FP32 shading (cast_fast.cu) */

#define RCD_ENTS 64             /* raycast.c RC_ENTS */
#define RCD_SEEN 4              /* raycast.h RC_SEEN */
#define RCD_RMAX 24             /* the ring's side at most: render distance up to 10 (range 160) */

struct rcd_ent {                /* raycast.c struct rcent */
    double box[6];
    int32_t i0, i1, j0, j1;
    float rgb[3];
    int32_t lmi;
};

/* the head's lightmap as packed RGB8 (lane/neon-prec-sim-buf), default 1 since lane/unify-2026-11-21: the same
 * frames as the float triples it was made from (-DNETH_RC_HEAD_U8=0 for float[256][3]) */
#ifndef NETH_RC_HEAD_U8
#define NETH_RC_HEAD_U8 1
#endif

struct rcd_head {
    double ox, oy, oz;
    double fx, fy, fz, rx, ry, rz, ux, uy, uz, aspect;
    double rnext;
    int32_t eye_x, eye_y, eye_z;
    int32_t liquid, range, no_sky, dim, fancy, top;
    int32_t ok;                 /* 0: no client world or no tables (a black frame, nothing seen) */
    int32_t seen_on;
    int16_t seen_ids[RCD_SEEN];
    int32_t cx0, cz0, ncx, ncz; /* the window (raycast.c sections()) */
    int32_t fp;                 /* the cast's instance (raycast.h "The two instances"): 64, or 32 (every real here is then
                                 * a float's value: the eye ox, oy, oz its offset in its cell, the boxes less that cell) */
    int32_t fp_pad_;
#if NETH_RC_HEAD_U8
    uint32_t lm[256];           /* packed ARGB8 lightmap (exact source of the 0..1 float triples) */
#else
    float lm[256][3];
#endif
    float sky[3], fog[3];
    int32_t exp_fog, uw;
    float uw_rgb[3];
    float fs, fe, fd, sky_end, fspan;
    int32_t nent;
    struct rcd_ent ent[RCD_ENTS];   /* (the head a multiple of 16 bytes: the device copies it so) */
};

struct rcd_col {
    int32_t cx, cz;
    int32_t exists;             /* a loaded chunk (raycast.c's col[] non-NULL) */
    uint16_t mask;              /* struct chunk's mask */
    uint16_t present;           /* bit s: section s has storage (the cast's sec[] non-NULL) */
    int32_t height[256];
    uint8_t biome[256];
};

#define RCD_CELLS 4096
struct rcd_sec {
    uint8_t ids[RCD_CELLS];
    uint8_t metas[RCD_CELLS / 2], sky[RCD_CELLS / 2], block[RCD_CELLS / 2];
};

/* a staged record: a column or a section of env's ring, its bytes after it */
enum { RCD_REC_COL = 1, RCD_REC_SEC = 2 };
struct rcd_rec {
    int32_t kind, env, col, sec;
};
#define RCD_REC_BYTES (sizeof(struct rcd_rec) + sizeof(struct rcd_sec))

/* the cast's tables in host memory (raycast_tables), copied to the device once: each sprite's 16x16 texels, the
 * faces' sprites by graphics, the renderer's bounds, the per-id kind, tint, emission and cull flags, the biomes'
 * colours, the End's sky, the liquids' and double plants' sprites */
struct rcd_tabsrc {
    int32_t nspr;
    const uint32_t *tex;        /* [nspr][256] */
    size_t nface;               /* RB_IDS * RB_METAS * 6 */
    const uint16_t *face[2];    /* [nface] */
    const double *bounds;       /* [nface] */
    const uint8_t *kind, *tint, *emit, *cull_same;   /* [4096] */
    const float *grass, *foliage, *water;           /* [256][3] */
    const float *end_sky;       /* [3] */
    uint16_t water_spr, lava_spr;
    const uint16_t *dp_low, *dp_high;               /* [6] */
};

/* what a cast saw (raycast.h struct raycast_seen) */
struct rcd_seen {
    int32_t n[RCD_SEEN];
    int32_t x[RCD_SEEN], y[RCD_SEEN], z[RCD_SEEN];
    float d[RCD_SEEN];
};

#endif
