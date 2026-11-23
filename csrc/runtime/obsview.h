/* lane/obsidian: the water, lava and obsidian a player sees, for NETH_VEC_OBSIDIAN (mods/macros/obsidian.c).
 *
 * The cast's seen record (rlbind.h neth_rl_state seen) holds ore, gravel and logs only, so the obsidian macro has its own
 * look at the client world (what the player has been sent, never the server's), the way vis_scan finds the livings in
 * view: a cell within `radius` blocks of the eye and 75 degrees of the look whose point the eye sees past every block
 * (raytrace_blocks over the client world, liquids passed). Water and lava count only as a source (metadata 0) at the
 * surface (the cell over it not the same liquid: the face a player sees), its point the cell's top; obsidian its
 * centre, the ray's first block the cell itself. Linux and CUDA builds alike (no device code). */
#ifndef NETHERITE_RUNTIME_OBSVIEW_H
#define NETHERITE_RUNTIME_OBSVIEW_H

#include <stdint.h>

/* OV_HOT (lane/obsidian2): any lava cell, source or flowing, its centre seen (liquids passed): the strip's danger */
enum { OV_WATER, OV_LAVA, OV_OBSIDIAN, OV_HOT, OV_KINDS };
/* lane/obsidian4: a mask bit (not a kind): water and lava sources only with air over them or beside them (a face that
 * can show), before the lines of sight are tried */
#define OV_EXPOSED (1 << 8)
/* lane/obsid2: a mask bit: obsidian also seen where the ray to one of its open faces (top, sides) meets it first */
#define OV_FACES (1 << 9)

struct obsview_hit {
    int32_t n;                  /* the cells of the kind in the cone (before the line of sight; 0: none) */
    int32_t x, y, z;            /* the nearest one seen */
    float d;                    /* its distance from the eye, -1 none seen */
};

struct pipe;

/* env id's client world from the eye (ex, ey, ez) looking along (yaw, pitch) degrees: the nearest seen cell of each
 * kind in mask (bit OV_*), skip(ctx, kind, x, y, z) a cell left out (a given-up target; NULL none). 0, -1 no client world */
int obsview_scan(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int radius, int mask,
                 int (*skip)(void *ctx, int kind, int x, int y, int z), void *ctx, struct obsview_hit out[OV_KINDS]);

/* lane/obsidian5: the far look over the surface, for the bucket portal's walk (mods/macros/bportal.c BM_ROAM): the same
 * answer as obsview_scan for OV_WATER and OV_LAVA (a source with its top face to air, the nearest the eye sees past
 * every block, in the 75-degree cone), but only cells at a column's top (the chunk's height map and up to
 * OV_FAR_DEPTH under it: lakes, a ravine's floor open to the sky), so a radius of 64 costs columns, not cubes */
#define OV_FAR_DEPTH 3
int obsview_far(struct pipe *p, int id, double ex, double ey, double ez, float yaw, float pitch, int radius, int mask,
                int (*skip)(void *ctx, int kind, int x, int y, int z), void *ctx, struct obsview_hit out[OV_KINDS]);

/* the sky light stored at a cell of env id's client world (what F3's sl shows; -1 its chunk not loaded) */
int obsview_sky(struct pipe *p, int id, int x, int y, int z);

/* the block id (meta into *meta) at a cell of env id's client world; -1 where its chunk is not loaded */
int obsview_block(struct pipe *p, int id, int x, int y, int z, int *meta);

/* the section scans of env id's client world (AVX-512BW where the build has it, else scalar; the same answer):
 * find_sources the max_n nearest still water (lava 1: lava) sources within r round (fx, fz), down under to up over
 * fy, slots filled and replaced in the (y, z, x) cell walk's order (the count; -1 for r past 8x8 chunks or max_n
 * past 16: the caller walks the cells); find_diamond the nearest diamond ore within r, ybot to ytop, that bad (if
 * any) passes and with no water or lava in its 3x3x3, ties to the lowest y, z, x (1 found, into *ox, *oy, *oz) */
int obsview_find_sources(struct pipe *p, int id, int fx, int fy, int fz, int r, int down, int up, int lava,
                         int max_n, int *bx, int *by, int *bz);
int obsview_find_diamond(struct pipe *p, int id, int fx, int fy, int fz, int r, int ybot, int ytop,
                         int (*bad)(const void *ctx, int x, int y, int z), const void *ctx, int *ox, int *oy, int *oz);

#endif
