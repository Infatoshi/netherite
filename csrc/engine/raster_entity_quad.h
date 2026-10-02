#ifndef NETHERITE_RASTER_ENTITY_QUAD_H
#define NETHERITE_RASTER_ENTITY_QUAD_H

#include <stdint.h>

/* The entity rasterizer shared by paintings, squids, and living models. */
struct entity_clip_vertex
{
    float clip[4];
    float x, y, sx, sy, z, w;
    float depth;
    float fogcoord;
    float u, v;
    float light[2];
    float diffuse;
/* glColor per vertex; only the passes that set target.vertex_color read it. */
    float color[4];
    float alpha;   /* per-vertex glColor alpha, for the weather quads */
};

struct entity_raster_target
{
    int w, h;
    const float *proj, *mv;
    const double *cam;
    const float *fog;
    float fogs, foge, fogd;
    int fogm;
    const uint32_t *lm;
    float *depth;
    unsigned char *rgb;
    unsigned long *triangles, *samples;
    int overlay; /* GL_EQUAL, texture/lightmap disabled, 40% red */
    float overlay_brightness;
    int overlay_lightmap; /* the overlay with only unit 0 off: the lightmap still modulates it */
    float material[3];
    int blend; /* 1 shadow: alpha blend, no depth write; 2 additive; 3 alpha blend with depth
                * write; 4 (SRC_ALPHA, ONE) by vertex alpha; 5 (SRC_COLOR, ONE), 8-bit;
                * 6 (SRC_ALPHA, ONE) by vertex alpha, 8-bit, with depth write; 7 (ONE, ONE)
                * with depth write */
    float alpha;
    int clamp_texture;
    float tex_u, tex_v;
    int unlit;
    int linear_depth; /* the hand pass: window z interpolated linearly in screen space */
int cull_front;   /* GL_CULL_FACE with glCullFace(GL_FRONT): keep area < 0 */
    int depth_equal;  /* GL_DEPTH_FUNC = GL_EQUAL, no depth write */
    float alpha_ref;  /* GL_ALPHA_TEST reference (default 0.1) */
    int vertex_color; /* read the per-vertex glColor (default white) */
    float overlay_alpha; /* GL blend alpha for the overlay pass (default 0.4) */
    int overlay_has_rgb;  /* the overlay's glColor is overlay_rgb, not the red of the hurt */
    float overlay_rgb[3];
    int two_sided; /* GL_CULL_FACE off (RendererLivingEntity.doRender): both windings draw */
    int no_light;    /* GL_LIGHTING and the lightmap off: the colour is the material alone */
    int no_alpha;    /* GL_ALPHA_TEST off: transparent texels still blend */
    int vertex_alpha; /* glColor's alpha varies per vertex (the weather quads) */
    int alpha_cut;    /* GL_ALPHA_TEST threshold in 1/255 texel alpha units; default 25 */
    /* raster_mobs' model boxes only: a GL_TEXTURE matrix (u' = tm[0] u + tm[2] v
     * + tm[4], v' = tm[1] u + tm[3] v + tm[5]) and a texture sampled in place of
     * the model's own (the armour glint over a 64x32 model) */
    int tex_matrix;
    float tm[6];
    int depth_lequal; /* GL_LEQUAL for the opaque draws too (the older passes test GL_LESS) */
    const unsigned char *sample_tex;
    int sample_w, sample_h;
};

/* raster_prec.h: the stages this process's entity (and end portal)
 * triangles draw fast; 0, exact, by default */
void raster_entity_set_prec(int prec);
int raster_entity_prec(void);

void raster_entity_quad(const struct entity_raster_target *target, const unsigned char *texture,
                        int texture_w, int texture_h, const struct entity_clip_vertex vertex[4]);

#ifdef NETHERITE_MOB_CULL_CHECK
/* raster_mobs.c's cull check: while on, every quad is tested instead of
 * drawn or recorded, and one that is not wholly outside one clip plane
 * aborts (the model it belongs to was culled) */
void raster_entity_cull_probe(int on, int kind);
#endif

#endif
