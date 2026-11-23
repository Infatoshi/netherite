/* RenderBlocks render types 14 beds, 15 repeaters, 16 piston bases, 17 piston
 * extensions, 18 panes, 19 stems, 20 vines, 21 fence gates, 23 lily pads,
 * 24 cauldrons, 25 brewing stands, 26 end portal frames, 27 dragon eggs,
 * 28 cocoa, 29 tripwire hooks, 30 tripwire, 32 walls, 33 flower pots,
 * 34 beacons, 35 anvils, 37 comparators, 38 hoppers, 39 quartz pillars and
 * 41 stained glass panes, ported from RenderBlocks.java block by block like
 * render_blocks2.c's types. The comments name the Java method each function
 * reproduces; the arithmetic follows it operation by operation, including the
 * float/double split (Java computes geometry in double and casts to float only
 * where the Tessellator stores it) and the order of the multiplications.
 *
 * Shared helpers live in render_blocks.c and render_blocks2.c (declared in
 * render_blocks_int.h). What this file adds: the icons table.bin cannot hold
 * because getIcon(side, meta) never returns them (pane tops, cauldron inner,
 * water surface, brewing base, end frame eye, hopper, stem connected) or reads
 * mutable state (the anvil's current part, the piston base's bounds), resolved
 * from atlas.json by name at init into m->special3. The flower pot renderer
 * reads its tile entity through rb_world_tile_entity (the scene's
 * tileentities.bin); every other new renderer is a function of the chunk dump.
 */
#include "render_blocks_int.h"

#include "blocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

/* Direction's tables (the bed, the tripwire and its hook walk with them). */
static const int DIR_TO_FACING[4] = {3, 4, 2, 5};
static const int ROT_OPPOSITE[4] = {2, 3, 0, 1};
static const int DIR_OX[4] = {0, -1, 0, 1};
static const int DIR_OZ[4] = {1, 0, -1, 0};

static const int OPP_SIDE[6] = {1, 0, 3, 2, 5, 4};

/* ------------------------------------------------------------------- setup */

static int rb3_uv_for(struct rb_mesher *m, const char *name, int idx)
{
    const struct rb_uv *uv = rb_atlas_by_name(m, name);

    if (!uv)
    {
        fprintf(stderr, "render_blocks3: icon %s is not in atlas.json\n", name);
        return -1;
    }

    m->special3[idx] = *uv;
    return 0;
}

static int rb3_stem_connected(struct rb_mesher *m, int id, const char *name, int idx)
{
    /* BlockStem.func_149872_i: the "_connected" twin of the table's
     * "_disconnected" blockIcon */
    const char *icon = icon_name(m->tab, table_icon_index(m, id, 0, 0));
    char buf[64];
    snprintf(buf, sizeof buf, "%s", icon);
    size_t n = strlen(buf);

    if (n > 13 && strcmp(buf + n - 13, "_disconnected") == 0) strcpy(buf + n - 13, "_connected");

    return rb3_uv_for(m, buf, idx);
}

static int rb3_pane_top(struct rb_mesher *m, int meta, int idx)
{
    /* BlockStainedGlassPane.registerBlockIcons: textureName + "_pane_top_" +
     * the colour of field_150106_a[~meta & 15], which is the table icon's
     * suffix after its first '_' */
    const char *icon = icon_name(m->tab, table_icon_index(m, 160, meta, 0));
    const char *colour = strchr(icon, '_');
    char buf[64];
    snprintf(buf, sizeof buf, "glass_pane_top_%s", colour ? colour + 1 : icon);
    return rb3_uv_for(m, buf, idx);
}

int rb3_init(struct rb_mesher *m)
{
    static const struct { const char *name; int idx; } NAMED[] = {
        {"piston_side", RB3_PISTON_SIDE},
        {"piston_top_normal", RB3_PISTON_TOP_NORMAL},
        {"piston_top_sticky", RB3_PISTON_TOP_STICKY},
        {"piston_inner", RB3_PISTON_INNER},
        {"piston_bottom", RB3_PISTON_BOTTOM},
        {"anvil_base", RB3_ANVIL_BASE},
        {"anvil_top_damaged_0", RB3_ANVIL_DAMAGED_0},
        {"anvil_top_damaged_1", RB3_ANVIL_DAMAGED_1},
        {"anvil_top_damaged_2", RB3_ANVIL_DAMAGED_2},
        {"glass_pane_top", RB3_PANE_TOP},
        {"iron_bars", RB3_PANE_TOP_IRON},
        {"cauldron_inner", RB3_CAULDRON_INNER},
        {"water_still", RB3_WATER_STILL},
        {"brewing_stand_base", RB3_BREWING_BASE},
        {"endframe_eye", RB3_ENDFRAME_EYE},
        {"hopper_outside", RB3_HOPPER_OUTSIDE},
        {"hopper_inside", RB3_HOPPER_INSIDE},
    };

    for (int i = 0; i < (int)(sizeof NAMED / sizeof NAMED[0]); ++i)
    {
        if (rb3_uv_for(m, NAMED[i].name, NAMED[i].idx) != 0) return -1;
    }

    if (rb3_stem_connected(m, 104, "pumpkin_stem_connected", RB3_STEM_PUMPKIN_CONNECTED) != 0) return -1;
    if (rb3_stem_connected(m, 105, "melon_stem_connected", RB3_STEM_MELON_CONNECTED) != 0) return -1;

    for (int meta = 0; meta < 16; ++meta)
    {
        if (rb3_pane_top(m, meta, RB3_PANE_TOP_STAINED + meta) != 0) return -1;
    }

    return 0;
}

/* --------------------------------------------------------------- getIcon */

/** BlockAnvil.getIcon / BlockPistonBase.getIcon: the icons that depend on the
 * mutable state the renderers set (field_149833_b, the block bounds), which
 * table.bin's getIcon(side, meta) sweep could not pin down. */
int rb3_block_icon_index(struct rb_mesher *m, int id, int x, int y, int z, int side)
{
    /* the class names compared once (rb_block_class), not on every face */
    const int cls = rb_block_class(id);

    if (cls & RB_CLASS_ANVIL)
    {
        if (m->anvil_part == 3 && side == 1) return RB3_CODE(RB3_ANVIL_DAMAGED_0 + (rb_world_meta(m->w, x, y, z) >> 2) % 3);
        return RB3_CODE(RB3_ANVIL_BASE);
    }

    if (cls & RB_CLASS_PISTON_BASE)
    {
        int meta = rb_world_meta(m->w, x, y, z);
        int facing = meta & 7;
        int top = RB3_CODE(id == 29 ? RB3_PISTON_TOP_STICKY : RB3_PISTON_TOP_NORMAL);

        if (facing > 5) return top;

        if (side == facing)
        {
            int full = m->min_x <= 0.0 && m->max_x >= 1.0 && m->min_y <= 0.0
                && m->max_y >= 1.0 && m->min_z <= 0.0 && m->max_z >= 1.0;
            return (meta & 8) == 0 && full ? top : RB3_CODE(RB3_PISTON_INNER);
        }

        if (side == OPP_SIDE[facing]) return RB3_CODE(RB3_PISTON_BOTTOM);
        return RB3_CODE(RB3_PISTON_SIDE);
    }

    return RB3_NOT_MINE;
}

/* --------------------------------------------------------------- helpers */

/** World.isAirBlock. */
static int is_air(struct rb_mesher *m, int x, int y, int z)
{
    return strcmp(MATERIALS[BLOCKS[rb_world_block(m->w, x, y, z)].material].name, "air") == 0;
}

/** Block.isBlockNormalCube: blocksMovement && renderAsNormalBlock. */
static int block_normal_cube(struct rb_mesher *m, int x, int y, int z)
{
    int id = rb_world_block(m->w, x, y, z);
    return MATERIALS[BLOCKS[id].material].blocks_movement && BLOCKS[id].normal_block;
}

/** Tessellator.setColorOpaque_I. */
static void tess_set_color_opaque_i(struct rb_mesher *m, int32_t c)
{
    tess_set_color_opaque(m->t, c >> 16 & 255, c >> 8 & 255, c & 255);
}

/* ------------------------------------------------------------- the diodes */

/** BlockRedstoneDiode.func_149913_i: one input side. The scene's only
 * canProvidePower neighbours are redstone wire (whose meta is the strength)
 * and air; other power sources would need Block.isBlockProvidingPowerTo. */
static int diode_side_input(struct rb_mesher *m, int x, int y, int z, int side)
{
    (void)side;
    int nid = rb_world_block(m->w, x, y, z);

    if (!BLOCKS[nid].provides_power) return 0;
    if (nid == 55) return rb_world_meta(m->w, x, y, z);
    return 0;
}

/** BlockRedstoneDiode.func_149902_h. */
static int diode_input(struct rb_mesher *m, int x, int y, int z, int meta)
{
    int facing = meta & 3;

    if (facing == 0 || facing == 2)
    {
        int a = diode_side_input(m, x - 1, y, z, 4);
        int b = diode_side_input(m, x + 1, y, z, 5);
        return a > b ? a : b;
    }

    int a = diode_side_input(m, x, y, z + 1, 3);
    int b = diode_side_input(m, x, y, z - 1, 2);
    return a > b ? a : b;
}

/** RenderBlocks.renderBlockRedstoneDiodeMetadata. */
static void render_diode_slab(struct rb_mesher *m, int id, int x, int y, int z, int facing)
{
    struct rb_tess *t = m->t;
    render_standard_block(m, id, x, y, z);
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    int meta = rb_world_meta(m->w, x, y, z);
    const struct rb_uv *var8 = icon_from_side_meta(m, id, 1, meta);
    double var9 = (double)var8->min_u;
    double var11 = (double)var8->max_u;
    double var13 = (double)var8->min_v;
    double var15 = (double)var8->max_v;
    double var17 = 0.125;
    double var19 = (double)(x + 1);
    double var21 = (double)(x + 1);
    double var23 = (double)(x + 0);
    double var25 = (double)(x + 0);
    double var27 = (double)(z + 0);
    double var29 = (double)(z + 1);
    double var31 = (double)(z + 1);
    double var33 = (double)(z + 0);
    double var35 = (double)y + var17;

    if (facing == 2)
    {
        var19 = var21 = (double)(x + 0);
        var23 = var25 = (double)(x + 1);
        var27 = var33 = (double)(z + 1);
        var29 = var31 = (double)(z + 0);
    }
    else if (facing == 3)
    {
        var19 = var25 = (double)(x + 0);
        var21 = var23 = (double)(x + 1);
        var27 = var29 = (double)(z + 0);
        var31 = var33 = (double)(z + 1);
    }
    else if (facing == 1)
    {
        var19 = var25 = (double)(x + 1);
        var21 = var23 = (double)(x + 0);
        var27 = var29 = (double)(z + 1);
        var31 = var33 = (double)(z + 0);
    }

    tess_add_vertex_with_uv(t, var25, var35, var33, var9, var13);
    tess_add_vertex_with_uv(t, var23, var35, var31, var9, var15);
    tess_add_vertex_with_uv(t, var21, var35, var29, var11, var15);
    tess_add_vertex_with_uv(t, var19, var35, var27, var11, var13);
}

/** RenderBlocks.renderBlockRepeater. */
int rb3_render_repeater(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    int var6 = meta & 3;
    int var7 = (meta & 12) >> 2;
    static const double DELAY[4] = {-0.0625, 0.0625, 0.1875, 0.3125};
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    double var9 = -0.1875;
    int var11 = diode_input(m, x, y, z, meta) > 0;
    double var12 = 0.0;
    double var14 = 0.0;
    double var16 = 0.0;
    double var18 = 0.0;

    switch (var6)
    {
        case 0: var18 = -0.3125; var14 = DELAY[var7]; break;
        case 1: var16 = 0.3125; var12 = -DELAY[var7]; break;
        case 2: var18 = 0.3125; var14 = -DELAY[var7]; break;
        case 3: var16 = -0.3125; var12 = DELAY[var7]; break;
    }

    if (!var11)
    {
        rb_torch_at_angle(m, id, 0, (double)x + var12, (double)y + var9, (double)z + var14, 0.0, 0.0);
    }
    else
    {
        const struct rb_uv *var20 = icon_from_side(m, 7, 1);   /* getBlockIcon(Blocks.bedrock) */
        m->override = var20;
        float var21 = 2.0F;
        float var22 = 14.0F;
        float var23 = 7.0F;
        float var24 = 9.0F;

        if (var6 == 1 || var6 == 3) { var21 = 7.0F; var22 = 9.0F; var23 = 2.0F; var24 = 14.0F; }

        rb2_set_render_bounds(m, (double)(var21 / 16.0F + (float)var12), 0.125, (double)(var23 / 16.0F + (float)var14),
                              (double)(var22 / 16.0F + (float)var12), 0.25, (double)(var24 / 16.0F + (float)var14));
        double var25 = (double)interp_u(var20, (double)var21);
        double var27 = (double)interp_v(var20, (double)var23);
        double var29 = (double)interp_u(var20, (double)var22);
        double var31 = (double)interp_v(var20, (double)var24);
        tess_add_vertex_with_uv(t, (double)((float)x + var21 / 16.0F) + var12, (double)((float)y + 0.25F), (double)((float)z + var23 / 16.0F) + var14, var25, var27);
        tess_add_vertex_with_uv(t, (double)((float)x + var21 / 16.0F) + var12, (double)((float)y + 0.25F), (double)((float)z + var24 / 16.0F) + var14, var25, var31);
        tess_add_vertex_with_uv(t, (double)((float)x + var22 / 16.0F) + var12, (double)((float)y + 0.25F), (double)((float)z + var24 / 16.0F) + var14, var29, var31);
        tess_add_vertex_with_uv(t, (double)((float)x + var22 / 16.0F) + var12, (double)((float)y + 0.25F), (double)((float)z + var23 / 16.0F) + var14, var29, var27);
        render_standard_block(m, id, x, y, z);
        rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 0.125, 1.0);
        m->override = NULL;
    }

    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    rb_torch_at_angle(m, id, 0, (double)x + var16, (double)y + var9, (double)z + var18, 0.0, 0.0);
    render_diode_slab(m, id, x, y, z, meta & 3);
    return 1;
}

/** RenderBlocks.renderBlockRedstoneComparator. */
int rb3_render_comparator(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    int var6 = rb_world_meta(m->w, x, y, z);
    int var7 = var6 & 3;
    double var8 = 0.0;
    double var10 = -0.1875;
    double var12 = 0.0;
    double var14 = 0.0;
    double var16 = 0.0;
    const struct rb_uv *var18;

    if ((var6 & 4) == 4)
    {
        var18 = icon_from_side(m, 76, 0);      /* Blocks.redstone_torch */
    }
    else
    {
        var10 -= 0.1875;
        var18 = icon_from_side(m, 75, 0);      /* Blocks.unlit_redstone_torch */
    }

    switch (var7)
    {
        case 0: var12 = -0.3125; var16 = 1.0; break;
        case 1: var8 = 0.3125; var14 = -1.0; break;
        case 2: var12 = 0.3125; var16 = -1.0; break;
        case 3: var8 = -0.3125; var14 = 1.0; break;
    }

    /* the torch icon is getIcon(0, var6): the block id and meta bit 3 pick the
     * lit or unlit torch; table.bin holds it per meta */
    rb_torch_at_angle(m, id, var6, (double)x + 0.25 * var14 + 0.1875 * var16, (double)((float)y - 0.1875F), (double)z + 0.25 * var16 + 0.1875 * var14, 0.0, 0.0);
    rb_torch_at_angle(m, id, var6, (double)x + 0.25 * var14 + -0.1875 * var16, (double)((float)y - 0.1875F), (double)z + 0.25 * var16 + -0.1875 * var14, 0.0, 0.0);
    m->override = var18;
    rb_torch_at_angle(m, id, var6, (double)x + var8, (double)y + var10, (double)z + var12, 0.0, 0.0);
    m->override = NULL;
    render_diode_slab(m, id, x, y, z, var7);
    return 1;
}

/* ------------------------------------------------------------------ beds */

/** RenderBlocks.renderBlockBed. */
int rb3_render_bed(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    int var7 = meta & 3;                     /* BlockBed.func_149895_l */
    int var8 = (meta & 8) != 0;              /* BlockBed.func_149975_b: the head */
    float var9 = 0.5F;
    float var10 = 1.0F;
    float var11 = 0.8F;
    float var12 = 0.6F;
    int var25 = block_brightness(m, id, x, y, z);
    tess_set_brightness(t, var25);
    tess_set_color_opaque_f(t, var9, var9, var9);
    const struct rb_uv *var26 = block_icon_world(m, id, x, y, z, 0);
    double var27 = (double)var26->min_u;
    double var29 = (double)var26->max_u;
    double var31 = (double)var26->min_v;
    double var33 = (double)var26->max_v;
    double var35 = (double)x + m->min_x;
    double var37 = (double)x + m->max_x;
    double var39 = (double)y + m->min_y + 0.1875;
    double var41 = (double)z + m->min_z;
    double var43 = (double)z + m->max_z;
    tess_add_vertex_with_uv(t, var35, var39, var43, var27, var33);
    tess_add_vertex_with_uv(t, var35, var39, var41, var27, var31);
    tess_add_vertex_with_uv(t, var37, var39, var41, var29, var31);
    tess_add_vertex_with_uv(t, var37, var39, var43, var29, var33);

    tess_set_brightness(t, block_brightness(m, id, x, y + 1, z));
    tess_set_color_opaque_f(t, var10, var10, var10);
    var26 = block_icon_world(m, id, x, y, z, 1);
    var27 = (double)var26->min_u;
    var29 = (double)var26->max_u;
    var31 = (double)var26->min_v;
    var33 = (double)var26->max_v;
    var35 = var27;
    var37 = var29;
    var39 = var31;
    var41 = var31;
    var43 = var27;
    double var45 = var29;
    double var47 = var33;
    double var49 = var33;

    if (var7 == 0)
    {
        var37 = var27;
        var39 = var33;
        var43 = var29;
        var49 = var31;
    }
    else if (var7 == 2)
    {
        var35 = var29;
        var41 = var33;
        var45 = var27;
        var47 = var31;
    }
    else if (var7 == 3)
    {
        var35 = var29;
        var41 = var33;
        var45 = var27;
        var47 = var31;
        var37 = var27;
        var39 = var33;
        var43 = var29;
        var49 = var31;
    }

    double var51 = (double)x + m->min_x;
    double var53 = (double)x + m->max_x;
    double var55 = (double)y + m->max_y;
    double var57 = (double)z + m->min_z;
    double var59 = (double)z + m->max_z;
    tess_add_vertex_with_uv(t, var53, var55, var59, var43, var47);
    tess_add_vertex_with_uv(t, var53, var55, var57, var35, var39);
    tess_add_vertex_with_uv(t, var51, var55, var57, var37, var41);
    tess_add_vertex_with_uv(t, var51, var55, var59, var45, var49);
    int var61 = DIR_TO_FACING[var7];

    if (var8) var61 = DIR_TO_FACING[ROT_OPPOSITE[var7]];

    int var62 = 4;

    switch (var7)
    {
        case 0: var62 = 5; break;
        case 1: var62 = 3; break;   /* Java falls through into case 2, which does nothing */
        case 3: var62 = 2; break;
    }

    if (var61 != 2 && (m->render_all_faces || should_side_be_rendered(m, id, x, y, z - 1, 2)))
    {
        tess_set_brightness(t, m->min_z > 0.0 ? var25 : block_brightness(m, id, x, y, z - 1));
        tess_set_color_opaque_f(t, var11, var11, var11);
        m->flip_texture = var62 == 2;
        render_face_z_neg(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 2));
    }

    if (var61 != 3 && (m->render_all_faces || should_side_be_rendered(m, id, x, y, z + 1, 3)))
    {
        tess_set_brightness(t, m->max_z < 1.0 ? var25 : block_brightness(m, id, x, y, z + 1));
        tess_set_color_opaque_f(t, var11, var11, var11);
        m->flip_texture = var62 == 3;
        render_face_z_pos(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 3));
    }

    if (var61 != 4 && (m->render_all_faces || should_side_be_rendered(m, id, x - 1, y, z, 4)))
    {
        tess_set_brightness(t, m->min_z > 0.0 ? var25 : block_brightness(m, id, x - 1, y, z));
        tess_set_color_opaque_f(t, var12, var12, var12);
        m->flip_texture = var62 == 4;
        render_face_x_neg(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 4));
    }

    if (var61 != 5 && (m->render_all_faces || should_side_be_rendered(m, id, x + 1, y, z, 5)))
    {
        tess_set_brightness(t, m->max_z < 1.0 ? var25 : block_brightness(m, id, x + 1, y, z));
        tess_set_color_opaque_f(t, var12, var12, var12);
        m->flip_texture = var62 == 5;
        render_face_x_pos(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 5));
    }

    m->flip_texture = 0;
    return 1;
}

/* --------------------------------------------------------------- pistons */

/** BlockPistonBase.setBlockBoundsBasedOnState, onto the stored bounds. */
void rb3_piston_dispatch_bounds(struct rb_mesher *m, int id, int meta)
{
    double *b = m->piston_bounds[id == 29];

    if ((meta & 8) != 0)
    {
        switch (meta & 7)
        {
            case 0: b[0] = 0.0; b[1] = 0.25; b[2] = 0.0; b[3] = 1.0; b[4] = 1.0; b[5] = 1.0; break;
            case 1: b[0] = 0.0; b[1] = 0.0; b[2] = 0.0; b[3] = 1.0; b[4] = 0.75; b[5] = 1.0; break;
            case 2: b[0] = 0.0; b[1] = 0.0; b[2] = 0.25; b[3] = 1.0; b[4] = 1.0; b[5] = 1.0; break;
            case 3: b[0] = 0.0; b[1] = 0.0; b[2] = 0.0; b[3] = 1.0; b[4] = 1.0; b[5] = 0.75; break;
            case 4: b[0] = 0.25; b[1] = 0.0; b[2] = 0.0; b[3] = 1.0; b[4] = 1.0; b[5] = 1.0; break;
            case 5: b[0] = 0.0; b[1] = 0.0; b[2] = 0.0; b[3] = 0.75; b[4] = 1.0; b[5] = 1.0; break;
            default: break;   /* facing 6 and 7 match no case: the bounds stay */
        }
    }
    else
    {
        b[0] = 0.0; b[1] = 0.0; b[2] = 0.0; b[3] = 1.0; b[4] = 1.0; b[5] = 1.0;
    }

    m->min_x = b[0];
    m->min_y = b[1];
    m->min_z = b[2];
    m->max_x = b[3];
    m->max_y = b[4];
    m->max_z = b[5];
    m->partial = m->min_x > 0.0 || m->max_x < 1.0 || m->min_y > 0.0 || m->max_y < 1.0
        || m->min_z > 0.0 || m->max_z < 1.0;
}

/** BlockPistonBase.func_150070_b. */
void rb3_piston_store_bounds(struct rb_mesher *m, int id)
{
    double *b = m->piston_bounds[id == 29];
    b[0] = m->min_x;
    b[1] = m->min_y;
    b[2] = m->min_z;
    b[3] = m->max_x;
    b[4] = m->max_y;
    b[5] = m->max_z;
}

/** RenderBlocks.renderPistonBase. */
int rb3_render_piston_base(struct rb_mesher *m, int id, int x, int y, int z)
{
    int meta = rb_world_meta(m->w, x, y, z);
    int var7 = (meta & 8) != 0;
    int var8 = meta & 7;

    if (var7)
    {
        switch (var8)
        {
            case 0:
                m->uv_east = 3; m->uv_west = 3; m->uv_south = 3; m->uv_north = 3;
                rb2_set_render_bounds(m, 0.0, 0.25, 0.0, 1.0, 1.0, 1.0);
                break;

            case 1:
                rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 0.75, 1.0);
                break;

            case 2:
                m->uv_south = 1; m->uv_north = 2;
                rb2_set_render_bounds(m, 0.0, 0.0, 0.25, 1.0, 1.0, 1.0);
                break;

            case 3:
                m->uv_south = 2; m->uv_north = 1; m->uv_top = 3; m->uv_bottom = 3;
                rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 0.75);
                break;

            case 4:
                m->uv_east = 1; m->uv_west = 2; m->uv_top = 2; m->uv_bottom = 1;
                rb2_set_render_bounds(m, 0.25, 0.0, 0.0, 1.0, 1.0, 1.0);
                break;

            case 5:
                m->uv_east = 2; m->uv_west = 1; m->uv_top = 1; m->uv_bottom = 2;
                rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 0.75, 1.0, 1.0);
                break;
        }

        /* func_150070_b copies the render bounds into the block, which its
         * getIcon reads through block_icon_world's m-bounds test */
        rb3_piston_store_bounds(m, id);
        render_standard_block(m, id, x, y, z);
        m->uv_east = 0; m->uv_west = 0; m->uv_south = 0; m->uv_north = 0; m->uv_top = 0; m->uv_bottom = 0;
        rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        rb3_piston_store_bounds(m, id);
    }
    else
    {
        switch (var8)
        {
            case 0:
                m->uv_east = 3; m->uv_west = 3; m->uv_south = 3; m->uv_north = 3;
                break;

            case 2:
                m->uv_south = 1; m->uv_north = 2;
                break;

            case 3:
                m->uv_south = 2; m->uv_north = 1; m->uv_top = 3; m->uv_bottom = 3;
                break;

            case 4:
                m->uv_east = 1; m->uv_west = 2; m->uv_top = 2; m->uv_bottom = 1;
                break;

            case 5:
                m->uv_east = 2; m->uv_west = 1; m->uv_top = 1; m->uv_bottom = 2;
                break;
        }

        render_standard_block(m, id, x, y, z);
        m->uv_east = 0; m->uv_west = 0; m->uv_south = 0; m->uv_north = 0; m->uv_top = 0; m->uv_bottom = 0;
    }

    return 1;
}

/** RenderBlocks.renderPistonRodUD. */
static void piston_rod_ud(struct rb_mesher *m, double p1, double p3, double p5, double p7,
                          double p9, double p11, float p13, double p14)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var16 = &m->special3[RB3_PISTON_SIDE];

    if (m->override) var16 = m->override;

    double var18 = (double)var16->min_u;
    double var20 = (double)var16->min_v;
    double var22 = (double)interp_u(var16, p14);
    double var24 = (double)interp_v(var16, 4.0);
    tess_set_color_opaque_f(t, p13, p13, p13);
    tess_add_vertex_with_uv(t, p1, p7, p9, var22, var20);
    tess_add_vertex_with_uv(t, p1, p5, p9, var18, var20);
    tess_add_vertex_with_uv(t, p3, p5, p11, var18, var24);
    tess_add_vertex_with_uv(t, p3, p7, p11, var22, var24);
}

/** RenderBlocks.renderPistonRodSN. */
static void piston_rod_sn(struct rb_mesher *m, double p1, double p3, double p5, double p7,
                          double p9, double p11, float p13, double p14)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var16 = &m->special3[RB3_PISTON_SIDE];

    if (m->override) var16 = m->override;

    double var18 = (double)var16->min_u;
    double var20 = (double)var16->min_v;
    double var22 = (double)interp_u(var16, p14);
    double var24 = (double)interp_v(var16, 4.0);
    tess_set_color_opaque_f(t, p13, p13, p13);
    tess_add_vertex_with_uv(t, p1, p5, p11, var22, var20);
    tess_add_vertex_with_uv(t, p1, p5, p9, var18, var20);
    tess_add_vertex_with_uv(t, p3, p7, p9, var18, var24);
    tess_add_vertex_with_uv(t, p3, p7, p11, var22, var24);
}

/** RenderBlocks.renderPistonRodEW. */
static void piston_rod_ew(struct rb_mesher *m, double p1, double p3, double p5, double p7,
                          double p9, double p11, float p13, double p14)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var16 = &m->special3[RB3_PISTON_SIDE];

    if (m->override) var16 = m->override;

    double var18 = (double)var16->min_u;
    double var20 = (double)var16->min_v;
    double var22 = (double)interp_u(var16, p14);
    double var24 = (double)interp_v(var16, 4.0);
    tess_set_color_opaque_f(t, p13, p13, p13);
    tess_add_vertex_with_uv(t, p3, p5, p9, var22, var20);
    tess_add_vertex_with_uv(t, p1, p5, p9, var18, var20);
    tess_add_vertex_with_uv(t, p1, p7, p11, var18, var24);
    tess_add_vertex_with_uv(t, p3, p7, p11, var22, var24);
}

/** RenderBlocks.renderPistonExtension. */
int rb3_render_piston_extension(struct rb_mesher *m, int id, int x, int y, int z)
{
    int var6 = rb_world_meta(m->w, x, y, z);
    int var7 = var6 & 7;
    /* the dispatch calls renderPistonExtension with p_147809_5_ true, so the
     * rod is a full block long */
    float var11 = 1.0F;
    double var12 = 16.0;

    switch (var7)
    {
        case 0:
            m->uv_east = 3; m->uv_west = 3; m->uv_south = 3; m->uv_north = 3;
            rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 0.25, 1.0);
            render_standard_block(m, id, x, y, z);
            piston_rod_ud(m, (double)((float)x + 0.375F), (double)((float)x + 0.625F), (double)((float)y + 0.25F), (double)((float)y + 0.25F + var11), (double)((float)z + 0.625F), (double)((float)z + 0.625F), 0.8F, var12);
            piston_rod_ud(m, (double)((float)x + 0.625F), (double)((float)x + 0.375F), (double)((float)y + 0.25F), (double)((float)y + 0.25F + var11), (double)((float)z + 0.375F), (double)((float)z + 0.375F), 0.8F, var12);
            piston_rod_ud(m, (double)((float)x + 0.375F), (double)((float)x + 0.375F), (double)((float)y + 0.25F), (double)((float)y + 0.25F + var11), (double)((float)z + 0.375F), (double)((float)z + 0.625F), 0.6F, var12);
            piston_rod_ud(m, (double)((float)x + 0.625F), (double)((float)x + 0.625F), (double)((float)y + 0.25F), (double)((float)y + 0.25F + var11), (double)((float)z + 0.625F), (double)((float)z + 0.375F), 0.6F, var12);
            break;

        case 1:
            rb2_set_render_bounds(m, 0.0, 0.75, 0.0, 1.0, 1.0, 1.0);
            render_standard_block(m, id, x, y, z);
            piston_rod_ud(m, (double)((float)x + 0.375F), (double)((float)x + 0.625F), (double)((float)y - 0.25F + 1.0F - var11), (double)((float)y - 0.25F + 1.0F), (double)((float)z + 0.625F), (double)((float)z + 0.625F), 0.8F, var12);
            piston_rod_ud(m, (double)((float)x + 0.625F), (double)((float)x + 0.375F), (double)((float)y - 0.25F + 1.0F - var11), (double)((float)y - 0.25F + 1.0F), (double)((float)z + 0.375F), (double)((float)z + 0.375F), 0.8F, var12);
            piston_rod_ud(m, (double)((float)x + 0.375F), (double)((float)x + 0.375F), (double)((float)y - 0.25F + 1.0F - var11), (double)((float)y - 0.25F + 1.0F), (double)((float)z + 0.375F), (double)((float)z + 0.625F), 0.6F, var12);
            piston_rod_ud(m, (double)((float)x + 0.625F), (double)((float)x + 0.625F), (double)((float)y - 0.25F + 1.0F - var11), (double)((float)y - 0.25F + 1.0F), (double)((float)z + 0.625F), (double)((float)z + 0.375F), 0.6F, var12);
            break;

        case 2:
            m->uv_south = 1; m->uv_north = 2;
            rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 0.25);
            render_standard_block(m, id, x, y, z);
            piston_rod_sn(m, (double)((float)x + 0.375F), (double)((float)x + 0.375F), (double)((float)z + 0.625F), (double)((float)z + 0.375F), (double)((float)y + 0.25F), (double)((float)y + 0.25F + var11), 0.6F, var12);
            piston_rod_sn(m, (double)((float)x + 0.625F), (double)((float)x + 0.625F), (double)((float)z + 0.375F), (double)((float)z + 0.625F), (double)((float)y + 0.25F), (double)((float)y + 0.25F + var11), 0.6F, var12);
            piston_rod_sn(m, (double)((float)x + 0.375F), (double)((float)x + 0.625F), (double)((float)z + 0.375F), (double)((float)z + 0.375F), (double)((float)y + 0.25F), (double)((float)y + 0.25F + var11), 0.5F, var12);
            piston_rod_sn(m, (double)((float)x + 0.625F), (double)((float)x + 0.375F), (double)((float)z + 0.625F), (double)((float)z + 0.625F), (double)((float)y + 0.25F), (double)((float)y + 0.25F + var11), 1.0F, var12);
            break;

        case 3:
            m->uv_south = 2; m->uv_north = 1; m->uv_top = 3; m->uv_bottom = 3;
            rb2_set_render_bounds(m, 0.0, 0.0, 0.75, 1.0, 1.0, 1.0);
            render_standard_block(m, id, x, y, z);
            piston_rod_sn(m, (double)((float)x + 0.375F), (double)((float)x + 0.375F), (double)((float)z + 0.625F), (double)((float)z + 0.375F), (double)((float)y - 0.25F + 1.0F - var11), (double)((float)y - 0.25F + 1.0F), 0.6F, var12);
            piston_rod_sn(m, (double)((float)x + 0.625F), (double)((float)x + 0.625F), (double)((float)z + 0.375F), (double)((float)z + 0.625F), (double)((float)y - 0.25F + 1.0F - var11), (double)((float)y - 0.25F + 1.0F), 0.6F, var12);
            piston_rod_sn(m, (double)((float)x + 0.375F), (double)((float)x + 0.625F), (double)((float)z + 0.375F), (double)((float)z + 0.375F), (double)((float)y - 0.25F + 1.0F - var11), (double)((float)y - 0.25F + 1.0F), 0.5F, var12);
            piston_rod_sn(m, (double)((float)x + 0.625F), (double)((float)x + 0.375F), (double)((float)z + 0.625F), (double)((float)z + 0.625F), (double)((float)y - 0.25F + 1.0F - var11), (double)((float)y - 0.25F + 1.0F), 1.0F, var12);
            break;

        case 4:
            m->uv_east = 1; m->uv_west = 2; m->uv_top = 2; m->uv_bottom = 1;
            rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 0.25, 1.0, 1.0);
            render_standard_block(m, id, x, y, z);
            piston_rod_ew(m, (double)((float)x + 0.25F), (double)((float)x + 0.25F + var11), (double)((float)y + 0.375F), (double)((float)y + 0.375F), (double)((float)z + 0.625F), (double)((float)z + 0.375F), 0.5F, var12);
            piston_rod_ew(m, (double)((float)x + 0.25F), (double)((float)x + 0.25F + var11), (double)((float)y + 0.625F), (double)((float)y + 0.625F), (double)((float)z + 0.375F), (double)((float)z + 0.375F), 1.0F, var12);
            piston_rod_ew(m, (double)((float)x + 0.25F), (double)((float)x + 0.25F + var11), (double)((float)y + 0.375F), (double)((float)y + 0.625F), (double)((float)z + 0.375F), (double)((float)z + 0.375F), 0.6F, var12);
            piston_rod_ew(m, (double)((float)x + 0.25F), (double)((float)x + 0.25F + var11), (double)((float)y + 0.625F), (double)((float)y + 0.375F), (double)((float)z + 0.625F), (double)((float)z + 0.625F), 0.6F, var12);
            break;

        case 5:
            m->uv_east = 2; m->uv_west = 1; m->uv_top = 1; m->uv_bottom = 2;
            rb2_set_render_bounds(m, 0.75, 0.0, 0.0, 1.0, 1.0, 1.0);
            render_standard_block(m, id, x, y, z);
            piston_rod_ew(m, (double)((float)x - 0.25F + 1.0F - var11), (double)((float)x - 0.25F + 1.0F), (double)((float)y + 0.375F), (double)((float)y + 0.375F), (double)((float)z + 0.625F), (double)((float)z + 0.375F), 0.5F, var12);
            piston_rod_ew(m, (double)((float)x - 0.25F + 1.0F - var11), (double)((float)x - 0.25F + 1.0F), (double)((float)y + 0.625F), (double)((float)y + 0.625F), (double)((float)z + 0.375F), (double)((float)z + 0.375F), 1.0F, var12);
            piston_rod_ew(m, (double)((float)x - 0.25F + 1.0F - var11), (double)((float)x - 0.25F + 1.0F), (double)((float)y + 0.375F), (double)((float)y + 0.625F), (double)((float)z + 0.375F), (double)((float)z + 0.375F), 0.6F, var12);
            piston_rod_ew(m, (double)((float)x - 0.25F + 1.0F - var11), (double)((float)x - 0.25F + 1.0F), (double)((float)y + 0.625F), (double)((float)y + 0.375F), (double)((float)z + 0.625F), (double)((float)z + 0.625F), 0.6F, var12);
            break;
    }

    m->uv_east = 0; m->uv_west = 0; m->uv_south = 0; m->uv_north = 0; m->uv_top = 0; m->uv_bottom = 0;
    rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    return 1;
}

/* ----------------------------------------------------------------- stems */

/** BlockStem.func_149873_e: the fruit's direction, or -1. */
static int stem_direction(struct rb_mesher *m, int id, int meta, int x, int y, int z)
{
    if (meta < 7) return -1;
    int fruit = strcmp(BLOCKS[id].name, "minecraft:pumpkin_stem") == 0 ? 86 : 103;

    if (rb_world_block(m->w, x - 1, y, z) == fruit) return 0;
    if (rb_world_block(m->w, x + 1, y, z) == fruit) return 1;
    if (rb_world_block(m->w, x, y, z - 1) == fruit) return 2;
    if (rb_world_block(m->w, x, y, z + 1) == fruit) return 3;
    return -1;
}

/** RenderBlocks.renderBlockStemSmall. */
static void render_stem_small(struct rb_mesher *m, int id, int meta, double height,
                              double x, double y, double z)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var12 = icon_from_side_meta(m, id, 0, meta);

    if (m->override) var12 = m->override;

    double var13 = (double)var12->min_u;
    double var15 = (double)var12->min_v;
    double var17 = (double)var12->max_u;
    double var19 = (double)interp_v(var12, height * 16.0);
    double var21 = x + 0.5 - 0.44999998807907104;
    double var23 = x + 0.5 + 0.44999998807907104;
    double var25 = z + 0.5 - 0.44999998807907104;
    double var27 = z + 0.5 + 0.44999998807907104;
    tess_add_vertex_with_uv(t, var21, y + height, var25, var13, var15);
    tess_add_vertex_with_uv(t, var21, y + 0.0, var25, var13, var19);
    tess_add_vertex_with_uv(t, var23, y + 0.0, var27, var17, var19);
    tess_add_vertex_with_uv(t, var23, y + height, var27, var17, var15);
    tess_add_vertex_with_uv(t, var23, y + height, var27, var17, var15);
    tess_add_vertex_with_uv(t, var23, y + 0.0, var27, var17, var19);
    tess_add_vertex_with_uv(t, var21, y + 0.0, var25, var13, var19);
    tess_add_vertex_with_uv(t, var21, y + height, var25, var13, var15);
    tess_add_vertex_with_uv(t, var21, y + height, var27, var13, var15);
    tess_add_vertex_with_uv(t, var21, y + 0.0, var27, var13, var19);
    tess_add_vertex_with_uv(t, var23, y + 0.0, var25, var17, var19);
    tess_add_vertex_with_uv(t, var23, y + height, var25, var17, var15);
    tess_add_vertex_with_uv(t, var23, y + height, var25, var17, var15);
    tess_add_vertex_with_uv(t, var23, y + 0.0, var25, var17, var19);
    tess_add_vertex_with_uv(t, var21, y + 0.0, var27, var13, var19);
    tess_add_vertex_with_uv(t, var21, y + height, var27, var13, var15);
}

/** RenderBlocks.renderBlockStemBig. */
static void render_stem_big(struct rb_mesher *m, int id, int meta, int dir, double height,
                            double x, double y, double z)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var13 = &m->special3[id == 104 ? RB3_STEM_PUMPKIN_CONNECTED : RB3_STEM_MELON_CONNECTED];

    if (m->override) var13 = m->override;

    double var14 = (double)var13->min_u;
    double var16 = (double)var13->min_v;
    double var18 = (double)var13->max_u;
    double var20 = (double)var13->max_v;
    double var22 = x + 0.5 - 0.5;
    double var24 = x + 0.5 + 0.5;
    double var26 = z + 0.5 - 0.5;
    double var28 = z + 0.5 + 0.5;
    double var30 = x + 0.5;
    double var32 = z + 0.5;

    if ((dir + 1) / 2 % 2 == 1)
    {
        double var34 = var18;
        var18 = var14;
        var14 = var34;
    }

    if (dir < 2)
    {
        tess_add_vertex_with_uv(t, var22, y + height, var32, var14, var16);
        tess_add_vertex_with_uv(t, var22, y + 0.0, var32, var14, var20);
        tess_add_vertex_with_uv(t, var24, y + 0.0, var32, var18, var20);
        tess_add_vertex_with_uv(t, var24, y + height, var32, var18, var16);
        tess_add_vertex_with_uv(t, var24, y + height, var32, var18, var16);
        tess_add_vertex_with_uv(t, var24, y + 0.0, var32, var18, var20);
        tess_add_vertex_with_uv(t, var22, y + 0.0, var32, var14, var20);
        tess_add_vertex_with_uv(t, var22, y + height, var32, var14, var16);
    }
    else
    {
        tess_add_vertex_with_uv(t, var30, y + height, var28, var14, var16);
        tess_add_vertex_with_uv(t, var30, y + 0.0, var28, var14, var20);
        tess_add_vertex_with_uv(t, var30, y + 0.0, var26, var18, var20);
        tess_add_vertex_with_uv(t, var30, y + height, var26, var18, var16);
        tess_add_vertex_with_uv(t, var30, y + height, var26, var18, var16);
        tess_add_vertex_with_uv(t, var30, y + 0.0, var26, var18, var20);
        tess_add_vertex_with_uv(t, var30, y + 0.0, var28, var14, var20);
        tess_add_vertex_with_uv(t, var30, y + height, var28, var14, var16);
    }
}

/** RenderBlocks.renderBlockStem. */
int rb3_render_stem(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    int var7 = color_multiplier(m, id, x, y, z);
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, (float)(var7 >> 16 & 255) / 255.0F,
                            (float)(var7 >> 8 & 255) / 255.0F, (float)(var7 & 255) / 255.0F);
    /* setBlockBoundsBasedOnState: the table bounds already hold it */
    int var14 = stem_direction(m, id, meta, x, y, z);

    if (var14 < 0)
    {
        render_stem_small(m, id, meta, m->max_y, (double)x, (double)((float)y - 0.0625F), (double)z);
    }
    else
    {
        render_stem_small(m, id, meta, 0.5, (double)x, (double)((float)y - 0.0625F), (double)z);
        render_stem_big(m, id, meta, var14, m->max_y, (double)x, (double)((float)y - 0.0625F), (double)z);
    }

    return 1;
}

/* ----------------------------------------------------------------- vines */

/** RenderBlocks.renderBlockVine. */
int rb3_render_vine(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var6 = icon_from_side(m, id, 0);

    if (m->override) var6 = m->override;

    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int var7 = color_multiplier(m, id, x, y, z);
    float var8 = (float)(var7 >> 16 & 255) / 255.0F;
    float var9 = (float)(var7 >> 8 & 255) / 255.0F;
    float var10 = (float)(var7 & 255) / 255.0F;
    tess_set_color_opaque_f(t, var8, var9, var10);
    double var18 = (double)var6->min_u;
    double var19 = (double)var6->min_v;
    double var11 = (double)var6->max_u;
    double var13 = (double)var6->max_v;
    double var15 = 0.05000000074505806;
    int var17 = rb_world_meta(m->w, x, y, z);

    if ((var17 & 2) != 0)
    {
        tess_add_vertex_with_uv(t, (double)x + var15, (double)(y + 1), (double)(z + 1), var18, var19);
        tess_add_vertex_with_uv(t, (double)x + var15, (double)(y + 0), (double)(z + 1), var18, var13);
        tess_add_vertex_with_uv(t, (double)x + var15, (double)(y + 0), (double)(z + 0), var11, var13);
        tess_add_vertex_with_uv(t, (double)x + var15, (double)(y + 1), (double)(z + 0), var11, var19);
        tess_add_vertex_with_uv(t, (double)x + var15, (double)(y + 1), (double)(z + 0), var11, var19);
        tess_add_vertex_with_uv(t, (double)x + var15, (double)(y + 0), (double)(z + 0), var11, var13);
        tess_add_vertex_with_uv(t, (double)x + var15, (double)(y + 0), (double)(z + 1), var18, var13);
        tess_add_vertex_with_uv(t, (double)x + var15, (double)(y + 1), (double)(z + 1), var18, var19);
    }

    if ((var17 & 8) != 0)
    {
        tess_add_vertex_with_uv(t, (double)(x + 1) - var15, (double)(y + 0), (double)(z + 1), var11, var13);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var15, (double)(y + 1), (double)(z + 1), var11, var19);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var15, (double)(y + 1), (double)(z + 0), var18, var19);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var15, (double)(y + 0), (double)(z + 0), var18, var13);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var15, (double)(y + 0), (double)(z + 0), var18, var13);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var15, (double)(y + 1), (double)(z + 0), var18, var19);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var15, (double)(y + 1), (double)(z + 1), var11, var19);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var15, (double)(y + 0), (double)(z + 1), var11, var13);
    }

    if ((var17 & 4) != 0)
    {
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), (double)z + var15, var11, var13);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 1), (double)z + var15, var11, var19);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 1), (double)z + var15, var18, var19);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), (double)z + var15, var18, var13);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), (double)z + var15, var18, var13);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 1), (double)z + var15, var18, var19);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 1), (double)z + var15, var11, var19);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), (double)z + var15, var11, var13);
    }

    if ((var17 & 1) != 0)
    {
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 1), (double)(z + 1) - var15, var18, var19);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), (double)(z + 1) - var15, var18, var13);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), (double)(z + 1) - var15, var11, var13);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 1), (double)(z + 1) - var15, var11, var19);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 1), (double)(z + 1) - var15, var11, var19);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), (double)(z + 1) - var15, var11, var13);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), (double)(z + 1) - var15, var18, var13);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 1), (double)(z + 1) - var15, var18, var19);
    }

    if (block_normal_cube(m, x, y + 1, z))
    {
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 1) - var15, (double)(z + 0), var18, var19);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 1) - var15, (double)(z + 1), var18, var13);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 1) - var15, (double)(z + 1), var11, var13);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 1) - var15, (double)(z + 0), var11, var19);
    }

    return 1;
}

/* ------------------------------------------------------------ fence gates */

/** RenderBlocks.renderBlockFenceGate. */
int rb3_render_fence_gate(struct rb_mesher *m, int id, int x, int y, int z)
{
    int meta = rb_world_meta(m->w, x, y, z);
    int var7 = (meta & 4) != 0;              /* BlockFenceGate.isFenceGateOpen */
    int var8 = meta & 3;                     /* BlockDirectional.func_149895_l */
    float var9 = 0.375F;
    float var10 = 0.5625F;
    float var11 = 0.75F;
    float var12 = 0.9375F;
    float var13 = 0.3125F;
    float var14 = 1.0F;

    if (((var8 == 2 || var8 == 0) && rb_world_block(m->w, x - 1, y, z) == 139 && rb_world_block(m->w, x + 1, y, z) == 139)
        || ((var8 == 3 || var8 == 1) && rb_world_block(m->w, x, y, z - 1) == 139 && rb_world_block(m->w, x, y, z + 1) == 139))
    {
        var9 -= 0.1875F;
        var10 -= 0.1875F;
        var11 -= 0.1875F;
        var12 -= 0.1875F;
        var13 -= 0.1875F;
        var14 -= 0.1875F;
    }

    m->render_all_faces = 1;
    float var15;
    float var16;
    float var17;
    float var18;

    if (var8 != 3 && var8 != 1)
    {
        var15 = 0.0F;
        var16 = 0.125F;
        var17 = 0.4375F;
        var18 = 0.5625F;
        rb2_set_render_bounds(m, (double)var15, (double)var13, (double)var17, (double)var16, (double)var14, (double)var18);
        render_standard_block(m, id, x, y, z);
        var15 = 0.875F;
        var16 = 1.0F;
        rb2_set_render_bounds(m, (double)var15, (double)var13, (double)var17, (double)var16, (double)var14, (double)var18);
        render_standard_block(m, id, x, y, z);
    }
    else
    {
        m->uv_top = 1;
        var15 = 0.4375F;
        var16 = 0.5625F;
        var17 = 0.0F;
        var18 = 0.125F;
        rb2_set_render_bounds(m, (double)var15, (double)var13, (double)var17, (double)var16, (double)var14, (double)var18);
        render_standard_block(m, id, x, y, z);
        var17 = 0.875F;
        var18 = 1.0F;
        rb2_set_render_bounds(m, (double)var15, (double)var13, (double)var17, (double)var16, (double)var14, (double)var18);
        render_standard_block(m, id, x, y, z);
        m->uv_top = 0;
    }

    if (var7)
    {
        if (var8 == 2 || var8 == 0) m->uv_top = 1;

        if (var8 == 3)
        {
            rb2_set_render_bounds(m, 0.8125, (double)var9, 0.0, 0.9375, (double)var12, 0.125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.8125, (double)var9, 0.875, 0.9375, (double)var12, 1.0);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.5625, (double)var9, 0.0, 0.8125, (double)var10, 0.125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.5625, (double)var9, 0.875, 0.8125, (double)var10, 1.0);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.5625, (double)var11, 0.0, 0.8125, (double)var12, 0.125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.5625, (double)var11, 0.875, 0.8125, (double)var12, 1.0);
            render_standard_block(m, id, x, y, z);
        }
        else if (var8 == 1)
        {
            rb2_set_render_bounds(m, 0.0625, (double)var9, 0.0, 0.1875, (double)var12, 0.125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.0625, (double)var9, 0.875, 0.1875, (double)var12, 1.0);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.1875, (double)var9, 0.0, 0.4375, (double)var10, 0.125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.1875, (double)var9, 0.875, 0.4375, (double)var10, 1.0);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.1875, (double)var11, 0.0, 0.4375, (double)var12, 0.125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.1875, (double)var11, 0.875, 0.4375, (double)var12, 1.0);
            render_standard_block(m, id, x, y, z);
        }
        else if (var8 == 0)
        {
            rb2_set_render_bounds(m, 0.0, (double)var9, 0.8125, 0.125, (double)var12, 0.9375);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.875, (double)var9, 0.8125, 1.0, (double)var12, 0.9375);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.0, (double)var9, 0.5625, 0.125, (double)var10, 0.8125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.875, (double)var9, 0.5625, 1.0, (double)var10, 0.8125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.0, (double)var11, 0.5625, 0.125, (double)var12, 0.8125);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.875, (double)var11, 0.5625, 1.0, (double)var12, 0.8125);
            render_standard_block(m, id, x, y, z);
        }
        else if (var8 == 2)
        {
            rb2_set_render_bounds(m, 0.0, (double)var9, 0.0625, 0.125, (double)var12, 0.1875);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.875, (double)var9, 0.0625, 1.0, (double)var12, 0.1875);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.0, (double)var9, 0.1875, 0.125, (double)var10, 0.4375);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.875, (double)var9, 0.1875, 1.0, (double)var10, 0.4375);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.0, (double)var11, 0.1875, 0.125, (double)var12, 0.4375);
            render_standard_block(m, id, x, y, z);
            rb2_set_render_bounds(m, 0.875, (double)var11, 0.1875, 1.0, (double)var12, 0.4375);
            render_standard_block(m, id, x, y, z);
        }
    }
    else if (var8 != 3 && var8 != 1)
    {
        var15 = 0.375F;
        var16 = 0.5F;
        var17 = 0.4375F;
        var18 = 0.5625F;
        rb2_set_render_bounds(m, (double)var15, (double)var9, (double)var17, (double)var16, (double)var12, (double)var18);
        render_standard_block(m, id, x, y, z);
        var15 = 0.5F;
        var16 = 0.625F;
        rb2_set_render_bounds(m, (double)var15, (double)var9, (double)var17, (double)var16, (double)var12, (double)var18);
        render_standard_block(m, id, x, y, z);
        var15 = 0.625F;
        var16 = 0.875F;
        rb2_set_render_bounds(m, (double)var15, (double)var9, (double)var17, (double)var16, (double)var10, (double)var18);
        render_standard_block(m, id, x, y, z);
        rb2_set_render_bounds(m, (double)var15, (double)var11, (double)var17, (double)var16, (double)var12, (double)var18);
        render_standard_block(m, id, x, y, z);
        var15 = 0.125F;
        var16 = 0.375F;
        rb2_set_render_bounds(m, (double)var15, (double)var9, (double)var17, (double)var16, (double)var10, (double)var18);
        render_standard_block(m, id, x, y, z);
        rb2_set_render_bounds(m, (double)var15, (double)var11, (double)var17, (double)var16, (double)var12, (double)var18);
        render_standard_block(m, id, x, y, z);
    }
    else
    {
        m->uv_top = 1;
        var15 = 0.4375F;
        var16 = 0.5625F;
        var17 = 0.375F;
        var18 = 0.5F;
        rb2_set_render_bounds(m, (double)var15, (double)var9, (double)var17, (double)var16, (double)var12, (double)var18);
        render_standard_block(m, id, x, y, z);
        var17 = 0.5F;
        var18 = 0.625F;
        rb2_set_render_bounds(m, (double)var15, (double)var9, (double)var17, (double)var16, (double)var12, (double)var18);
        render_standard_block(m, id, x, y, z);
        var17 = 0.625F;
        var18 = 0.875F;
        rb2_set_render_bounds(m, (double)var15, (double)var9, (double)var17, (double)var16, (double)var10, (double)var18);
        render_standard_block(m, id, x, y, z);
        rb2_set_render_bounds(m, (double)var15, (double)var11, (double)var17, (double)var16, (double)var12, (double)var18);
        render_standard_block(m, id, x, y, z);
        var17 = 0.125F;
        var18 = 0.375F;
        rb2_set_render_bounds(m, (double)var15, (double)var9, (double)var17, (double)var16, (double)var10, (double)var18);
        render_standard_block(m, id, x, y, z);
        rb2_set_render_bounds(m, (double)var15, (double)var11, (double)var17, (double)var16, (double)var12, (double)var18);
        render_standard_block(m, id, x, y, z);
    }

    m->render_all_faces = 0;
    m->uv_top = 0;
    rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    return 1;
}

/* -------------------------------------------------------------- lily pads */

/** RenderBlocks.renderBlockLilyPad. */
int rb3_render_lily_pad(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var6 = icon_from_side(m, id, 1);

    if (m->override) var6 = m->override;

    float var7 = 0.015625F;
    double var8 = (double)var6->min_u;
    double var10 = (double)var6->min_v;
    double var12 = (double)var6->max_u;
    double var14 = (double)var6->max_v;
    long var16 = (long)(x * 3129871) ^ (long)z * 116129781L ^ (long)y;
    var16 = var16 * var16 * 42317861L + var16 * 11L;
    int var18 = (int)(var16 >> 16 & 3L);
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    float var19 = (float)x + 0.5F;
    float var20 = (float)z + 0.5F;
    float var21 = (float)(var18 & 1) * 0.5F * (float)(1 - var18 / 2 % 2 * 2);
    float var22 = (float)((var18 + 1) & 1) * 0.5F * (float)(1 - (var18 + 1) / 2 % 2 * 2);
    tess_set_color_opaque_i(m, 2129968);   /* BlockLilyPad.getBlockColor */
    tess_add_vertex_with_uv(t, (double)(var19 + var21 - var22), (double)((float)y + var7), (double)(var20 + var21 + var22), var8, var10);
    tess_add_vertex_with_uv(t, (double)(var19 + var21 + var22), (double)((float)y + var7), (double)(var20 - var21 + var22), var12, var10);
    tess_add_vertex_with_uv(t, (double)(var19 - var21 + var22), (double)((float)y + var7), (double)(var20 - var21 - var22), var12, var14);
    tess_add_vertex_with_uv(t, (double)(var19 - var21 - var22), (double)((float)y + var7), (double)(var20 + var21 - var22), var8, var14);
    tess_set_color_opaque_i(m, (2129968 & 16711422) >> 1);
    tess_add_vertex_with_uv(t, (double)(var19 - var21 - var22), (double)((float)y + var7), (double)(var20 + var21 - var22), var8, var14);
    tess_add_vertex_with_uv(t, (double)(var19 - var21 + var22), (double)((float)y + var7), (double)(var20 - var21 - var22), var12, var14);
    tess_add_vertex_with_uv(t, (double)(var19 + var21 + var22), (double)((float)y + var7), (double)(var20 - var21 + var22), var12, var10);
    tess_add_vertex_with_uv(t, (double)(var19 + var21 - var22), (double)((float)y + var7), (double)(var20 + var21 + var22), var8, var10);
    return 1;
}

/* -------------------------------------------------------------- cauldrons */

/** RenderBlocks.renderBlockCauldron. */
int rb3_render_cauldron(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    render_standard_block(m, id, x, y, z);
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int var6 = color_multiplier(m, id, x, y, z);
    float var7 = (float)(var6 >> 16 & 255) / 255.0F;
    float var8 = (float)(var6 >> 8 & 255) / 255.0F;
    float var9 = (float)(var6 & 255) / 255.0F;
    tess_set_color_opaque_f(t, var7, var8, var9);
    const struct rb_uv *var15 = icon_from_side(m, id, 2);   /* getBlockTextureFromSide(2) */
    float var11 = 0.125F;
    render_face_x_pos(m, (double)((float)x - 1.0F + var11), (double)y, (double)z, var15);
    render_face_x_neg(m, (double)((float)x + 1.0F - var11), (double)y, (double)z, var15);
    render_face_z_pos(m, (double)x, (double)y, (double)((float)z - 1.0F + var11), var15);
    render_face_z_neg(m, (double)x, (double)y, (double)((float)z + 1.0F - var11), var15);
    const struct rb_uv *var16 = &m->special3[RB3_CAULDRON_INNER];
    render_face_y_pos(m, (double)x, (double)((float)y - 1.0F + 0.25F), (double)z, var16);
    render_face_y_neg(m, (double)x, (double)((float)y + 1.0F - 0.75F), (double)z, var16);
    int var13 = rb_world_meta(m->w, x, y, z);

    if (var13 > 0)
    {
        const struct rb_uv *var14 = &m->special3[RB3_WATER_STILL];
        float h = (float)(6 + 3 * var13) / 16.0F;   /* BlockCauldron.func_150025_c */
        render_face_y_pos(m, (double)x, (double)((float)y - 1.0F + h), (double)z, var14);
    }

    return 1;
}

/* --------------------------------------------------------- brewing stands */

/* rb_trig_brewing: the three arms' angles (renderBlockBrewingStand's var17) */
int rb_trig_brewing(struct rb_trig out[6])
{
    for (int var16 = 0; var16 < 3; ++var16)
    {
        double var17 = (double)var16 * 3.141592653589793 * 2.0 / 3.0 + 1.5707963267948966;
        out[2 * var16] = (struct rb_trig){.fn = RB_TRIG_SIN, .a = var17, .v = sin(var17)};
        out[2 * var16 + 1] = (struct rb_trig){.fn = RB_TRIG_COS, .a = var17, .v = cos(var17)};
    }
    return 6;
}

/** RenderBlocks.renderBlockBrewingStand. */
int rb3_render_brewing_stand(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    rb2_set_render_bounds(m, 0.4375, 0.0, 0.4375, 0.5625, 0.875, 0.5625);
    render_standard_block(m, id, x, y, z);
    m->override = &m->special3[RB3_BREWING_BASE];
    m->render_all_faces = 1;
    rb2_set_render_bounds(m, 0.5625, 0.0, 0.3125, 0.9375, 0.125, 0.6875);
    render_standard_block(m, id, x, y, z);
    rb2_set_render_bounds(m, 0.125, 0.0, 0.0625, 0.5, 0.125, 0.4375);
    render_standard_block(m, id, x, y, z);
    rb2_set_render_bounds(m, 0.125, 0.0, 0.5625, 0.5, 0.125, 0.9375);
    render_standard_block(m, id, x, y, z);
    m->render_all_faces = 0;
    m->override = NULL;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int var6 = color_multiplier(m, id, x, y, z);
    float var7 = (float)(var6 >> 16 & 255) / 255.0F;
    float var8 = (float)(var6 >> 8 & 255) / 255.0F;
    float var9 = (float)(var6 & 255) / 255.0F;
    tess_set_color_opaque_f(t, var7, var8, var9);
    const struct rb_uv *var31 = icon_from_side_meta(m, id, 0, 0);

    if (m->override) var31 = m->override;

    double var32 = (double)var31->min_v;
    double var13 = (double)var31->max_v;
    int var15 = rb_world_meta(m->w, x, y, z);

    for (int var16 = 0; var16 < 3; ++var16)
    {
        double var17 = (double)var16 * 3.141592653589793 * 2.0 / 3.0 + 1.5707963267948966;
        double var19 = (double)interp_u(var31, 8.0);
        double var21 = (double)var31->max_u;

        if ((var15 & 1 << var16) != 0) var21 = (double)var31->min_u;

        double var23 = (double)x + 0.5;
        double var25 = (double)x + 0.5 + rb_sin(m, var17) * 8.0 / 16.0;
        double var27 = (double)z + 0.5;
        double var29 = (double)z + 0.5 + rb_cos(m, var17) * 8.0 / 16.0;
        tess_add_vertex_with_uv(t, var23, (double)(y + 1), var27, var19, var32);
        tess_add_vertex_with_uv(t, var23, (double)(y + 0), var27, var19, var13);
        tess_add_vertex_with_uv(t, var25, (double)(y + 0), var29, var21, var13);
        tess_add_vertex_with_uv(t, var25, (double)(y + 1), var29, var21, var32);
        tess_add_vertex_with_uv(t, var25, (double)(y + 1), var29, var21, var32);
        tess_add_vertex_with_uv(t, var25, (double)(y + 0), var29, var21, var13);
        tess_add_vertex_with_uv(t, var23, (double)(y + 0), var27, var19, var13);
        tess_add_vertex_with_uv(t, var23, (double)(y + 1), var27, var19, var32);
    }

    return 1;
}

/* ------------------------------------------------------ end portal frames */

/** RenderBlocks.renderBlockEndPortalFrame. */
int rb3_render_end_portal_frame(struct rb_mesher *m, int id, int x, int y, int z)
{
    int var5 = rb_world_meta(m->w, x, y, z);
    int var6 = var5 & 3;

    if (var6 == 0) m->uv_top = 3;
    else if (var6 == 3) m->uv_top = 1;
    else if (var6 == 1) m->uv_top = 2;

    if ((var5 & 4) == 0)   /* BlockEndPortalFrame.func_150020_b */
    {
        rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 0.8125, 1.0);
        render_standard_block(m, id, x, y, z);
        m->uv_top = 0;
        return 1;
    }

    m->render_all_faces = 1;
    rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 0.8125, 1.0);
    render_standard_block(m, id, x, y, z);
    m->override = &m->special3[RB3_ENDFRAME_EYE];
    rb2_set_render_bounds(m, 0.25, 0.8125, 0.25, 0.75, 1.0, 0.75);
    render_standard_block(m, id, x, y, z);
    m->render_all_faces = 0;
    m->override = NULL;
    m->uv_top = 0;
    return 1;
}

/* ------------------------------------------------------------ dragon eggs */

/** RenderBlocks.renderBlockDragonEgg. */
int rb3_render_dragon_egg(struct rb_mesher *m, int id, int x, int y, int z)
{
    int var6 = 0;

    for (int var7 = 0; var7 < 8; ++var7)
    {
        int var8 = 0;
        int var9 = 1;

        if (var7 == 0) var8 = 2;
        if (var7 == 1) var8 = 3;
        if (var7 == 2) var8 = 4;
        if (var7 == 3) { var8 = 5; var9 = 2; }
        if (var7 == 4) { var8 = 6; var9 = 3; }
        if (var7 == 5) { var8 = 7; var9 = 5; }
        if (var7 == 6) { var8 = 6; var9 = 2; }
        if (var7 == 7) var8 = 3;

        float var10 = (float)var8 / 16.0F;
        float var11 = 1.0F - (float)var6 / 16.0F;
        float var12 = 1.0F - (float)(var6 + var9) / 16.0F;
        var6 += var9;
        rb2_set_render_bounds(m, (double)(0.5F - var10), (double)var12, (double)(0.5F - var10),
                              (double)(0.5F + var10), (double)var11, (double)(0.5F + var10));
        render_standard_block(m, id, x, y, z);
    }

    rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    return 1;
}

/* ----------------------------------------------------------------- cocoa */

/** RenderBlocks.renderBlockCocoa. */
int rb3_render_cocoa(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    int var6 = rb_world_meta(m->w, x, y, z);
    int var7 = var6 & 3;                     /* BlockDirectional.func_149895_l */
    int var8 = (var6 >> 2) & 3;              /* BlockCocoa.func_149987_c */
    /* looked up each time: a cache in a function static is one for every
     * thread of the device mesher, which read it half filled */
    static const char *const COCOA[3] = {"cocoa_stage_0", "cocoa_stage_1", "cocoa_stage_2"};

    int age = var8;

    if (age < 0 || age >= 3) age = 2;

    const struct rb_uv *var9 = rb_atlas_by_name(m, COCOA[age]);
    int var10 = 4 + var8 * 2;
    int var11 = 5 + var8 * 2;
    double var12 = 15.0 - (double)var10;
    double var14 = 15.0;
    double var16 = 4.0;
    double var18 = 4.0 + (double)var11;
    double var20 = (double)interp_u(var9, var12);
    double var22 = (double)interp_u(var9, var14);
    double var24 = (double)interp_v(var9, var16);
    double var26 = (double)interp_v(var9, var18);
    double var28 = 0.0;
    double var30 = 0.0;

    switch (var7)
    {
        case 0: var28 = 8.0 - (double)(var10 / 2); var30 = 15.0 - (double)var10; break;
        case 1: var28 = 1.0; var30 = 8.0 - (double)(var10 / 2); break;
        case 2: var28 = 8.0 - (double)(var10 / 2); var30 = 1.0; break;
        case 3: var28 = 15.0 - (double)var10; var30 = 8.0 - (double)(var10 / 2); break;
    }

    double var32 = (double)x + var28 / 16.0;
    double var34 = (double)x + (var28 + (double)var10) / 16.0;
    double var36 = (double)y + (12.0 - (double)var11) / 16.0;
    double var38 = (double)y + 0.75;
    double var40 = (double)z + var30 / 16.0;
    double var42 = (double)z + (var30 + (double)var10) / 16.0;
    tess_add_vertex_with_uv(t, var32, var36, var40, var20, var26);
    tess_add_vertex_with_uv(t, var32, var36, var42, var22, var26);
    tess_add_vertex_with_uv(t, var32, var38, var42, var22, var24);
    tess_add_vertex_with_uv(t, var32, var38, var40, var20, var24);
    tess_add_vertex_with_uv(t, var34, var36, var42, var20, var26);
    tess_add_vertex_with_uv(t, var34, var36, var40, var22, var26);
    tess_add_vertex_with_uv(t, var34, var38, var40, var22, var24);
    tess_add_vertex_with_uv(t, var34, var38, var42, var20, var24);
    tess_add_vertex_with_uv(t, var34, var36, var40, var20, var26);
    tess_add_vertex_with_uv(t, var32, var36, var40, var22, var26);
    tess_add_vertex_with_uv(t, var32, var38, var40, var22, var24);
    tess_add_vertex_with_uv(t, var34, var38, var40, var20, var24);
    tess_add_vertex_with_uv(t, var32, var36, var42, var20, var26);
    tess_add_vertex_with_uv(t, var34, var36, var42, var22, var26);
    tess_add_vertex_with_uv(t, var34, var38, var42, var22, var24);
    tess_add_vertex_with_uv(t, var32, var38, var42, var20, var24);
    int var44 = var10;

    if (var8 >= 2) var44 = var10 - 1;

    var20 = (double)var9->min_u;
    var22 = (double)interp_u(var9, (double)var44);
    var24 = (double)var9->min_v;
    var26 = (double)interp_v(var9, (double)var44);
    tess_add_vertex_with_uv(t, var32, var38, var42, var20, var26);
    tess_add_vertex_with_uv(t, var34, var38, var42, var22, var26);
    tess_add_vertex_with_uv(t, var34, var38, var40, var22, var24);
    tess_add_vertex_with_uv(t, var32, var38, var40, var20, var24);
    tess_add_vertex_with_uv(t, var32, var36, var40, var20, var24);
    tess_add_vertex_with_uv(t, var34, var36, var40, var22, var24);
    tess_add_vertex_with_uv(t, var34, var36, var42, var22, var26);
    tess_add_vertex_with_uv(t, var32, var36, var42, var20, var26);
    var20 = (double)interp_u(var9, 12.0);
    var22 = (double)var9->max_u;
    var24 = (double)var9->min_v;
    var26 = (double)interp_v(var9, 4.0);
    var28 = 8.0;
    var30 = 0.0;

    switch (var7)
    {
        case 0:
            var28 = 8.0;
            var30 = 12.0;
            var32 = var20;
            var20 = var22;
            var22 = var32;
            break;

        case 1: var28 = 0.0; var30 = 8.0; break;
        case 2: var28 = 8.0; var30 = 0.0; break;
        case 3:
            var28 = 12.0;
            var30 = 8.0;
            var32 = var20;
            var20 = var22;
            var22 = var32;
            break;
    }

    var32 = (double)x + var28 / 16.0;
    var34 = (double)x + (var28 + 4.0) / 16.0;
    var36 = (double)y + 0.75;
    var38 = (double)y + 1.0;
    var40 = (double)z + var30 / 16.0;
    var42 = (double)z + (var30 + 4.0) / 16.0;

    if (var7 != 2 && var7 != 0)
    {
        if (var7 == 1 || var7 == 3)
        {
            tess_add_vertex_with_uv(t, var34, var36, var40, var20, var26);
            tess_add_vertex_with_uv(t, var32, var36, var40, var22, var26);
            tess_add_vertex_with_uv(t, var32, var38, var40, var22, var24);
            tess_add_vertex_with_uv(t, var34, var38, var40, var20, var24);
            tess_add_vertex_with_uv(t, var32, var36, var40, var22, var26);
            tess_add_vertex_with_uv(t, var34, var36, var40, var20, var26);
            tess_add_vertex_with_uv(t, var34, var38, var40, var20, var24);
            tess_add_vertex_with_uv(t, var32, var38, var40, var22, var24);
        }
    }
    else
    {
        tess_add_vertex_with_uv(t, var32, var36, var40, var22, var26);
        tess_add_vertex_with_uv(t, var32, var36, var42, var20, var26);
        tess_add_vertex_with_uv(t, var32, var38, var42, var20, var24);
        tess_add_vertex_with_uv(t, var32, var38, var40, var22, var24);
        tess_add_vertex_with_uv(t, var32, var36, var42, var20, var26);
        tess_add_vertex_with_uv(t, var32, var36, var40, var22, var26);
        tess_add_vertex_with_uv(t, var32, var38, var40, var22, var24);
        tess_add_vertex_with_uv(t, var32, var38, var42, var20, var24);
    }

    return 1;
}

/* -------------------------------------------------------------- tripwire */

/** BlockTripWire.func_150139_a: does the arm to `side` connect. */
static int tripwire_side(struct rb_mesher *m, int x, int y, int z, int meta, int side)
{
    int nx = x + DIR_OX[side];
    int nz = z + DIR_OZ[side];
    int nid = rb_world_block(m->w, nx, y, nz);
    int bit2 = (meta & 2) == 2;

    if (nid == 131) return (rb_world_meta(m->w, nx, y, nz) & 3) == ROT_OPPOSITE[side];
    if (nid == 132) return ((rb_world_meta(m->w, nx, y, nz) & 2) == 2) == bit2;
    return 0;
}

/** RenderBlocks.renderBlockTripWire. */
int rb3_render_tripwire(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var6 = icon_from_side(m, id, 0);
    int var7 = rb_world_meta(m->w, x, y, z);
    int var8 = (var7 & 4) == 4;
    int var9 = (var7 & 2) == 2;

    if (m->override) var6 = m->override;

    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    double var10 = (double)var6->min_u;
    double var12 = (double)interp_v(var6, var8 ? 2.0 : 0.0);
    double var14 = (double)var6->max_u;
    double var16 = (double)interp_v(var6, var8 ? 4.0 : 2.0);
    double var18 = (double)(var9 ? 3.5F : 1.5F) / 16.0;
    int var20 = tripwire_side(m, x, y, z, var7, 1);
    int var21 = tripwire_side(m, x, y, z, var7, 3);
    int var22 = tripwire_side(m, x, y, z, var7, 2);
    int var23 = tripwire_side(m, x, y, z, var7, 0);
    float var24 = 0.03125F;
    float var25 = 0.5F - var24 / 2.0F;
    float var26 = var25 + var24;

    if (!var22 && !var21 && !var23 && !var20)
    {
        var22 = 1;
        var23 = 1;
    }

    if (var22)
    {
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.25, var10, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.25, var10, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z, var14, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z, var14, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z, var14, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z, var14, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.25, var10, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.25, var10, var12);
    }

    if (var22 || (var23 && !var21 && !var20))
    {
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.5, var10, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.5, var10, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.25, var14, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.25, var14, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.25, var14, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.25, var14, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.5, var10, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.5, var10, var12);
    }

    if (var23 || (var22 && !var21 && !var20))
    {
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.75, var10, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.75, var10, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.5, var14, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.5, var14, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.5, var14, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.5, var14, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.75, var10, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.75, var10, var12);
    }

    if (var23)
    {
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)(z + 1), var10, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)(z + 1), var10, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.75, var14, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.75, var14, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)z + 0.75, var14, var12);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)z + 0.75, var14, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var26), (double)y + var18, (double)(z + 1), var10, var16);
        tess_add_vertex_with_uv(t, (double)((float)x + var25), (double)y + var18, (double)(z + 1), var10, var12);
    }

    if (var20)
    {
        tess_add_vertex_with_uv(t, (double)x, (double)y + var18, (double)((float)z + var26), var10, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var18, (double)((float)z + var26), var14, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var18, (double)((float)z + var25), var14, var12);
        tess_add_vertex_with_uv(t, (double)x, (double)y + var18, (double)((float)z + var25), var10, var12);
        tess_add_vertex_with_uv(t, (double)x, (double)y + var18, (double)((float)z + var25), var10, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var18, (double)((float)z + var25), var14, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var18, (double)((float)z + var26), var14, var16);
        tess_add_vertex_with_uv(t, (double)x, (double)y + var18, (double)((float)z + var26), var10, var16);
    }

    if (var20 || (var21 && !var22 && !var23))
    {
        tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var18, (double)((float)z + var26), var10, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.5, (double)y + var18, (double)((float)z + var26), var14, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.5, (double)y + var18, (double)((float)z + var25), var14, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var18, (double)((float)z + var25), var10, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var18, (double)((float)z + var25), var10, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.5, (double)y + var18, (double)((float)z + var25), var14, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.5, (double)y + var18, (double)((float)z + var26), var14, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var18, (double)((float)z + var26), var10, var16);
    }

    if (var21 || (var20 && !var22 && !var23))
    {
        tess_add_vertex_with_uv(t, (double)x + 0.5, (double)y + var18, (double)((float)z + var26), var10, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var18, (double)((float)z + var26), var14, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var18, (double)((float)z + var25), var14, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.5, (double)y + var18, (double)((float)z + var25), var10, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.5, (double)y + var18, (double)((float)z + var25), var10, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var18, (double)((float)z + var25), var14, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var18, (double)((float)z + var26), var14, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.5, (double)y + var18, (double)((float)z + var26), var10, var16);
    }

    if (var21)
    {
        tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var18, (double)((float)z + var26), var10, var16);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + var18, (double)((float)z + var26), var14, var16);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + var18, (double)((float)z + var25), var14, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var18, (double)((float)z + var25), var10, var12);
        tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var18, (double)((float)z + var25), var10, var12);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + var18, (double)((float)z + var25), var14, var12);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + var18, (double)((float)z + var26), var14, var16);
        tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var18, (double)((float)z + var26), var10, var16);
    }

    return 1;
}

/** RenderBlocks.renderBlockTripWireSource. */
int rb3_render_tripwire_source(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int var6 = rb_world_meta(m->w, x, y, z);
    int var7 = var6 & 3;
    int var8 = (var6 & 4) == 4;
    int var9 = (var6 & 8) == 8;
    int var10 = !rb_solid_top_surface(m, x, y - 1, z);

    int had_override = m->override != NULL;

    if (!had_override) m->override = icon_from_side(m, 5, 1);   /* getBlockIcon(Blocks.planks) */

    float var12 = 0.25F;
    float var13 = 0.125F;
    float var14 = 0.125F;
    float var15 = 0.3F - var12;
    float var16 = 0.3F + var12;

    if (var7 == 2)      rb2_set_render_bounds(m, (double)(0.5F - var13), (double)var15, (double)(1.0F - var14), (double)(0.5F + var13), (double)var16, 1.0);
    else if (var7 == 0) rb2_set_render_bounds(m, (double)(0.5F - var13), (double)var15, 0.0, (double)(0.5F + var13), (double)var16, (double)var14);
    else if (var7 == 1) rb2_set_render_bounds(m, (double)(1.0F - var14), (double)var15, (double)(0.5F - var13), 1.0, (double)var16, (double)(0.5F + var13));
    else if (var7 == 3) rb2_set_render_bounds(m, 0.0, (double)var15, (double)(0.5F - var13), (double)var14, (double)var16, (double)(0.5F + var13));

    render_standard_block(m, id, x, y, z);

    if (!had_override) m->override = NULL;

    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    const struct rb_uv *var17 = icon_from_side(m, id, 0);

    if (m->override) var17 = m->override;

    double v[8][3];
    float var27 = 0.046875F;
    float var28 = 0.046875F;
    float var29 = 0.3125F;
    v[0][0] = (double)(-var27); v[0][1] = 0.0; v[0][2] = (double)(-var28);
    v[1][0] = (double)var27;    v[1][1] = 0.0; v[1][2] = (double)(-var28);
    v[2][0] = (double)var27;    v[2][1] = 0.0; v[2][2] = (double)var28;
    v[3][0] = (double)(-var27); v[3][1] = 0.0; v[3][2] = (double)var28;
    v[4][0] = (double)(-var27); v[4][1] = (double)var29; v[4][2] = (double)(-var28);
    v[5][0] = (double)var27;    v[5][1] = (double)var29; v[5][2] = (double)(-var28);
    v[6][0] = (double)var27;    v[6][1] = (double)var29; v[6][2] = (double)var28;
    v[7][0] = (double)(-var27); v[7][1] = (double)var29; v[7][2] = (double)var28;

    for (int i = 0; i < 8; ++i)
    {
        v[i][2] += 0.0625;

        if (var9)      { vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], 0.5235988F); v[i][1] -= 0.4375; }
        else if (var8) { vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], 0.08726647F); v[i][1] -= 0.4375; }
        else           { vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], -((float)3.14159265358979323846 * 2.0F / 9.0F)); v[i][1] -= 0.375; }

        vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], (float)3.14159265358979323846 / 2.0F);

        if (var7 == 2) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], 0.0F);
        if (var7 == 0) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], (float)3.14159265358979323846);
        if (var7 == 1) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], (float)3.14159265358979323846 / 2.0F);
        if (var7 == 3) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], -(float)3.14159265358979323846 / 2.0F);

        v[i][0] += (double)x + 0.5;
        v[i][1] += (double)((float)y + 0.3125F);
        v[i][2] += (double)z + 0.5;
    }

    double var18 = (double)var17->min_u;
    double var20 = (double)var17->min_v;
    double var22 = (double)var17->max_u;
    double var24 = (double)var17->max_v;

    static const int Q[6][4] = {
        {0, 1, 2, 3}, {7, 6, 5, 4}, {1, 0, 4, 5},
        {2, 1, 5, 6}, {3, 2, 6, 7}, {0, 3, 7, 4}
    };

    for (int q = 0; q < 6; ++q)
    {
        if (q == 0)
        {
            var18 = (double)interp_u(var17, 7.0);
            var20 = (double)interp_v(var17, 9.0);
            var22 = (double)interp_u(var17, 9.0);
            var24 = (double)interp_v(var17, 11.0);
        }
        else if (q == 2)
        {
            var18 = (double)interp_u(var17, 7.0);
            var20 = (double)interp_v(var17, 9.0);
            var22 = (double)interp_u(var17, 9.0);
            var24 = (double)interp_v(var17, 16.0);
        }

        tess_add_vertex_with_uv(t, v[Q[q][0]][0], v[Q[q][0]][1], v[Q[q][0]][2], var18, var24);
        tess_add_vertex_with_uv(t, v[Q[q][1]][0], v[Q[q][1]][1], v[Q[q][1]][2], var22, var24);
        tess_add_vertex_with_uv(t, v[Q[q][2]][0], v[Q[q][2]][1], v[Q[q][2]][2], var22, var20);
        tess_add_vertex_with_uv(t, v[Q[q][3]][0], v[Q[q][3]][1], v[Q[q][3]][2], var18, var20);
    }

    float var61 = 0.09375F;
    float var39 = 0.09375F;
    float var40 = 0.03125F;
    v[0][0] = (double)(-var61); v[0][1] = 0.0; v[0][2] = (double)(-var39);
    v[1][0] = (double)var61;    v[1][1] = 0.0; v[1][2] = (double)(-var39);
    v[2][0] = (double)var61;    v[2][1] = 0.0; v[2][2] = (double)var39;
    v[3][0] = (double)(-var61); v[3][1] = 0.0; v[3][2] = (double)var39;
    v[4][0] = (double)(-var61); v[4][1] = (double)var40; v[4][2] = (double)(-var39);
    v[5][0] = (double)var61;    v[5][1] = (double)var40; v[5][2] = (double)(-var39);
    v[6][0] = (double)var61;    v[6][1] = (double)var40; v[6][2] = (double)var39;
    v[7][0] = (double)(-var61); v[7][1] = (double)var40; v[7][2] = (double)var39;

    for (int i = 0; i < 8; ++i)
    {
        v[i][2] += 0.21875;

        if (var9)      { v[i][1] -= 0.09375; v[i][2] -= 0.1625; vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], 0.0F); }
        else if (var8) { v[i][1] += 0.015625; v[i][2] -= 0.171875; vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], 0.17453294F); }
        else           { vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], 0.87266463F); }

        if (var7 == 2) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], 0.0F);
        if (var7 == 0) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], (float)3.14159265358979323846);
        if (var7 == 1) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], (float)3.14159265358979323846 / 2.0F);
        if (var7 == 3) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], -(float)3.14159265358979323846 / 2.0F);

        v[i][0] += (double)x + 0.5;
        v[i][1] += (double)((float)y + 0.3125F);
        v[i][2] += (double)z + 0.5;
    }

    for (int q = 0; q < 6; ++q)
    {
        if (q == 0)
        {
            var18 = (double)interp_u(var17, 5.0);
            var20 = (double)interp_v(var17, 3.0);
            var22 = (double)interp_u(var17, 11.0);
            var24 = (double)interp_v(var17, 9.0);
        }
        else if (q == 2)
        {
            var18 = (double)interp_u(var17, 5.0);
            var20 = (double)interp_v(var17, 3.0);
            var22 = (double)interp_u(var17, 11.0);
            var24 = (double)interp_v(var17, 5.0);
        }

        tess_add_vertex_with_uv(t, v[Q[q][0]][0], v[Q[q][0]][1], v[Q[q][0]][2], var18, var24);
        tess_add_vertex_with_uv(t, v[Q[q][1]][0], v[Q[q][1]][1], v[Q[q][1]][2], var22, var24);
        tess_add_vertex_with_uv(t, v[Q[q][2]][0], v[Q[q][2]][1], v[Q[q][2]][2], var22, var20);
        tess_add_vertex_with_uv(t, v[Q[q][3]][0], v[Q[q][3]][1], v[Q[q][3]][2], var18, var20);
    }

    if (var8)
    {
        double var63 = v[0][1];
        float var47 = 0.03125F;
        float var48 = 0.5F - var47 / 2.0F;
        float var49 = var48 + var47;
        double var50 = (double)var17->min_u;
        double var52 = (double)interp_v(var17, 2.0);
        double var54 = (double)var17->max_u;
        double var56 = (double)interp_v(var17, 4.0);
        double var58 = (double)(var10 ? 3.5F : 1.5F) / 16.0;
        tess_set_color_opaque_f(t, 0.75F, 0.75F, 0.75F);

        if (var7 == 2)
        {
            tess_add_vertex_with_uv(t, (double)((float)x + var48), (double)y + var58, (double)z + 0.25, var50, var52);
            tess_add_vertex_with_uv(t, (double)((float)x + var49), (double)y + var58, (double)z + 0.25, var50, var56);
            tess_add_vertex_with_uv(t, (double)((float)x + var49), (double)y + var58, (double)z, var54, var56);
            tess_add_vertex_with_uv(t, (double)((float)x + var48), (double)y + var58, (double)z, var54, var52);
            tess_add_vertex_with_uv(t, (double)((float)x + var48), var63, (double)z + 0.5, var50, var52);
            tess_add_vertex_with_uv(t, (double)((float)x + var49), var63, (double)z + 0.5, var50, var56);
            tess_add_vertex_with_uv(t, (double)((float)x + var49), (double)y + var58, (double)z + 0.25, var54, var56);
            tess_add_vertex_with_uv(t, (double)((float)x + var48), (double)y + var58, (double)z + 0.25, var54, var52);
        }
        else if (var7 == 0)
        {
            tess_add_vertex_with_uv(t, (double)((float)x + var48), (double)y + var58, (double)z + 0.75, var50, var52);
            tess_add_vertex_with_uv(t, (double)((float)x + var49), (double)y + var58, (double)z + 0.75, var50, var56);
            tess_add_vertex_with_uv(t, (double)((float)x + var49), var63, (double)z + 0.5, var54, var56);
            tess_add_vertex_with_uv(t, (double)((float)x + var48), var63, (double)z + 0.5, var54, var52);
            tess_add_vertex_with_uv(t, (double)((float)x + var48), (double)y + var58, (double)(z + 1), var50, var52);
            tess_add_vertex_with_uv(t, (double)((float)x + var49), (double)y + var58, (double)(z + 1), var50, var56);
            tess_add_vertex_with_uv(t, (double)((float)x + var49), (double)y + var58, (double)z + 0.75, var54, var56);
            tess_add_vertex_with_uv(t, (double)((float)x + var48), (double)y + var58, (double)z + 0.75, var54, var52);
        }
        else if (var7 == 1)
        {
            tess_add_vertex_with_uv(t, (double)x, (double)y + var58, (double)((float)z + var49), var50, var56);
            tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var58, (double)((float)z + var49), var54, var56);
            tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var58, (double)((float)z + var48), var54, var52);
            tess_add_vertex_with_uv(t, (double)x, (double)y + var58, (double)((float)z + var48), var50, var52);
            tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var58, (double)((float)z + var49), var50, var56);
            tess_add_vertex_with_uv(t, (double)x + 0.5, var63, (double)((float)z + var49), var54, var56);
            tess_add_vertex_with_uv(t, (double)x + 0.5, var63, (double)((float)z + var48), var54, var52);
            tess_add_vertex_with_uv(t, (double)x + 0.25, (double)y + var58, (double)((float)z + var48), var50, var52);
        }
        else
        {
            tess_add_vertex_with_uv(t, (double)x + 0.5, var63, (double)((float)z + var49), var50, var56);
            tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var58, (double)((float)z + var49), var54, var56);
            tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var58, (double)((float)z + var48), var54, var52);
            tess_add_vertex_with_uv(t, (double)x + 0.5, var63, (double)((float)z + var48), var50, var52);
            tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var58, (double)((float)z + var49), var50, var56);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + var58, (double)((float)z + var49), var54, var56);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + var58, (double)((float)z + var48), var54, var52);
            tess_add_vertex_with_uv(t, (double)x + 0.75, (double)y + var58, (double)((float)z + var48), var50, var52);
        }
    }

    return 1;
}

/* ------------------------------------------------------------------ walls */

/** RenderBlocks.renderBlockWall. */
int rb3_render_wall(struct rb_mesher *m, int id, int x, int y, int z)
{
    int var5 = fence_connects(m, id, x - 1, y, z);
    int var6 = fence_connects(m, id, x + 1, y, z);
    int var7 = fence_connects(m, id, x, y, z - 1);
    int var8 = fence_connects(m, id, x, y, z + 1);
    int var9 = var7 && var8 && !var5 && !var6;
    int var10 = !var7 && !var8 && var5 && var6;
    int var11 = is_air(m, x, y + 1, z);

    if ((var9 || var10) && var11)
    {
        if (var9)
        {
            rb2_set_render_bounds(m, 0.3125, 0.0, 0.0, 0.6875, 0.8125, 1.0);
            render_standard_block(m, id, x, y, z);
        }
        else
        {
            rb2_set_render_bounds(m, 0.0, 0.0, 0.3125, 1.0, 0.8125, 0.6875);
            render_standard_block(m, id, x, y, z);
        }
    }
    else
    {
        rb2_set_render_bounds(m, 0.25, 0.0, 0.25, 0.75, 1.0, 0.75);
        render_standard_block(m, id, x, y, z);

        if (var5)
        {
            rb2_set_render_bounds(m, 0.0, 0.0, 0.3125, 0.25, 0.8125, 0.6875);
            render_standard_block(m, id, x, y, z);
        }

        if (var6)
        {
            rb2_set_render_bounds(m, 0.75, 0.0, 0.3125, 1.0, 0.8125, 0.6875);
            render_standard_block(m, id, x, y, z);
        }

        if (var7)
        {
            rb2_set_render_bounds(m, 0.3125, 0.0, 0.0, 0.6875, 0.8125, 0.25);
            render_standard_block(m, id, x, y, z);
        }

        if (var8)
        {
            rb2_set_render_bounds(m, 0.3125, 0.0, 0.75, 0.6875, 0.8125, 1.0);
            render_standard_block(m, id, x, y, z);
        }
    }

    /* setBlockBoundsBasedOnState: a no-op for the wall */
    return 1;
}

/* ------------------------------------------------------------ flower pots */

/** RenderBlocks.renderBlockFlowerpot. */
int rb3_render_flowerpot(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    render_standard_block(m, id, x, y, z);
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int var6 = color_multiplier(m, id, x, y, z);
    const struct rb_uv *var7 = icon_from_side(m, id, 0);
    float var8 = (float)(var6 >> 16 & 255) / 255.0F;
    float var9 = (float)(var6 >> 8 & 255) / 255.0F;
    float var10 = (float)(var6 & 255) / 255.0F;
    tess_set_color_opaque_f(t, var8, var9, var10);
    float var11 = 0.1865F;
    render_face_x_pos(m, (double)((float)x - 0.5F + var11), (double)y, (double)z, var7);
    render_face_x_neg(m, (double)((float)x + 0.5F - var11), (double)y, (double)z, var7);
    render_face_z_pos(m, (double)x, (double)y, (double)((float)z - 0.5F + var11), var7);
    render_face_z_neg(m, (double)x, (double)y, (double)((float)z + 0.5F - var11), var7);
    render_face_y_pos(m, (double)x, (double)((float)y - 0.5F + var11 + 0.1875F), (double)z, icon_from_side(m, 3, 1));

    int plant, data;
    int has_te = rb_world_tile_entity(m->w, x, y, z, &plant, &data);

    if (has_te && BLOCKS[plant].exists)
    {
        int var16 = BLOCKS[plant].render_type;
        t->yoff += 0.25;   /* addTranslation(0, 4/16, 0) */
        var6 = color_multiplier(m, plant, x, y, z);

        if (var6 != 16777215)
        {
            var8 = (float)(var6 >> 16 & 255) / 255.0F;
            var9 = (float)(var6 >> 8 & 255) / 255.0F;
            var10 = (float)(var6 & 255) / 255.0F;
            tess_set_color_opaque_f(t, var8, var9, var10);
        }

        if (var16 == 1)
        {
            draw_crossed_squares(m, icon_from_side_meta(m, plant, 0, data), (double)x, (double)y, (double)z, 0.75F);
        }
        else if (var16 == 13)
        {
            m->render_all_faces = 1;
            float var20 = 0.125F;
            rb2_set_render_bounds(m, (double)(0.5F - var20), 0.0, (double)(0.5F - var20), (double)(0.5F + var20), 0.25, (double)(0.5F + var20));
            render_standard_block(m, plant, x, y, z);
            rb2_set_render_bounds(m, (double)(0.5F - var20), 0.25, (double)(0.5F - var20), (double)(0.5F + var20), 0.5, (double)(0.5F + var20));
            render_standard_block(m, plant, x, y, z);
            rb2_set_render_bounds(m, (double)(0.5F - var20), 0.5, (double)(0.5F - var20), (double)(0.5F + var20), 0.75, (double)(0.5F + var20));
            render_standard_block(m, plant, x, y, z);
            m->render_all_faces = 0;
            rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        }

        t->yoff -= 0.25;
    }

    return 1;
}

/* ---------------------------------------------------------------- beacons */

/** RenderBlocks.renderBlockBeacon. */
int rb3_render_beacon(struct rb_mesher *m, int id, int x, int y, int z)
{
    float var5 = 0.1875F;
    m->override = icon_from_side(m, 20, 1);    /* getBlockIcon(Blocks.glass) */
    rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    render_standard_block(m, id, x, y, z);
    m->render_all_faces = 1;
    m->override = icon_from_side(m, 49, 1);    /* getBlockIcon(Blocks.obsidian) */
    rb2_set_render_bounds(m, 0.125, 0.0062500000931322575, 0.125, 0.875, (double)var5, 0.875);
    render_standard_block(m, id, x, y, z);
    m->override = icon_from_side(m, 138, 1);   /* getBlockIcon(Blocks.beacon) */
    rb2_set_render_bounds(m, 0.1875, (double)var5, 0.1875, 0.8125, 0.875, 0.8125);
    render_standard_block(m, id, x, y, z);
    m->render_all_faces = 0;
    m->override = NULL;
    return 1;
}

/* ----------------------------------------------------------------- anvils */

/** RenderBlocks.renderBlockAnvilRotate. */
static float render_anvil_part(struct rb_mesher *m, int id, int x, int y, int z, int part,
                               float p6, float p7, float p8, float p9, int rotated)
{
    if (rotated)
    {
        float var13 = p7;
        p7 = p9;
        p9 = var13;
    }

    p7 /= 2.0F;
    p9 /= 2.0F;
    m->anvil_part = part;
    rb2_set_render_bounds(m, (double)(0.5F - p7), (double)p6, (double)(0.5F - p9),
                          (double)(0.5F + p7), (double)(p6 + p8), (double)(0.5F + p9));
    render_standard_block(m, id, x, y, z);
    return p6 + p8;
}

/** RenderBlocks.renderBlockAnvilOrient. */
static int render_anvil_orient(struct rb_mesher *m, int id, int x, int y, int z, int meta)
{
    int var7 = meta & 3;
    int var8 = 0;

    switch (var7)
    {
        case 0:
            m->uv_south = 2;
            m->uv_north = 1;
            m->uv_top = 3;
            m->uv_bottom = 3;
            break;

        case 1:
            m->uv_east = 1;
            m->uv_west = 2;
            m->uv_top = 2;
            m->uv_bottom = 1;
            var8 = 1;
            break;

        case 2:
            m->uv_south = 1;
            m->uv_north = 2;
            break;

        case 3:
            m->uv_east = 2;
            m->uv_west = 1;
            m->uv_top = 1;
            m->uv_bottom = 2;
            var8 = 1;
            break;
    }

    float var9 = 0.0F;
    var9 = render_anvil_part(m, id, x, y, z, 0, var9, 0.75F, 0.25F, 0.75F, var8);
    var9 = render_anvil_part(m, id, x, y, z, 1, var9, 0.5F, 0.0625F, 0.625F, var8);
    var9 = render_anvil_part(m, id, x, y, z, 2, var9, 0.25F, 0.3125F, 0.5F, var8);
    render_anvil_part(m, id, x, y, z, 3, var9, 0.625F, 0.375F, 1.0F, var8);
    rb2_set_render_bounds(m, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    m->uv_east = 0;
    m->uv_west = 0;
    m->uv_south = 0;
    m->uv_north = 0;
    m->uv_top = 0;
    m->uv_bottom = 0;
    return 1;
}

/** RenderBlocks.renderBlockAnvil: renderBlockAnvilMetadata with the cell's
 * metadata. */
int rb3_render_anvil(struct rb_mesher *m, int id, int x, int y, int z)
{
    return rb3_render_anvil_meta(m, id, x, y, z, rb_world_meta(m->w, x, y, z));
}

/** RenderBlocks.renderBlockAnvilMetadata (RenderFallingBlock passes the
 * falling the dev host's own metadata; the icons still read the cell's). */
int rb3_render_anvil_meta(struct rb_mesher *m, int id, int x, int y, int z, int meta)
{
    struct rb_tess *t = m->t;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int var7 = color_multiplier(m, id, x, y, z);
    float var8 = (float)(var7 >> 16 & 255) / 255.0F;
    float var9 = (float)(var7 >> 8 & 255) / 255.0F;
    float var10 = (float)(var7 & 255) / 255.0F;
    tess_set_color_opaque_f(t, var8, var9, var10);
    m->anvil_part = 0;
    int drawn = render_anvil_orient(m, id, x, y, z, meta);
    m->anvil_part = 0;
    return drawn;
}

/* ------------------------------------------------------------- comparators */

/* rendered above with the repeater */

/* ---------------------------------------------------------------- hoppers */

/** RenderBlocks.renderBlockHopperMetadata, the world path (p_147799_6_ false). */
int rb3_render_hopper(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    int var8 = meta & 7;                     /* BlockHopper.func_149918_b */
    double var9 = 0.625;
    rb2_set_render_bounds(m, 0.0, var9, 0.0, 1.0, 1.0, 1.0);
    render_standard_block(m, id, x, y, z);
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int var11 = color_multiplier(m, id, x, y, z);
    float var12 = (float)(var11 >> 16 & 255) / 255.0F;
    float var13 = (float)(var11 >> 8 & 255) / 255.0F;
    float var14 = (float)(var11 & 255) / 255.0F;
    tess_set_color_opaque_f(t, var12, var13, var14);
    const struct rb_uv *var24 = &m->special3[RB3_HOPPER_OUTSIDE];
    const struct rb_uv *var25 = &m->special3[RB3_HOPPER_INSIDE];
    float varf = 0.125F;
    render_face_x_pos(m, (double)((float)x - 1.0F + varf), (double)y, (double)z, var24);
    render_face_x_neg(m, (double)((float)x + 1.0F - varf), (double)y, (double)z, var24);
    render_face_z_pos(m, (double)x, (double)y, (double)((float)z - 1.0F + varf), var24);
    render_face_z_neg(m, (double)x, (double)y, (double)((float)z + 1.0F - varf), var24);
    render_face_y_pos(m, (double)x, (double)((float)y - 1.0F) + var9, (double)z, var25);
    m->override = var24;
    double var26 = 0.25;
    double var27 = 0.25;
    rb2_set_render_bounds(m, var26, var27, var26, 1.0 - var26, var9 - 0.002, 1.0 - var26);
    render_standard_block(m, id, x, y, z);

    if (var8 == 0)
    {
        rb2_set_render_bounds(m, 0.375, 0.0, 0.375, 0.625, 0.25, 0.625);
        render_standard_block(m, id, x, y, z);
    }

    if (var8 == 2)
    {
        rb2_set_render_bounds(m, 0.375, var27, 0.0, 0.625, var27 + 0.25, var26);
        render_standard_block(m, id, x, y, z);
    }

    if (var8 == 3)
    {
        rb2_set_render_bounds(m, 0.375, var27, 1.0 - var26, 0.625, var27 + 0.25, 1.0);
        render_standard_block(m, id, x, y, z);
    }

    if (var8 == 4)
    {
        rb2_set_render_bounds(m, 0.0, var27, 0.375, var26, var27 + 0.25, 0.625);
        render_standard_block(m, id, x, y, z);
    }

    if (var8 == 5)
    {
        rb2_set_render_bounds(m, 1.0 - var26, var27, 0.375, 1.0, var27 + 0.25, 0.625);
        render_standard_block(m, id, x, y, z);
    }

    m->override = NULL;
    return 1;
}

/* ----------------------------------------------------------------- quartz */

/** RenderBlocks.renderBlockQuartz. */
int rb3_render_quartz(struct rb_mesher *m, int id, int x, int y, int z)
{
    int var5 = rb_world_meta(m->w, x, y, z);

    if (var5 == 3)
    {
        m->uv_east = 1;
        m->uv_west = 1;
        m->uv_top = 1;
        m->uv_bottom = 1;
    }
    else if (var5 == 4)
    {
        m->uv_south = 1;
        m->uv_north = 1;
    }

    int var6 = render_standard_block(m, id, x, y, z);
    m->uv_south = 0;
    m->uv_east = 0;
    m->uv_west = 0;
    m->uv_north = 0;
    m->uv_top = 0;
    m->uv_bottom = 0;
    return var6;
}

/* ------------------------------------------------------------------ panes */

/** BlockPane.func_150098_a: does this pane connect to the block here. */
static int pane_connects(struct rb_mesher *m, int id, int x, int y, int z)
{
    int nid = rb_world_block(m->w, x, y, z);
    const char *cls = BLOCKS[nid].class_name;

    if (BLOCKS[nid].opaque_cube) return 1;
    if (nid == id) return 1;
    if (strcmp(BLOCKS[nid].name, "minecraft:glass") == 0) return 1;
    if (strcmp(BLOCKS[nid].name, "minecraft:stained_glass") == 0) return 1;
    if (strcmp(BLOCKS[nid].name, "minecraft:stained_glass_pane") == 0) return 1;
    if (strcmp(cls, "BlockPane") == 0 || strcmp(cls, "BlockStainedGlassPane") == 0) return 1;
    return 0;
}

/** RenderBlocks.renderBlockPane. */
int rb3_render_pane(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int var7 = color_multiplier(m, id, x, y, z);
    float var8 = (float)(var7 >> 16 & 255) / 255.0F;
    float var9 = (float)(var7 >> 8 & 255) / 255.0F;
    float var10 = (float)(var7 & 255) / 255.0F;
    tess_set_color_opaque_f(t, var8, var9, var10);
    const struct rb_uv *var63;
    const struct rb_uv *var64;

    if (m->override)
    {
        var63 = m->override;
        var64 = m->override;
    }
    else
    {
        int var65 = rb_world_meta(m->w, x, y, z);
        var63 = icon_from_side_meta(m, id, 0, var65);
        var64 = &m->special3[RB3_PANE_TOP_IRON];   /* renderBlockPane only draws the iron bars */
    }

    double var66 = (double)var63->min_u;
    double var15 = (double)interp_u(var63, 8.0);
    double var17 = (double)var63->max_u;
    double var19 = (double)var63->min_v;
    double var21 = (double)var63->max_v;
    double var23 = (double)interp_u(var64, 7.0);
    double var25 = (double)interp_u(var64, 9.0);
    double var27 = (double)var64->min_v;
    double var29 = (double)interp_v(var64, 8.0);
    double var31 = (double)var64->max_v;
    double var33 = (double)x;
    double var35 = (double)x + 0.5;
    double var37 = (double)(x + 1);
    double var39 = (double)z;
    double var41 = (double)z + 0.5;
    double var43 = (double)(z + 1);
    double var45 = (double)x + 0.5 - 0.0625;
    double var47 = (double)x + 0.5 + 0.0625;
    double var49 = (double)z + 0.5 - 0.0625;
    double var51 = (double)z + 0.5 + 0.0625;
    int var53 = pane_connects(m, id, x, y, z - 1);
    int var54 = pane_connects(m, id, x, y, z + 1);
    int var55 = pane_connects(m, id, x - 1, y, z);
    int var56 = pane_connects(m, id, x + 1, y, z);
    int var57 = should_side_be_rendered(m, id, x, y + 1, z, 1);
    int var58 = should_side_be_rendered(m, id, x, y - 1, z, 0);

        if ((!var55 || !var56) && (var55 || var56 || var53 || var54))
        {
            if (var55 && !var56)
            {
                tess_add_vertex_with_uv(t, var33, (double)(y + 1), var41, var66, var19);
                tess_add_vertex_with_uv(t, var33, (double)(y + 0), var41, var66, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var41, var15, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var41, var15, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var41, var66, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var41, var66, var21);
                tess_add_vertex_with_uv(t, var33, (double)(y + 0), var41, var15, var21);
                tess_add_vertex_with_uv(t, var33, (double)(y + 1), var41, var15, var19);

                if (!var54 && !var53)
                {
        tess_add_vertex_with_uv(t, var35, (double)(y + 1), var51, var23, var27);
        tess_add_vertex_with_uv(t, var35, (double)(y + 0), var51, var23, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 0), var49, var25, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1), var49, var25, var27);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1), var49, var23, var27);
        tess_add_vertex_with_uv(t, var35, (double)(y + 0), var49, var23, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 0), var51, var25, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1), var51, var25, var27);
                }

                if ((var57 || (y < 255 && is_air(m, x - 1, y + 1, z))))
                {
        tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var51, var25, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var49, var23, var31);
        tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var51, var25, var31);
        tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var49, var23, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var49, var23, var29);
                }

                if ((var58 || (y > 1 && is_air(m, x - 1, y - 1, z))))
                {
        tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var51, var25, var31);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var49, var23, var31);
        tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var51, var25, var31);
        tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var49, var23, var31);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var49, var23, var29);
                }
            }
            else if (!var55 && var56)
            {
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var41, var15, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var41, var15, var21);
                tess_add_vertex_with_uv(t, var37, (double)(y + 0), var41, var17, var21);
                tess_add_vertex_with_uv(t, var37, (double)(y + 1), var41, var17, var19);
                tess_add_vertex_with_uv(t, var37, (double)(y + 1), var41, var15, var19);
                tess_add_vertex_with_uv(t, var37, (double)(y + 0), var41, var15, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var41, var17, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var41, var17, var19);

                if (!var54 && !var53)
                {
        tess_add_vertex_with_uv(t, var35, (double)(y + 1), var49, var23, var27);
        tess_add_vertex_with_uv(t, var35, (double)(y + 0), var49, var23, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 0), var51, var25, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1), var51, var25, var27);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1), var51, var23, var27);
        tess_add_vertex_with_uv(t, var35, (double)(y + 0), var51, var23, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 0), var49, var25, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1), var49, var25, var27);
                }

                if ((var57 || (y < 255 && is_air(m, x + 1, y + 1, z))))
                {
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var51, var25, var27);
        tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var49, var23, var27);
        tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var51, var25, var27);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var49, var23, var27);
                }

                if ((var58 || (y > 1 && is_air(m, x + 1, y - 1, z))))
                {
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var51, var25, var27);
        tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var49, var23, var27);
        tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var51, var25, var27);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var49, var23, var27);
                }
            }
        }
        else
        {
            tess_add_vertex_with_uv(t, var33, (double)(y + 1), var41, var66, var19);
            tess_add_vertex_with_uv(t, var33, (double)(y + 0), var41, var66, var21);
            tess_add_vertex_with_uv(t, var37, (double)(y + 0), var41, var17, var21);
            tess_add_vertex_with_uv(t, var37, (double)(y + 1), var41, var17, var19);
            tess_add_vertex_with_uv(t, var37, (double)(y + 1), var41, var66, var19);
            tess_add_vertex_with_uv(t, var37, (double)(y + 0), var41, var66, var21);
            tess_add_vertex_with_uv(t, var33, (double)(y + 0), var41, var17, var21);
            tess_add_vertex_with_uv(t, var33, (double)(y + 1), var41, var17, var19);

            if (var57)
            {
                tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var51, var25, var31);
                tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var51, var25, var27);
                tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var49, var23, var27);
                tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var49, var23, var31);
                tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var51, var25, var31);
                tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var51, var25, var27);
                tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var49, var23, var27);
                tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var49, var23, var31);
            }
            else
            {
                if (y < 255 && is_air(m, x - 1, y + 1, z))
                {
        tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var51, var25, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var49, var23, var31);
        tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var51, var25, var31);
        tess_add_vertex_with_uv(t, var33, (double)(y + 1) + 0.01, var49, var23, var31);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var49, var23, var29);
                }

                if (y < 255 && is_air(m, x + 1, y + 1, z))
                {
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var51, var25, var27);
        tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var49, var23, var27);
        tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var51, var25, var27);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var35, (double)(y + 1) + 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var37, (double)(y + 1) + 0.01, var49, var23, var27);
                }
            }

            if (var58)
            {
                tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var51, var25, var31);
                tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var51, var25, var27);
                tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var49, var23, var27);
                tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var49, var23, var31);
                tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var51, var25, var31);
                tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var51, var25, var27);
                tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var49, var23, var27);
                tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var49, var23, var31);
            }
            else
            {
                if (y > 1 && is_air(m, x - 1, y - 1, z))
                {
        tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var51, var25, var31);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var49, var23, var31);
        tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var51, var25, var31);
        tess_add_vertex_with_uv(t, var33, (double)y - 0.01, var49, var23, var31);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var49, var23, var29);
                }

                if (y > 1 && is_air(m, x + 1, y - 1, z))
                {
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var51, var25, var27);
        tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var49, var23, var27);
        tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var51, var25, var27);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var51, var25, var29);
        tess_add_vertex_with_uv(t, var35, (double)y - 0.01, var49, var23, var29);
        tess_add_vertex_with_uv(t, var37, (double)y - 0.01, var49, var23, var27);
                }
            }
        }

        if ((!var53 || !var54) && (var55 || var56 || var53 || var54))
        {
            if (var53 && !var54)
            {
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var39, var66, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var39, var66, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var41, var15, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var41, var15, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var41, var66, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var41, var66, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var39, var15, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var39, var15, var19);

                if (!var56 && !var55)
                {
        tess_add_vertex_with_uv(t, var45, (double)(y + 1), var41, var23, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 0), var41, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 0), var41, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1), var41, var25, var27);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1), var41, var23, var27);
        tess_add_vertex_with_uv(t, var47, (double)(y + 0), var41, var23, var31);
        tess_add_vertex_with_uv(t, var45, (double)(y + 0), var41, var25, var31);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1), var41, var25, var27);
                }

                if ((var57 || (y < 255 && is_air(m, x, y + 1, z - 1))))
                {
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var39, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var41, var25, var29);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var41, var23, var29);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var39, var23, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var41, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var39, var25, var29);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var39, var23, var29);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var41, var23, var27);
                }

                if ((var58 || (y > 1 && is_air(m, x, y - 1, z - 1))))
                {
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var39, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var41, var25, var29);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var41, var23, var29);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var39, var23, var27);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var41, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var39, var25, var29);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var39, var23, var29);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var41, var23, var27);
                }
            }
            else if (!var53 && var54)
            {
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var41, var15, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var41, var15, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var43, var17, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var43, var17, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var43, var15, var19);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var43, var15, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 0), var41, var17, var21);
                tess_add_vertex_with_uv(t, var35, (double)(y + 1), var41, var17, var19);

                if (!var56 && !var55)
                {
        tess_add_vertex_with_uv(t, var47, (double)(y + 1), var41, var23, var27);
        tess_add_vertex_with_uv(t, var47, (double)(y + 0), var41, var23, var31);
        tess_add_vertex_with_uv(t, var45, (double)(y + 0), var41, var25, var31);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1), var41, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1), var41, var23, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 0), var41, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 0), var41, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1), var41, var25, var27);
                }

                if ((var57 || (y < 255 && is_air(m, x, y + 1, z + 1))))
                {
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var41, var23, var29);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var43, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var43, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var41, var25, var29);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var43, var23, var29);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var41, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var41, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var43, var25, var29);
                }

                if ((var58 || (y > 1 && is_air(m, x, y - 1, z + 1))))
                {
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var41, var23, var29);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var43, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var43, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var41, var25, var29);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var43, var23, var29);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var41, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var41, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var43, var25, var29);
                }
            }
        }
        else
        {
            tess_add_vertex_with_uv(t, var35, (double)(y + 1), var43, var66, var19);
            tess_add_vertex_with_uv(t, var35, (double)(y + 0), var43, var66, var21);
            tess_add_vertex_with_uv(t, var35, (double)(y + 0), var39, var17, var21);
            tess_add_vertex_with_uv(t, var35, (double)(y + 1), var39, var17, var19);
            tess_add_vertex_with_uv(t, var35, (double)(y + 1), var39, var66, var19);
            tess_add_vertex_with_uv(t, var35, (double)(y + 0), var39, var66, var21);
            tess_add_vertex_with_uv(t, var35, (double)(y + 0), var43, var17, var21);
            tess_add_vertex_with_uv(t, var35, (double)(y + 1), var43, var17, var19);

            if (var57)
            {
                tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var43, var25, var31);
                tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var39, var25, var27);
                tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var39, var23, var27);
                tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var43, var23, var31);
                tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var39, var25, var31);
                tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var43, var25, var27);
                tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var43, var23, var27);
                tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var39, var23, var31);
            }
            else
            {
                if (y < 255 && is_air(m, x, y + 1, z - 1))
                {
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var39, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var41, var25, var29);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var41, var23, var29);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var39, var23, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var41, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var39, var25, var29);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var39, var23, var29);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var41, var23, var27);
                }

                if (y < 255 && is_air(m, x, y + 1, z + 1))
                {
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var41, var23, var29);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var43, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var43, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var41, var25, var29);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var43, var23, var29);
        tess_add_vertex_with_uv(t, var45, (double)(y + 1) + 0.005, var41, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var41, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)(y + 1) + 0.005, var43, var25, var29);
                }
            }

            if (var58)
            {
                tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var43, var25, var31);
                tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var39, var25, var27);
                tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var39, var23, var27);
                tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var43, var23, var31);
                tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var39, var25, var31);
                tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var43, var25, var27);
                tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var43, var23, var27);
                tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var39, var23, var31);
            }
            else
            {
                if (y > 1 && is_air(m, x, y - 1, z - 1))
                {
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var39, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var41, var25, var29);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var41, var23, var29);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var39, var23, var27);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var41, var25, var27);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var39, var25, var29);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var39, var23, var29);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var41, var23, var27);
                }

                if (y > 1 && is_air(m, x, y - 1, z + 1))
                {
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var41, var23, var29);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var43, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var43, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var41, var25, var29);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var43, var23, var29);
        tess_add_vertex_with_uv(t, var45, (double)y - 0.005, var41, var23, var31);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var41, var25, var31);
        tess_add_vertex_with_uv(t, var47, (double)y - 0.005, var43, var25, var29);
                }
            }
        }

            return 1;
}

/** RenderBlocks.renderBlockStainedGlassPane. */
int rb3_render_stained_glass_pane(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    int var7 = color_multiplier(m, id, x, y, z);
    float var8 = (float)(var7 >> 16 & 255) / 255.0F;
    float var9 = (float)(var7 >> 8 & 255) / 255.0F;
    float var10 = (float)(var7 & 255) / 255.0F;
    tess_set_color_opaque_f(t, var8, var9, var10);
    const struct rb_uv *var65;
    const struct rb_uv *var66;

    if (m->override)
    {
        var65 = m->override;
        var66 = m->override;
    }
    else
    {
        int var14 = rb_world_meta(m->w, x, y, z);
        var65 = icon_from_side_meta(m, id, 0, var14);
        var66 = id == 160 ? &m->special3[RB3_PANE_TOP_STAINED + (var14 & 15)]
            : &m->special3[RB3_PANE_TOP];   /* the plain glass pane draws here too */
    }

    double var68 = (double)var65->min_u;
    double var16 = (double)interp_u(var65, 7.0);
    double var18 = (double)interp_u(var65, 9.0);
    double var20 = (double)var65->max_u;
    double var22 = (double)var65->min_v;
    double var24 = (double)var65->max_v;
    double var26 = (double)interp_u(var66, 7.0);
    double var28 = (double)interp_u(var66, 9.0);
    double var30 = (double)var66->min_v;
    double var32 = (double)var66->max_v;
    double var34 = (double)interp_v(var66, 7.0);
    double var36 = (double)interp_v(var66, 9.0);
    double var38 = (double)x;
    double var40 = (double)(x + 1);
    double var42 = (double)z;
    double var44 = (double)(z + 1);
    double var46 = (double)x + 0.5 - 0.0625;
    double var48 = (double)x + 0.5 + 0.0625;
    double var50 = (double)z + 0.5 - 0.0625;
    double var52 = (double)z + 0.5 + 0.0625;
    int var54 = pane_connects(m, id, x, y, z - 1);
    int var55 = pane_connects(m, id, x, y, z + 1);
    int var56 = pane_connects(m, id, x - 1, y, z);
    int var57 = pane_connects(m, id, x + 1, y, z);
    int var64 = !var54 && !var55 && !var56 && !var57;


        if (!var56 && !var64)
        {
            if (!var54 && !var55)
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var16, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var18, var22);
            }
        }
        else if (var56 && var57)
        {
            if (!var54)
            {
                tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var50, var20, var22);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var50, var20, var24);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var50, var68, var24);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var50, var68, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var16, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var50, var68, var24);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var50, var68, var22);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var50, var20, var22);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var50, var20, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var18, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var18, var22);
            }

            if (!var55)
            {
                tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var52, var68, var22);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var52, var68, var24);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var52, var20, var24);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var52, var20, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var52, var68, var22);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var52, var68, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var16, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var16, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var18, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var52, var20, var24);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var52, var20, var22);
            }

            tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var52, var28, var30);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var52, var28, var32);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var50, var26, var32);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var50, var26, var30);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var52, var26, var32);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var52, var26, var30);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var50, var28, var30);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var50, var28, var32);
        }
        else
        {
            if (!var54 && !var64)
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var18, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var18, var24);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var50, var68, var24);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var50, var68, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var16, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var50, var68, var24);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var50, var68, var22);
            }

            if (!var55 && !var64)
            {
                tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var52, var68, var22);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var52, var68, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var18, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var52, var68, var22);
                tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var52, var68, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var16, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var16, var22);
            }

            tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var52, var28, var30);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var28, var34);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var26, var34);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var50, var26, var30);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var26, var34);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var52, var26, var30);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var50, var28, var30);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var28, var34);
        }

        if ((var57 || var64) && !var56)
        {
            if (!var55 && !var64)
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var16, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var16, var24);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var52, var20, var24);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var52, var20, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var18, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var52, var20, var24);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var52, var20, var22);
            }

            if (!var54 && !var64)
            {
                tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var50, var20, var22);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var50, var20, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var16, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var50, var20, var22);
                tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var50, var20, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var18, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var18, var22);
            }

            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var28, var36);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var52, var28, var30);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var50, var26, var30);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var26, var36);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var52, var26, var32);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var26, var36);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var28, var36);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var50, var28, var32);
        }
        else if (!var57 && !var54 && !var55)
        {
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var16, var22);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var16, var24);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var18, var24);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var18, var22);
        }

        if (!var54 && !var64)
        {
            if (!var57 && !var56)
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var18, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var18, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var16, var22);
            }
        }
        else if (var54 && var55)
        {
            if (!var56)
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var42, var68, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var42, var68, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var44, var20, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var44, var20, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var42, var68, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var42, var68, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var16, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var18, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var44, var20, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var44, var20, var22);
            }

            if (!var57)
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var44, var20, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var44, var20, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var42, var68, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var42, var68, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var16, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var42, var68, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var42, var68, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var44, var20, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var44, var20, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var18, var22);
            }

            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var42, var28, var30);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var42, var26, var30);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var44, var26, var32);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var44, var28, var32);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var42, var26, var30);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var42, var28, var30);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var44, var28, var32);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var44, var26, var32);
        }
        else
        {
            if (!var56 && !var64)
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var42, var68, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var42, var68, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var18, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var42, var68, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var42, var68, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var16, var22);
            }

            if (!var57 && !var64)
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var18, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var42, var68, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var42, var68, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var16, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var42, var68, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var42, var68, var22);
            }

            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var42, var28, var30);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var42, var26, var30);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var26, var34);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var28, var34);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var42, var26, var30);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var42, var28, var30);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var28, var34);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var26, var34);
        }

        if ((var55 || var64) && !var54)
        {
            if (!var56 && !var64)
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var16, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var44, var20, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var44, var20, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var18, var22);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var44, var20, var24);
                tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var44, var20, var22);
            }

            if (!var57 && !var64)
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var44, var20, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var44, var20, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var16, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var16, var22);
            }
            else
            {
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var44, var20, var22);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var44, var20, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var18, var24);
                tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var18, var22);
            }

            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var28, var36);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var26, var36);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var44, var26, var32);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var44, var28, var32);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var26, var36);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var28, var36);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var44, var28, var32);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var44, var26, var32);
        }
        else if (!var55 && !var57 && !var56)
        {
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var16, var22);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var16, var24);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var18, var24);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var18, var22);
        }

        tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var50, var28, var34);
        tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var50, var26, var34);
        tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var52, var26, var36);
        tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var52, var28, var36);
        tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var50, var26, var34);
        tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var50, var28, var34);
        tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var52, var28, var36);
        tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var52, var26, var36);

        if (var64)
        {
            tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var50, var16, var22);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var50, var16, var24);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.001, var52, var18, var24);
            tess_add_vertex_with_uv(t, var38, (double)y + 0.999, var52, var18, var22);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var52, var16, var22);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var52, var16, var24);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.001, var50, var18, var24);
            tess_add_vertex_with_uv(t, var40, (double)y + 0.999, var50, var18, var22);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var42, var18, var22);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var42, var18, var24);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var42, var16, var24);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var42, var16, var22);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.999, var44, var16, var22);
            tess_add_vertex_with_uv(t, var46, (double)y + 0.001, var44, var16, var24);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.001, var44, var18, var24);
            tess_add_vertex_with_uv(t, var48, (double)y + 0.999, var44, var18, var22);
        }

            return 1;
}
