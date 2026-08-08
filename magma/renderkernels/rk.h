/* renderkernels/rk.h - reusable, bit-exact Minecraft 1.11.2 render kernels.
 *
 * Each rk_* function is the pure COMPUTE core of one render-opt kernel
 * (mc-1.11.2-env/java/render-opt/kernels/<NN_name>/candidate.c), copied VERBATIM with only
 * the stdin/stdout/main() harness removed. The arithmetic (float/int op order, constants,
 * table values) is preserved exactly so the output stays BIT-FOR-BIT identical to real
 * Minecraft. Compile every translation unit with -ffp-contract=off.
 *
 * One .c file per kernel (rk_<NN>_<name>.c) so each candidate's file-static helpers keep
 * internal linkage and cannot collide. This header is the stable public surface a later
 * DRIVER agent calls; do not change a signature without noting it here.
 *
 * All types are plain C (int / int32_t / float / double / arrays). Unless stated, integers
 * are Java int32 semantics (two's-complement wrap) and floats are IEEE-754 single.
 */
#ifndef RENDERKERNELS_RK_H
#define RENDERKERNELS_RK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 07_mipmap_blend_gamma - TextureUtil.blendColors()/blendColorComponent()
 * Gamma-correct 2x2 box blend of 4 ARGB texels. Linearizes each channel through the
 * COLOR_GAMMAS (pow 2.2) LUT, averages, re-encodes via pow 0.45454..., with the
 * transparency branch (skip alpha==0 lanes, zero alpha<96). The COLOR_GAMMAS LUT is
 * built lazily on first use.
 *   c0..c3            : the four source ARGB pixels (signed int, 0xAARRGGBB)
 *   has_transparency  : selects the transparency-aware branch (nonzero = yes)
 *   returns           : blended ARGB pixel (signed int)
 * ==========================================================================*/
int rk_mipmap_blend_gamma(int c0, int c1, int c2, int c3, int has_transparency);

/* ============================================================================
 * 08_mipmap_gen_chain - TextureUtil.generateMipmapData()
 * Generates the full mip chain by repeated gamma-correct 2x2 downsampling (kernel 07's
 * blend). Reproduces the verbatim hasTransparency quirk (scans the first max_level+1 base
 * pixels) and the exact index math of the reference loop.
 *
 * Fills RkMipChain: level[0] aliases the caller-owned `base` (length[0]==n_base); levels
 * 1..max_level are freshly malloc'd here (length[l] == length[l-1] >> 2). Call
 * rk_mipmap_chain_free() to release levels 1..max_level (level 0 is NOT freed).
 * ==========================================================================*/
typedef struct {
    int      max_level;
    int32_t *level[16];
    int      length[16];
} RkMipChain;

void rk_mipmap_gen_chain(int max_level, int width,
                         int32_t *base, int n_base, RkMipChain *out);
void rk_mipmap_chain_free(RkMipChain *chain);

/* ============================================================================
 * 12_ao_vertex_brightness - BlockModelRenderer.AmbientOcclusionFace.updateVertexBrightness()
 * From the ~20 snapshotted corner/diagonal/center light+AO scalars of one quad, produce the
 * 4 packed vertexBrightness ints and 4 vertexColorMultiplier floats. Handles both the cubic
 * branch and the non-cubic faceShape-weighted branch (taken iff s1 != 0; doNonCubicWeight is
 * always true for the sampled faces).
 *   face        : EnumFacing index 0..5 (DOWN UP NORTH SOUTH WEST EAST)
 *   s1          : shapeState.get(1) (selects non-cubic branch when nonzero)
 *   faceShape   : 12 AO faceShape weights (AmbientOcclusionFace faceShape[])
 *   li,lj,lk,ll : packed lightmap coords at the 4 corners (i,j,k,l)
 *   i1,j1,k1,l1 : the conditionally-selected corner-diagonal packed coords
 *   i3          : center (or center+dir) packed coords
 *   f..f8       : AmbientOcclusionLightValue scalars (f..f3 corners, f4..f7 diagonals, f8 center)
 *   out_vb[4]   : the 4 vertexBrightness ints (indexed by output vertex slot)
 *   out_vcm[4]  : the 4 vertexColorMultiplier floats
 * ==========================================================================*/
void rk_ao_vertex_brightness(int face, int s1,
                             const float faceShape[12],
                             int li, int lj, int lk, int ll,
                             int i1, int j1, int k1, int l1, int i3,
                             float f, float f1, float f2, float f3,
                             float f4, float f5, float f6, float f7, float f8,
                             int out_vb[4], float out_vcm[4]);

/* ============================================================================
 * 13_ao_pack_helpers - BlockModelRenderer.getAoBrightness()/getVertexBrightness()
 * Pure packed-brightness helpers (0xRR00GG lane math), bit-exact to the JVM's int wrap and
 * float->int truncation.
 *   rk_ao_get_ao_brightness      : zero-substitute then (br1+br2+br3+br4)>>2 & 0x00FF00FF
 *   rk_ao_get_vertex_brightness  : weighted float blend of the RR / GG byte lanes,
 *                                  truncate & 255, repack (i<<16 | j)
 * ==========================================================================*/
int32_t rk_ao_get_ao_brightness(int32_t br1, int32_t br2, int32_t br3, int32_t br4);
int32_t rk_ao_get_vertex_brightness(int32_t p1, int32_t p2, int32_t p3, int32_t p4,
                                    float f5, float f6, float f7, float f8);

/* ============================================================================
 * 14_light_query - World.getLightFromNeighborsFor()
 * Block/sky light query (0..15). For useNeighborBrightness blocks: max of the 5 neighbour
 * light values (up/east/west/south/north, NOT down, NOT self); otherwise the block's own
 * stored light. Neighbour validity/loading is already folded into the passed-in values.
 *   nb  : useNeighborBrightness() of the block (0/1)
 *   up,east,west,south,north : the 5 neighbour light values
 *   own : the block's own stored light
 * ==========================================================================*/
int rk_light_query(int nb, int up, int east, int west, int south, int north, int own);

/* ============================================================================
 * 15_light_combine_pack - World.getCombinedLight() (pure packing tail)
 * Pack resolved sky + block light into one lightmap int, clamping block up to an override
 * lightValue first: if (block < override) block = override; return sky<<20 | block<<4.
 * Shifts use two's-complement wrap semantics (Java signed <<).
 * ==========================================================================*/
int32_t rk_light_combine_pack(int32_t sky, int32_t block, int32_t override_value);

/* ============================================================================
 * 18_biome_color_blend - BiomeColorHelper.getColorAtPos()
 * 3x3 biome color blend: channel-wise sum of the 9 packed 0xRRGGBB colors, integer-divide
 * each channel by 9, mask to a byte, repack: (i/9&255)<<16 | (j/9&255)<<8 | k/9&255.
 *   c[9] : the 9 packed 0xRRGGBB grass colors of the 3x3 box (order-independent)
 * ==========================================================================*/
int rk_biome_color_blend(const int c[9]);

/* ============================================================================
 * 21_should_side_render - Block.shouldSideBeRendered()
 * Per-face visibility. Early "return true" if the block's bounding box does not reach that
 * face; otherwise return !neighbourDoesSideBlockRendering. Bounds are the exact doubles the
 * method reads (feed raw doubleToRawLongBits reinterpreted to double for bit fidelity).
 *   side : EnumFacing.getIndex() 0=DOWN 1=UP 2=NORTH 3=SOUTH 4=WEST 5=EAST
 *   minX..maxZ : the block's AxisAlignedBB components
 *   nbr  : neighbour.doesSideBlockRendering(...) (0/1)
 *   returns : 1 if the side should render, else 0
 * ==========================================================================*/
int rk_should_side_render(int side,
                          double minX, double minY, double minZ,
                          double maxX, double maxY, double maxZ, int nbr);

/* ============================================================================
 * 27_translucent_sort - VertexBuffer.sortVertexData() (+ getDistanceSq)
 * Painter's-order translucent sort. For each of n quads (BLOCK format, 28 floats/quad in
 * `floatbuf`) computes the centroid-to-camera squared distance (getDistanceSq, verbatim
 * float math), then writes into out_idx the quad permutation sorted by DESCENDING distance,
 * ties broken by ASCENDING original index (matches Java's stable TimSort via a total order).
 *   floatbuf : n*28 floats (quad vertex data, contiguous)
 *   n        : quad count
 *   camX/Y/Z : camera position
 *   out_idx  : n ints, the output permutation (caller-allocated)
 * NOTE: uses file-static scratch during the sort; not thread-safe (single-threaded use).
 * ==========================================================================*/
void rk_translucent_sort(const float *floatbuf, int n,
                         float camX, float camY, float camZ, int *out_idx);

/* ============================================================================
 * 28_vertex_pack - VertexBuffer.addVertexData()+putBrightness4()+putColorMultiplier()
 * Pack one quad (int[28], DefaultVertexFormats.BLOCK) into the buffer: copy the 28 ints,
 * overwrite the 4 lightmap ints (indices 6,13,20,27) via putBrightness4, then apply the
 * per-vertex color multiplier (little-endian branch, (int)(byte*mult) channel truncation)
 * to color ints 3,10,17,24. Bit-exact.
 *   data[28]  : source quad vertex data
 *   bright[4] : the 4 packed lightmap/brightness values (b1..b4)
 *   cmul[4]   : the 4 per-vertex color multipliers (applied at vertexIndex 4,3,2,1)
 *   out[28]   : resulting packed quad (may alias nothing; caller-allocated)
 * ==========================================================================*/
void rk_vertex_pack(const int32_t data[28], const int32_t bright[4],
                    const float cmul[4], int32_t out[28]);

/* ============================================================================
 * 31_facebakery_make_quad - FaceBakery.makeBakedQuad()
 * Full BakedQuad int[28] bake: position bounds /16, 4x fillVertexData (shade=false so the
 * color lane is -1), getFacingFromVertexData, applyFacing when partRotation is absent, and
 * ForgeHooksClient.fillNormal (with its int-bits-widened-by-value quirk). modelRotation is
 * identity and uvLock false (post-lock uvs are fed directly).
 *   fx,fy,fz / tx,ty,tz : the face's from/to position bounds (0..16 model units)
 *   facing              : source EnumFacing index 0..5
 *   uvQuarter           : the UV rotation quarter-turn count
 *   uvs[4]              : the face's 4 post-lock UV bounds (u0,v0,u1,v1)
 *   minU,maxU,minV,maxV : sprite UV bounds used by getInterpolatedU/V
 *   partPresent         : 1 if a BlockPartRotation is present (skips applyFacing)
 *   axis                : rotation axis 0=X 1=Y 2=Z (>2 = none) when partPresent
 *   angle               : rotation angle in degrees
 *   origin[3]           : rotation origin
 *   rescale             : BlockPartRotation rescale flag (0/1)
 *   out_d[28]           : the baked quad vertex data
 *   returns             : the derived EnumFacing ordinal
 * ==========================================================================*/
int rk_facebakery_make_quad(float fx, float fy, float fz,
                            float tx, float ty, float tz,
                            int facing, int uvQuarter, const float uvs[4],
                            float minU, float maxU, float minV, float maxV,
                            int partPresent, int axis, float angle,
                            const float origin[3], int rescale, int32_t out_d[28]);

/* ============================================================================
 * 32_facebakery_fill_vertex - FaceBakery.fillVertexData()/storeVertexData()
 * Per-vertex bake: pick the vertex position from the EnumFaceDirection table + bounds, apply
 * rotatePart, compute the directional shade color (when shade), interpolate the sprite UV
 * (0.999/0.001 corner mix), and pack into a vertex int[7] (pos0,pos1,pos2,color,u,v,normal=0).
 * modelRotation is identity (rotateVertex no-op). Args mirror kernel 31 for one vertex.
 *   vertexIndex : which of the 4 face vertices (0..3)
 *   facing      : EnumFacing index 0..5
 *   shade       : 1 -> compute directional shade color, 0 -> color lane = -1
 *   bounds[6]   : position bounds already in model space (indices per EnumFacing)
 *   out[7]      : the packed vertex ints
 * ==========================================================================*/
void rk_facebakery_fill_vertex(int vertexIndex, int facing, int shade,
                               const float bounds[6], int uvQuarter, const float uvs[4],
                               float minU, float maxU, float minV, float maxV,
                               int axis, float angle, const float origin[3],
                               int rescale, int32_t out[7]);

/* ============================================================================
 * 33_facebakery_rotate - FaceBakery.rotatePart() + rotateVertex()'s geometric transform
 * rotatePart() rotates a model vertex about an axis through an origin (LWJGL Matrix4f.rotate
 * matrix, with the 22.5/general rescale branches); transform() applies a javax.vecmath
 * row-major 4x4 (the ITransformation matrix, fed as input).
 *   in_pos[3]        : the source model vertex
 *   axis             : 0=X 1=Y 2=Z, >2 = no rotation
 *   angle            : degrees
 *   origin[3]        : rotation origin
 *   rescale          : rescale flag (0/1)
 *   m[16]            : row-major 4x4 transform matrix
 *   out_rotated[3]   : result of rotatePart alone
 *   out_transformed[3] : result of transform(out_rotated, m)
 * ==========================================================================*/
void rk_facebakery_rotate(const float in_pos[3], int axis, float angle,
                          const float origin[3], int rescale, const float m[16],
                          float out_rotated[3], float out_transformed[3]);

/* ============================================================================
 * 34_facebakery_facing_normal - FaceBakery.getFacingFromVertexData() + applyFacing()
 * rk_facebakery_get_facing   : compute the quad normal from int[28] (Vector3f sub/cross,
 *                              sqrt-normalize) and argmax the 6 axis dots -> EnumFacing index
 *                              (null -> UP=1). LWJGL 2.9.2 cross/normalize semantics preserved.
 * rk_facebakery_apply_facing : reorder the 4 vertices in place to the target facing's corner
 *                              layout (min/max bounds + EnumFaceDirection table); always
 *                              overwrites pos lanes, copies u/v lanes only on an epsilonEquals
 *                              (fabsf(b-a)<1e-5) position match.
 *   faceData[28] : the quad vertex data (modified in place by apply_facing)
 *   targetFacing : EnumFacing index 0..5
 * ==========================================================================*/
int  rk_facebakery_get_facing(const int32_t faceData[28]);
void rk_facebakery_apply_facing(int32_t faceData[28], int targetFacing);

#ifdef __cplusplus
}
#endif
#endif /* RENDERKERNELS_RK_H */
