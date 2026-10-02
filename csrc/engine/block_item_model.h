#ifndef NETHERITE_BLOCK_ITEM_MODEL_H
#define NETHERITE_BLOCK_ITEM_MODEL_H

/* RenderBlocks.renderBlockAsItem geometry in block-local coordinates. The GUI,
 * held-item and EntityItem paths supply their own modelview and projection. */
struct block_item_quad {
    float p[4][3], uv[4][2];
    float normal[3];
    int tint_top;
};
struct block_item_model {
    struct block_item_quad quad[192];
    int n, inner_rotate_y90, render_pass, chest_texture;
};
int block_item_model_build(const char *scene, int id, int meta,
                           struct block_item_model *out);
/* ColorizerGrass.getGrassColor(0.5, 1.0) from SCENE's grass colormap, the
 * colour BlockGrass.getRenderColor gives the grass item's top; -1 without one. */
int block_item_model_grass_color(const char *scene);
/* RenderBlocks.renderBlockAsItem's six faces of one box with the bounds
 * locked (overrideBlockBounds) and every side drawn with the atlas sprite
 * ICON (setOverrideBlockTexture), appended to OUT at the Tessellator's own
 * positions: the caller applies the -0.5 translate and the 90 degree turn. */
int block_item_model_override_box(const char *scene, const char *icon,
                                  double x0, double y0, double z0,
                                  double x1, double y1, double z1,
                                  struct block_item_model *out);

#endif
