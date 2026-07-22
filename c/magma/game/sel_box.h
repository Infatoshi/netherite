/* game/sel_box.h - vanilla 1.11.2 per-block selection bounding boxes.
 *
 * Block.getSelectedBoundingBox defaults to getBoundingBox(state), so slabs,
 * torches, plants, crops, snow layers, chests, fences, panes, doors etc. all
 * highlight a shaped box, not the full cube. Constants below are copied from
 * the decompiled oracle (java/oracle-src/net/minecraft/block/Block*.java).
 * Cell space: b = {x0,y0,z0,x1,y1,z1} within [0,1]^3 of the block cell.
 */
#ifndef MAGMA_GAME_SEL_BOX_H
#define MAGMA_GAME_SEL_BOX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int id, meta;
    int nid[4];        /* horizontal neighbor ids: 0=z- (north), 1=z+ (south),
                        * 2=x- (west), 3=x+ (east); fence/pane/wire connections */
    int below_meta;    /* door upper half reads facing/open from the lower half */
    int above_meta;    /* door lower half reads hinge from the upper half */
} GmSelIn;

void gm_sel_box(const GmSelIn *in, float b[6]);

/* Convenience: fill GmSelIn from the psv chunk window at local (x,y,z). */
struct Chunk;
void gm_sel_box_at(const struct Chunk *win, int x, int y, int z, float b[6]);

/* Selection-box raycast: like psv_raycast but vanilla-correct - the ray hits
 * ANY targetable block (torch, plants, crops, slabs...) whose selection box
 * it enters, not just full-cube solids; air/liquids/fire pass through.
 * (hx,hy,hz) is the hit cell.  (ax,ay,az) is the cell adjacent to the face
 * actually hit on that block's selection AABB (the ItemBlock place spot).
 * For a full cube this is also the last traversed cell; for a recessed shape
 * such as a wall torch it need not be.  Returns 1 after leaving the eye cell,
 * 0 for a hit there, and -1 for no hit within reach.
 *
 * gm_raycast_sel uses PlayerControllerMP survival reach 4.5 (outline / look).
 * gm_raycast_sel_reach lets dig/place pass PSV_REACH (5.0) until the shared
 * sim path is fully aligned to survival 4.5. */
struct McSinTable;
struct PsvPlayer;
int gm_raycast_sel(const struct Chunk *win, const struct McSinTable *st,
                   const struct PsvPlayer *pl,
                   int *hx, int *hy, int *hz, int *ax, int *ay, int *az);
int gm_raycast_sel_reach(const struct Chunk *win, const struct McSinTable *st,
                         const struct PsvPlayer *pl, double reach,
                         int *hx, int *hy, int *hz, int *ax, int *ay, int *az);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_SEL_BOX_H */
