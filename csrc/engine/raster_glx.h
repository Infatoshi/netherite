#ifndef NETHERITE_RASTER_GLX_H
#define NETHERITE_RASTER_GLX_H

/* The fixed-function GL state the world entity passes draw with, emulated the
 * way raster_hand.c does it for the hand: the modelview built with the GL11
 * calls in vanilla order through Mesa's matrix arithmetic (rs_gl_*),
 * RenderHelper.enableStandardItemLighting's two lights in eye space (set under
 * the camera modelview, as EntityRenderer.renderWorld enables them before
 * renderEntities), the normal through the inverse transpose with
 * GL_RESCALE_NORMAL, glColor as the lit material, the lightmap coordinates,
 * an optional texture matrix, and the entity rasterizer's blend and depth
 * switches (the target). Used by the non-living entity renders
 * (raster_things.c), the living models' equipment and the pickup animation. */

#include "raster_entity_quad.h"

struct glx
{
    struct entity_raster_target t; /* the frame; the pass switches live here */
    float mv[16];
    float light[2][3];             /* eye-space light directions, normalized */
    float color[4];                /* glColor */
    int lighting;                  /* GL_LIGHTING */
    float normal[3];               /* glNormal3f, for quads drawn without one */
    int lb, ls;                    /* the lightmap coordinates (block, sky) */
    int tex_matrix;                /* GL_TEXTURE matrix set (not identity) */
    float tm[16];
};

/* The frame's camera modelview, lighting on, glColor white, the lightmap at
 * BRIGHTNESS (RenderManager's getBrightnessForRender pair). */
void glx_init(struct glx *g, const struct entity_raster_target *t, int brightness);

/* The lit factor for normal N (eye-space lights, rescaled normal); BYTES
 * quantizes N as Tessellator.setNormal packs it. */
float glx_lit(const struct glx *g, const float n[3], int bytes);

/* One quad in the current state. N is the quad's Tessellator normal (packed
 * to bytes) or NULL for the current glNormal3f. */
void glx_quad(struct glx *g, const float p[4][3], const float uv[4][2], const float *n,
              const unsigned char *tex, int tw, int th);

/* A white 1x1 texture for the untextured passes (GL_TEXTURE_2D off). */
extern const unsigned char glx_white[4];

/* ModelBox.render compiled into a display list at SCALE: six TexturedQuads,
 * each normal from its own vertices (TexturedQuad.draw). MIRROR swaps the x
 * corners as ModelBox's mirror does. */
void glx_model_box(struct glx *g, float x, float y, float z, int dx, int dy, int dz, float grow,
                   int u, int v, int mirror, float scale, const unsigned char *tex, int tw, int th);

/* ItemRenderer.renderItemIn2D: front, back and the per-texel edge strips. */
void glx_item_in_2d(struct glx *g, float p1, float p2, float p3, float p4, int w, int h,
                    float thick, const unsigned char *tex, int tw, int th);

#endif
