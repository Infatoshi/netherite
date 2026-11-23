/* Helpers render_blocks.c and render_blocks2.c share: the Tessellator emitters,
 * the icon lookups, the light and colour helpers, the six face renderers and
 * the standard-block paths. Declared here and defined in render_blocks.c; the
 * second file (render_blocks2.c) is the ported render types beyond 0, 1, 4, 31
 * and 40. See render_blocks.h for the probe-directory contract. */
#ifndef NETHERITE_RENDER_BLOCKS_INT_H
#define NETHERITE_RENDER_BLOCKS_INT_H

#include "render_blocks.h"

/* icons */
const char *icon_name(const struct rb_table *tab, int idx);

/* Tessellator */
void tess_set_brightness(struct rb_tess *t, int32_t b);
void tess_set_color_opaque(struct rb_tess *t, int32_t r, int32_t g, int32_t b);
void tess_set_color_opaque_f(struct rb_tess *t, float r, float g, float b);
void tess_add_vertex(struct rb_tess *t, double x, double y, double z);
void tess_add_vertex_with_uv(struct rb_tess *t, double x, double y, double z, double u, double v);

/* icons */
int table_icon_index(const struct rb_mesher *m, int id, int meta, int side);
int icon_is(const struct rb_mesher *m, int idx, const char *name);
const struct rb_uv *table_icon(const struct rb_mesher *m, int id, int meta, int side);
const struct rb_uv *icon_from_side_meta(const struct rb_mesher *m, int id, int side, int meta);
const struct rb_uv *icon_from_side(const struct rb_mesher *m, int id, int side);
const struct rb_uv *block_icon_world(struct rb_mesher *m, int id, int x, int y, int z, int side);
int32_t rb_foliage_color_multiplier(struct rb_mesher *m, int x, int y, int z);

/* light and colour */
int block_brightness(struct rb_mesher *m, int sender, int x, int y, int z);
float ao_light_of(struct rb_mesher *m, int x, int y, int z);
int can_block_grass(struct rb_mesher *m, int x, int y, int z);
int opaque_cube(struct rb_mesher *m, int x, int y, int z);
int32_t get_ao_brightness(int32_t a, int32_t b, int32_t c, int32_t d);
int32_t mix_ao_brightness(int32_t p1, int32_t p2, int32_t p3, int32_t p4,
                          double w1, double w2, double w3, double w4);
int32_t color_multiplier(struct rb_mesher *m, int id, int x, int y, int z);

/* block state */
void set_bounds(struct rb_mesher *m, int id, int meta);
int slab_super_should_side(struct rb_mesher *m, int id, int x, int y, int z, int side);
int should_side_be_rendered(struct rb_mesher *m, int id, int x, int y, int z, int side);

/* faces and standard blocks */
float interp_u(const struct rb_uv *uv, double u);
float interp_v(const struct rb_uv *uv, double v);
void render_face_y_neg(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic);
void render_face_y_pos(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic);
void render_face_z_neg(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic);
void render_face_z_pos(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic);
void render_face_x_neg(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic);
void render_face_x_pos(struct rb_mesher *m, double x, double y, double z, const struct rb_uv *ic);
void set_face_color(struct rb_mesher *m, float r, float g, float b);
void scale_face_colors(struct rb_mesher *m, float tl, float bl, float br, float tr);
int render_standard_block(struct rb_mesher *m, int id, int x, int y, int z);
int render_standard_block_ao(struct rb_mesher *m, int id, int x, int y, int z,
                             float p5, float p6, float p7);
int render_standard_block_ao_partial(struct rb_mesher *m, int id, int x, int y, int z,
                                     float p5, float p6, float p7);
int render_standard_block_color(struct rb_mesher *m, int id, int x, int y, int z,
                                float p5, float p6, float p7);
/* libm's cos, sin and atan2 as the mesher calls them: glibc's on the host
 * (render_blocks.c), the host's values looked up on the device (cuda/meshing) */
double rb_cos(const struct rb_mesher *m, double x);
double rb_sin(const struct rb_mesher *m, double x);
double rb_atan2(const struct rb_mesher *m, double y, double x);
float mh_sin(const struct rb_mesher *m, float f);
float mh_cos(const struct rb_mesher *m, float f);

/* render_blocks2.c: the render types beyond 0, 1, 4, 31 and 40. Each returns
 * what RenderBlocks.renderBlockByRenderType returns (a drawn block or not);
 * the dispatch ignores it, the oracle records vertices only. */
int rb2_render_torch(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_fire(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_redstone_wire(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_crops(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_door(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_ladder(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_rail(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_stairs(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_fence(struct rb_mesher *m, int id, int x, int y, int z);
/* a block's class bits (render_blocks.c block_class_of, from its class
 * name once): these two for render_blocks3.c's getIcon */
enum { RB_CLASS_ANVIL = 128, RB_CLASS_PISTON_BASE = 256 };
int rb_block_class(int id);
#if defined(__NVPTX__)
/* what the three leave of the mesher's words (the device mesher's guess) */
void rb2_stairs_state(struct rb_mesher *m, int id, int x, int y, int z);
void rb2_door_state(struct rb_mesher *m, int id, int x, int y, int z);
void rb2_fence_state(struct rb_mesher *m, int id, int x, int y, int z);
#endif
int rb2_render_lever(struct rb_mesher *m, int id, int x, int y, int z);
int rb2_render_cactus(struct rb_mesher *m, int id, int x, int y, int z);

/* render_blocks2.c's fixed icon names, resolved from atlas.json by name at
 * init: the two fire layers and the four redstone dust layers. table.bin only
 * records icons getIcon(side, meta) reaches; these are fetched by name instead. */
enum {
    RB2_ICON_FIRE_0 = 0,
    RB2_ICON_FIRE_1,
    RB2_ICON_DUST_CROSS,
    RB2_ICON_DUST_LINE,
    RB2_ICON_DUST_CROSS_OVERLAY,
    RB2_ICON_DUST_LINE_OVERLAY
};

/* Resolve the six names above into m; called from rb_mesher_init. */
int rb2_init(struct rb_mesher *m);

/* RenderBlocks.setRenderBounds: the current render bounds and the partial flag
 * it recomputes with them (the AO paths read both). */
void rb2_set_render_bounds(struct rb_mesher *m, double x0, double y0, double z0,
                           double x1, double y1, double z1);

/* RenderBlocks.drawCrossedSquares (the private one with the scale argument). */
void draw_crossed_squares(struct rb_mesher *m, const struct rb_uv *ic,
                          double x, double y, double z, float f);

/* render_blocks2.c helpers render_blocks3.c shares: the atlas lookup by name,
 * World.doesBlockHaveSolidTopSurface, RenderBlocks.renderTorchAtAngle (with
 * the metadata getIcon(side, meta) is called with), the Vec3 rotations the
 * lever and the tripwire hook share, and BlockFence.func_149826_e (which is
 * also BlockWall.func_150091_e). */
const struct rb_uv *rb_atlas_by_name(struct rb_mesher *m, const char *name);
int rb_solid_top_surface(struct rb_mesher *m, int x, int y, int z);
void rb_torch_at_angle(struct rb_mesher *m, int id, int meta, double x, double y, double z,
                       double p8, double p10);
void vec_rot_x(struct rb_mesher *m, double *x, double *y, double *z, float angle);
void vec_rot_y(struct rb_mesher *m, double *x, double *y, double *z, float angle);
int fence_connects(struct rb_mesher *m, int id, int x, int y, int z);
/* the door's and the stairs' parts (render_blocks2.c; the device mesher's
 * table mode draws them apart, cuda/meshing/table.h) */
int door_state(struct rb_mesher *m, int x, int y, int z);
void door_bounds(int state, double *out);
void door_icon(struct rb_mesher *m, int id, int side, int state, struct rb_uv *out);
int stairs_top_bounds(struct rb_mesher *m, int x, int y, int z, double *out);
int stairs_corner_bounds(struct rb_mesher *m, int x, int y, int z, double *out);
/* the liquids' corner height and flow angle (render_blocks.c; the table mode
 * draws a liquid's faces apart) */
float fluid_height(struct rb_mesher *m, int x, int y, int z, int mat);
float liquid_angle(struct rb_mesher *m, int x, int y, int z, int mat);

/* render_blocks3.c: the last render types. Same return convention as above. */
int rb3_render_bed(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_repeater(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_piston_base(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_piston_extension(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_pane(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_stem(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_vine(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_fence_gate(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_lily_pad(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_cauldron(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_brewing_stand(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_end_portal_frame(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_dragon_egg(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_cocoa(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_tripwire_source(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_tripwire(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_wall(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_flowerpot(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_beacon(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_anvil(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_anvil_meta(struct rb_mesher *m, int id, int x, int y, int z, int meta);
int rb3_render_comparator(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_hopper(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_quartz(struct rb_mesher *m, int id, int x, int y, int z);
int rb3_render_stained_glass_pane(struct rb_mesher *m, int id, int x, int y, int z);

/* The icon codes block_icon_world returns for the blocks whose getIcon reads
 * mutable state (the anvil's part, the piston base's bounds) or whose icons
 * table.bin cannot record; resolved into m->special3 at init. */
#define RB3_NOT_MINE (-999999)
#define RB3_CODE(i) (-1 - RB_SPECIALS - RB2_SPECIALS - (i))

enum {
    RB3_PISTON_SIDE = 0,
    RB3_PISTON_TOP_NORMAL,
    RB3_PISTON_TOP_STICKY,
    RB3_PISTON_INNER,
    RB3_PISTON_BOTTOM,
    RB3_ANVIL_BASE,
    RB3_ANVIL_DAMAGED_0,
    RB3_ANVIL_DAMAGED_1,
    RB3_ANVIL_DAMAGED_2,
    RB3_PANE_TOP,
    RB3_PANE_TOP_IRON,
    RB3_CAULDRON_INNER,
    RB3_WATER_STILL,
    RB3_BREWING_BASE,
    RB3_ENDFRAME_EYE,
    RB3_HOPPER_OUTSIDE,
    RB3_HOPPER_INSIDE,
    RB3_STEM_PUMPKIN_CONNECTED,
    RB3_STEM_MELON_CONNECTED,
    RB3_PANE_TOP_STAINED,           /* + 0..15, the per-colour pane tops */
    RB3_ICONS                       /* the count, also the fit check below */
};

typedef char rb3_icons_fit[(int)RB3_ICONS <= (int)RB3_SPECIALS ? 1 : -1];

/* Block.getIcon(side, meta) for the blocks above, called from
 * render_blocks.c's block_icon_world_index; RB3_NOT_MINE for every other id. */
int rb3_block_icon_index(struct rb_mesher *m, int id, int x, int y, int z, int side);

/* BlockPistonBase.setBlockBoundsBasedOnState: moves the block's stored bounds
 * (kept in m->piston_bounds) and copies them into the render bounds. Called
 * from the dispatch for ids 33 and 29 instead of the table bounds, because
 * the stored bounds depend on what the previous piston renders left there. */
void rb3_piston_dispatch_bounds(struct rb_mesher *m, int id, int meta);

/* BlockPistonBase.func_150070_b: copy the render bounds into the stored
 * bounds; the piston base renderer calls it where Java does. */
void rb3_piston_store_bounds(struct rb_mesher *m, int id);

/* Resolve rb3's icon names into m; called from rb_mesher_init. */
int rb3_init(struct rb_mesher *m);

#endif