/* The particle pass over a recorded scene: EffectRenderer.renderLitParticles
 * (layer 3, the EntityLargeExplodeFX quads) and renderParticles (layers 0, 1,
 * 2), ported statement by statement. The state row's "fx" block is the dump
 * RenderStateProbe.particles writes at the fog hook: every EntityFX in
 * mc.effectRenderer.fxLayers at exactly the moment the frame is about to draw
 * them, with each layer's texture and the fields renderParticle reads.
 *
 * Layer 3 draws inside renderLitParticles with the lightmap enabled but every
 * quad's brightness fixed at 240 by setBrightness, no blend, no alpha test
 * change; layers 0-2 draw in renderParticles with the layer's own texture,
 * depthMask false, alpha blend and GL_ALPHA_TEST GREATER 0.003921569. The
 * billboard basis is ActiveRenderInfo.rotationX/XZ/Z/YZ/XY, which the probe
 * does not dump but the frame's camera yaw and pitch recompute exactly
 * (first person, thirdPersonView 0, the formulas of
 * ActiveRenderInfo.updateRenderInfo). */
#ifndef NETHERITE_RASTER_PARTICLES_H
#define NETHERITE_RASTER_PARTICLES_H

#include <stdint.h>

struct rb_world;
struct rb_mesher;

/* One particle frame pass over the recorded frame. scene is the scene
 * directory (state/frames.jsonl, the atlas dumps); the target carries the
 * frame's buffers. Returns 1 when the row carried an "fx" block (old scenes
 * keep drawing nothing). */
int raster_particles_scene(const char *scene, int w, int h,
                           const float proj[16], const float mv[16],
                           const double cam[3], const float fog[3],
                           float fogs, float foge, float fogd, int fogm,
                           const uint32_t lm[256], float *depth, unsigned char *rgb,
                           unsigned long *triangles, unsigned long *samples);

/* The same passes over the live client's fx list (particles_live.h): the
 * caller owns the frame's buffers and the four layer textures (layer 0 the
 * particle atlas, 1 the block atlas, 2 the item atlas, 3 the explosion sheet),
 * plus the live mesher for the layer 1 icons and the interpolated view
 * rotation pair ActiveRenderInfo builds. Returns 0 when the list is empty. */
struct rb_mesher;
struct live_fx;
int raster_particles_draw_live(const struct live_fx *fx, int n,
                               int w, int h, const float proj[16], const float mv[16],
                               const double cam[3], const float fog[3],
                               float fogs, float foge, float fogd, int fogm,
                               const uint32_t lm[256], float *depth, unsigned char *rgb,
                               unsigned long *triangles, unsigned long *samples,
                               const unsigned char *const tex[4], const int tex_wh[8],
                               const struct rb_mesher *mesher,
                               float pt, float yaw, float pitch);

/* One entry of a recorded "fx" block (RenderStateProbe.particles) as the
 * live client's struct, over world w for the FX's later moves (NULL when
 * the FX is only drawn). Returns 0 for a class the port has no kind for (the
 * entry still loads, drawn as the base EntityFX). */
struct jval;
struct world;
int particles_live_from_json(const struct jval *m, struct live_fx *f, struct world *w);

#endif
