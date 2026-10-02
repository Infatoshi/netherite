/* RenderBlocks render types 2 torch, 3 fire, 5 redstone wire, 6 crops,
 * 7 doors, 8 ladders, 9 rails, 10 stairs, 11 fences, 12 levers, 13 cactus,
 * ported from RenderBlocks.java block by block like render_blocks.c's 0, 1, 4,
 * 31 and 40. The comments name the Java method each function reproduces; the
 * arithmetic follows it operation by operation, including the float/double
 * split (Java computes geometry in double and casts to float only where the
 * Tessellator stores it) and the order of the multiplications.
 *
 * Shared helpers (Tessellator, icons, light, colours, the six faces, the
 * standard-block paths) live in render_blocks.c and are declared in
 * render_blocks_int.h. What this file adds: the fire and redstone dust icons,
 * which table.bin cannot hold because getIcon(side, meta) never returns them,
 * are resolved from atlas.json by name at init, and RenderBlocks' override
 * texture (setOverrideBlockTexture) is m->override, applied inside the face
 * renderers.
 */
#include "render_blocks_int.h"

#include "blocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ icon helpers */

/** IIcon.getInterpolatedU/V go through the atlas uv like interp_u/interp_v. */

const struct rb_uv *rb_atlas_by_name(struct rb_mesher *m, const char *name)
{
    for (int i = 0; i < m->atlas->n; ++i)
    {
        if (strcmp(m->atlas->name[i], name) == 0) return &m->atlas->uv[i];
    }
    return NULL;
}

int rb2_init(struct rb_mesher *m)
{
    static const char *NAMES[RB2_SPECIALS] = {
        "fire_layer_0", "fire_layer_1",
        "redstone_dust_cross", "redstone_dust_line",
        "redstone_dust_cross_overlay", "redstone_dust_line_overlay"
    };

    for (int i = 0; i < RB2_SPECIALS; ++i)
    {
        const struct rb_uv *uv = rb_atlas_by_name(m, NAMES[i]);

        if (!uv)
        {
            fprintf(stderr, "render_blocks2: icon %s is not in atlas.json\n", NAMES[i]);
            return -1;
        }

        m->special2[i] = *uv;
    }

    return 0;
}

/** RenderBlocks.setRenderBounds: the render bounds and the partial flag. */
void rb2_set_render_bounds(struct rb_mesher *m, double x0, double y0, double z0,
                           double x1, double y1, double z1)
{
    m->min_x = x0;
    m->min_y = y0;
    m->min_z = z0;
    m->max_x = x1;
    m->max_y = y1;
    m->max_z = z1;
    /* gameSettings.ambientOcclusion is pinned at 2 */
    m->partial = m->min_x > 0.0 || m->max_x < 1.0 || m->min_y > 0.0 || m->max_y < 1.0
        || m->min_z > 0.0 || m->max_z < 1.0;
}

/** World.doesBlockHaveSolidTopSurface. */
int rb_solid_top_surface(struct rb_mesher *m, int x, int y, int z)
{
    int id = rb_world_block(m->w, x, y, z);
    int meta = rb_world_meta(m->w, x, y, z);

    if (MATERIALS[BLOCKS[id].material].is_opaque && BLOCKS[id].normal_block) return 1;
    if (strcmp(BLOCKS[id].class_name, "BlockStairs") == 0) return (meta & 4) == 4;
    if (m->tab->props[id].is_slab) return (meta & 8) == 8;
    if (strcmp(BLOCKS[id].name, "minecraft:hopper") == 0) return 1;
    if (strcmp(BLOCKS[id].class_name, "BlockSnow") == 0) return (meta & 7) == 7;
    return 0;
}

/* BlockFire.func_149844_e: can the block at this position catch fire. */
static int fire_can_catch(struct rb_mesher *m, int x, int y, int z)
{
    return FIRE_FLAM[rb_world_block(m->w, x, y, z)] > 0;
}

/** BlockRedstoneWire.func_150174_f: does this side connect for the wire's arm. */
static int wire_connects(struct rb_mesher *m, int x, int y, int z, int side)
{
    int nid = rb_world_block(m->w, x, y, z);
    const char *n = BLOCKS[nid].name;

    if (strcmp(n, "minecraft:redstone_wire") == 0) return 1;

    if (strcmp(n, "minecraft:unpowered_repeater") == 0 || strcmp(n, "minecraft:powered_repeater") == 0)
    {
        static const int ROT_OPPOSITE[4] = {2, 3, 0, 1};
        int meta = rb_world_meta(m->w, x, y, z);
        return side == (meta & 3) || side == ROT_OPPOSITE[meta & 3];
    }

    return BLOCKS[nid].provides_power && side != -1;
}

/* ------------------------------------------------------------------ torch */

/** RenderBlocks.renderTorchAtAngle. */
void rb_torch_at_angle(struct rb_mesher *m, int id, int meta, double x, double y, double z,
                       double p8, double p10)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *ic = icon_from_side_meta(m, id, 0, meta);
    if (m->override) ic = m->override;

    double min_u = (double)ic->min_u, min_v = (double)ic->min_v;
    double max_u = (double)ic->max_u, max_v = (double)ic->max_v;
    double u7 = (double)interp_u(ic, 7.0), v6 = (double)interp_v(ic, 6.0);
    double u9 = (double)interp_u(ic, 9.0), v8 = (double)interp_v(ic, 8.0);
    double u13 = (double)interp_u(ic, 7.0), v13 = (double)interp_v(ic, 13.0);
    double u15 = (double)interp_u(ic, 9.0), v15 = (double)interp_v(ic, 15.0);
    x += 0.5;
    z += 0.5;
    double x39 = x - 0.5, x41 = x + 0.5, z43 = z - 0.5, z45 = z + 0.5;
    double d47 = 0.0625, d49 = 0.625;

    tess_add_vertex_with_uv(t, x + p8 * (1.0 - d49) - d47, y + d49, z + p10 * (1.0 - d49) - d47, u7, v6);
    tess_add_vertex_with_uv(t, x + p8 * (1.0 - d49) - d47, y + d49, z + p10 * (1.0 - d49) + d47, u7, v8);
    tess_add_vertex_with_uv(t, x + p8 * (1.0 - d49) + d47, y + d49, z + p10 * (1.0 - d49) + d47, u9, v8);
    tess_add_vertex_with_uv(t, x + p8 * (1.0 - d49) + d47, y + d49, z + p10 * (1.0 - d49) - d47, u9, v6);
    tess_add_vertex_with_uv(t, x + d47 + p8, y, z - d47 + p10, u15, v13);
    tess_add_vertex_with_uv(t, x + d47 + p8, y, z + d47 + p10, u15, v15);
    tess_add_vertex_with_uv(t, x - d47 + p8, y, z + d47 + p10, u13, v15);
    tess_add_vertex_with_uv(t, x - d47 + p8, y, z - d47 + p10, u13, v13);
    tess_add_vertex_with_uv(t, x - d47, y + 1.0, z43, min_u, min_v);
    tess_add_vertex_with_uv(t, x - d47 + p8, y + 0.0, z43 + p10, min_u, max_v);
    tess_add_vertex_with_uv(t, x - d47 + p8, y + 0.0, z45 + p10, max_u, max_v);
    tess_add_vertex_with_uv(t, x - d47, y + 1.0, z45, max_u, min_v);
    tess_add_vertex_with_uv(t, x + d47, y + 1.0, z45, min_u, min_v);
    tess_add_vertex_with_uv(t, x + p8 + d47, y + 0.0, z45 + p10, min_u, max_v);
    tess_add_vertex_with_uv(t, x + p8 + d47, y + 0.0, z43 + p10, max_u, max_v);
    tess_add_vertex_with_uv(t, x + d47, y + 1.0, z43, max_u, min_v);
    tess_add_vertex_with_uv(t, x39, y + 1.0, z + d47, min_u, min_v);
    tess_add_vertex_with_uv(t, x39 + p8, y + 0.0, z + d47 + p10, min_u, max_v);
    tess_add_vertex_with_uv(t, x41 + p8, y + 0.0, z + d47 + p10, max_u, max_v);
    tess_add_vertex_with_uv(t, x41, y + 1.0, z + d47, max_u, min_v);
    tess_add_vertex_with_uv(t, x41, y + 1.0, z - d47, min_u, min_v);
    tess_add_vertex_with_uv(t, x41 + p8, y + 0.0, z - d47 + p10, min_u, max_v);
    tess_add_vertex_with_uv(t, x39 + p8, y + 0.0, z - d47 + p10, max_u, max_v);
    tess_add_vertex_with_uv(t, x39, y + 1.0, z - d47, max_u, min_v);
}

/** RenderBlocks.renderBlockTorch. */
int rb2_render_torch(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    double var7 = 0.4000000059604645;
    double var9 = 0.5 - var7;
    double var11 = 0.20000000298023224;

    if (meta == 1)
    {
        rb_torch_at_angle(m, id, 0, (double)x - var9, (double)y + var11, (double)z, -var7, 0.0);
    }
    else if (meta == 2)
    {
        rb_torch_at_angle(m, id, 0, (double)x + var9, (double)y + var11, (double)z, var7, 0.0);
    }
    else if (meta == 3)
    {
        rb_torch_at_angle(m, id, 0, (double)x, (double)y + var11, (double)z - var9, 0.0, -var7);
    }
    else if (meta == 4)
    {
        rb_torch_at_angle(m, id, 0, (double)x, (double)y + var11, (double)z + var9, 0.0, var7);
    }
    else
    {
        rb_torch_at_angle(m, id, 0, (double)x, (double)y, (double)z, 0.0, 0.0);
    }

    return 1;
}

/* ------------------------------------------------------------------- fire */

/** RenderBlocks.renderBlockFire. */
int rb2_render_fire(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *var6 = &m->special2[RB2_ICON_FIRE_0];
    const struct rb_uv *var7 = &m->special2[RB2_ICON_FIRE_1];
    const struct rb_uv *var8 = var6;

    if (m->override) var8 = m->override;

    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    double var9 = (double)var8->min_u;
    double var11 = (double)var8->min_v;
    double var13 = (double)var8->max_u;
    double var15 = (double)var8->max_v;
    float var17 = 1.4F;
    double var20, var22, var24, var26, var28, var30, var32;

    if (!rb_solid_top_surface(m, x, y - 1, z) && !fire_can_catch(m, x, y - 1, z))
    {
        float var36 = 0.2F;
        float var19 = 0.0625F;

        if (((x + y + z) & 1) == 1)
        {
            var9 = (double)var7->min_u;
            var11 = (double)var7->min_v;
            var13 = (double)var7->max_u;
            var15 = (double)var7->max_v;
        }

        if (((x / 2 + y / 2 + z / 2) & 1) == 1)
        {
            double s = var13;
            var13 = var9;
            var9 = s;
        }

        if (fire_can_catch(m, x - 1, y, z))
        {
            tess_add_vertex_with_uv(t, (double)((float)x + var36), (double)((float)y + var17 + var19), (double)(z + 1), var13, var11);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 0) + var19), (double)(z + 1), var13, var15);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 0) + var19), (double)(z + 0), var9, var15);
            tess_add_vertex_with_uv(t, (double)((float)x + var36), (double)((float)y + var17 + var19), (double)(z + 0), var9, var11);
            tess_add_vertex_with_uv(t, (double)((float)x + var36), (double)((float)y + var17 + var19), (double)(z + 0), var9, var11);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 0) + var19), (double)(z + 0), var9, var15);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 0) + var19), (double)(z + 1), var13, var15);
            tess_add_vertex_with_uv(t, (double)((float)x + var36), (double)((float)y + var17 + var19), (double)(z + 1), var13, var11);
        }

        if (fire_can_catch(m, x + 1, y, z))
        {
            tess_add_vertex_with_uv(t, (double)((float)(x + 1) - var36), (double)((float)y + var17 + var19), (double)(z + 0), var9, var11);
            tess_add_vertex_with_uv(t, (double)(x + 1 - 0), (double)((float)(y + 0) + var19), (double)(z + 0), var9, var15);
            tess_add_vertex_with_uv(t, (double)(x + 1 - 0), (double)((float)(y + 0) + var19), (double)(z + 1), var13, var15);
            tess_add_vertex_with_uv(t, (double)((float)(x + 1) - var36), (double)((float)y + var17 + var19), (double)(z + 1), var13, var11);
            tess_add_vertex_with_uv(t, (double)((float)(x + 1) - var36), (double)((float)y + var17 + var19), (double)(z + 1), var13, var11);
            tess_add_vertex_with_uv(t, (double)(x + 1 - 0), (double)((float)(y + 0) + var19), (double)(z + 1), var13, var15);
            tess_add_vertex_with_uv(t, (double)(x + 1 - 0), (double)((float)(y + 0) + var19), (double)(z + 0), var9, var15);
            tess_add_vertex_with_uv(t, (double)((float)(x + 1) - var36), (double)((float)y + var17 + var19), (double)(z + 0), var9, var11);
        }

        if (fire_can_catch(m, x, y, z - 1))
        {
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17 + var19), (double)((float)z + var36), var13, var11);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 0) + var19), (double)(z + 0), var13, var15);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)(y + 0) + var19), (double)(z + 0), var9, var15);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17 + var19), (double)((float)z + var36), var9, var11);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17 + var19), (double)((float)z + var36), var9, var11);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)(y + 0) + var19), (double)(z + 0), var9, var15);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 0) + var19), (double)(z + 0), var13, var15);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17 + var19), (double)((float)z + var36), var13, var11);
        }

        if (fire_can_catch(m, x, y, z + 1))
        {
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17 + var19), (double)((float)(z + 1) - var36), var9, var11);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)(y + 0) + var19), (double)(z + 1 - 0), var9, var15);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 0) + var19), (double)(z + 1 - 0), var13, var15);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17 + var19), (double)((float)(z + 1) - var36), var13, var11);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17 + var19), (double)((float)(z + 1) - var36), var13, var11);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 0) + var19), (double)(z + 1 - 0), var13, var15);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)(y + 0) + var19), (double)(z + 1 - 0), var9, var15);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17 + var19), (double)((float)(z + 1) - var36), var9, var11);
        }

        if (fire_can_catch(m, x, y + 1, z))
        {
            double var20 = (double)x + 0.5 + 0.5;
            double var22 = (double)x + 0.5 - 0.5;
            double var24 = (double)z + 0.5 + 0.5;
            double var26 = (double)z + 0.5 - 0.5;
            var28 = (double)x + 0.5 - 0.5;
            var30 = (double)x + 0.5 + 0.5;
            var32 = (double)z + 0.5 - 0.5;
            double var34 = (double)z + 0.5 + 0.5;
            var9 = (double)var6->min_u;
            var11 = (double)var6->min_v;
            var13 = (double)var6->max_u;
            var15 = (double)var6->max_v;
            ++y;
            var17 = -0.2F;

            if (((x + y + z) & 1) == 0)
            {
                tess_add_vertex_with_uv(t, var28, (double)((float)y + var17), (double)(z + 0), var13, var11);
                tess_add_vertex_with_uv(t, var20, (double)(y + 0), (double)(z + 0), var13, var15);
                tess_add_vertex_with_uv(t, var20, (double)(y + 0), (double)(z + 1), var9, var15);
                tess_add_vertex_with_uv(t, var28, (double)((float)y + var17), (double)(z + 1), var9, var11);
                var9 = (double)var7->min_u;
                var11 = (double)var7->min_v;
                var13 = (double)var7->max_u;
                var15 = (double)var7->max_v;
                tess_add_vertex_with_uv(t, var30, (double)((float)y + var17), (double)(z + 1), var13, var11);
                tess_add_vertex_with_uv(t, var22, (double)(y + 0), (double)(z + 1), var13, var15);
                tess_add_vertex_with_uv(t, var22, (double)(y + 0), (double)(z + 0), var9, var15);
                tess_add_vertex_with_uv(t, var30, (double)((float)y + var17), (double)(z + 0), var9, var11);
            }
            else
            {
                tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17), var34, var13, var11);
                tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), var26, var13, var15);
                tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), var26, var9, var15);
                tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17), var34, var9, var11);
                var9 = (double)var7->min_u;
                var11 = (double)var7->min_v;
                var13 = (double)var7->max_u;
                var15 = (double)var7->max_v;
                tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17), var32, var13, var11);
                tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), var24, var13, var15);
                tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), var24, var9, var15);
                tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17), var32, var9, var11);
            }
        }
    }
    else
    {
        double var18 = (double)x + 0.5 + 0.2;
        var20 = (double)x + 0.5 - 0.2;
        var22 = (double)z + 0.5 + 0.2;
        var24 = (double)z + 0.5 - 0.2;
        var26 = (double)x + 0.5 - 0.3;
        var28 = (double)x + 0.5 + 0.3;
        var30 = (double)z + 0.5 - 0.3;
        var32 = (double)z + 0.5 + 0.3;
        tess_add_vertex_with_uv(t, var26, (double)((float)y + var17), (double)(z + 1), var13, var11);
        tess_add_vertex_with_uv(t, var18, (double)(y + 0), (double)(z + 1), var13, var15);
        tess_add_vertex_with_uv(t, var18, (double)(y + 0), (double)(z + 0), var9, var15);
        tess_add_vertex_with_uv(t, var26, (double)((float)y + var17), (double)(z + 0), var9, var11);
        tess_add_vertex_with_uv(t, var28, (double)((float)y + var17), (double)(z + 0), var13, var11);
        tess_add_vertex_with_uv(t, var20, (double)(y + 0), (double)(z + 0), var13, var15);
        tess_add_vertex_with_uv(t, var20, (double)(y + 0), (double)(z + 1), var9, var15);
        tess_add_vertex_with_uv(t, var28, (double)((float)y + var17), (double)(z + 1), var9, var11);
        var9 = (double)var7->min_u;
        var11 = (double)var7->min_v;
        var13 = (double)var7->max_u;
        var15 = (double)var7->max_v;
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17), var32, var13, var11);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), var24, var13, var15);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), var24, var9, var15);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17), var32, var9, var11);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17), var30, var13, var11);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), var22, var13, var15);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), var22, var9, var15);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17), var30, var9, var11);
        var18 = (double)x + 0.5 - 0.5;
        var20 = (double)x + 0.5 + 0.5;
        var22 = (double)z + 0.5 - 0.5;
        var24 = (double)z + 0.5 + 0.5;
        var26 = (double)x + 0.5 - 0.4;
        var28 = (double)x + 0.5 + 0.4;
        var30 = (double)z + 0.5 - 0.4;
        var32 = (double)z + 0.5 + 0.4;
        tess_add_vertex_with_uv(t, var26, (double)((float)y + var17), (double)(z + 0), var9, var11);
        tess_add_vertex_with_uv(t, var18, (double)(y + 0), (double)(z + 0), var9, var15);
        tess_add_vertex_with_uv(t, var18, (double)(y + 0), (double)(z + 1), var13, var15);
        tess_add_vertex_with_uv(t, var26, (double)((float)y + var17), (double)(z + 1), var13, var11);
        tess_add_vertex_with_uv(t, var28, (double)((float)y + var17), (double)(z + 1), var9, var11);
        tess_add_vertex_with_uv(t, var20, (double)(y + 0), (double)(z + 1), var9, var15);
        tess_add_vertex_with_uv(t, var20, (double)(y + 0), (double)(z + 0), var13, var15);
        tess_add_vertex_with_uv(t, var28, (double)((float)y + var17), (double)(z + 0), var13, var11);
        var9 = (double)var6->min_u;
        var11 = (double)var6->min_v;
        var13 = (double)var6->max_u;
        var15 = (double)var6->max_v;
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17), var32, var9, var11);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), var24, var9, var15);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), var24, var13, var15);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17), var32, var13, var11);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)y + var17), var30, var9, var11);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), var22, var9, var15);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), var22, var13, var15);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)y + var17), var30, var13, var11);
    }

    return 1;
}

/* --------------------------------------------------------- redstone wire */

/** RenderBlocks.renderBlockRedstoneWire. */
int rb2_render_redstone_wire(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    const struct rb_uv *var7 = &m->special2[RB2_ICON_DUST_CROSS];
    const struct rb_uv *var8 = &m->special2[RB2_ICON_DUST_LINE];
    const struct rb_uv *var9 = &m->special2[RB2_ICON_DUST_CROSS_OVERLAY];
    const struct rb_uv *var10 = &m->special2[RB2_ICON_DUST_LINE_OVERLAY];
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    float var11 = (float)meta / 15.0F;
    float var12 = var11 * 0.6F + 0.4F;

    if (meta == 0) var12 = 0.3F;

    float var13 = var11 * var11 * 0.7F - 0.5F;
    float var14 = var11 * var11 * 0.6F - 0.7F;

    if (var13 < 0.0F) var13 = 0.0F;
    if (var14 < 0.0F) var14 = 0.0F;

    tess_set_color_opaque_f(t, var12, var13, var14);

    int var19 = wire_connects(m, x - 1, y, z, 1)
        || (!BLOCKS[rb_world_block(m->w, x - 1, y, z)].normal_cube && wire_connects(m, x - 1, y - 1, z, -1));
    int var20 = wire_connects(m, x + 1, y, z, 3)
        || (!BLOCKS[rb_world_block(m->w, x + 1, y, z)].normal_cube && wire_connects(m, x + 1, y - 1, z, -1));
    int var21 = wire_connects(m, x, y, z - 1, 2)
        || (!BLOCKS[rb_world_block(m->w, x, y, z - 1)].normal_cube && wire_connects(m, x, y - 1, z - 1, -1));
    int var22 = wire_connects(m, x, y, z + 1, 0)
        || (!BLOCKS[rb_world_block(m->w, x, y, z + 1)].normal_cube && wire_connects(m, x, y - 1, z + 1, -1));

    if (!BLOCKS[rb_world_block(m->w, x, y + 1, z)].normal_cube)
    {
        if (BLOCKS[rb_world_block(m->w, x - 1, y, z)].normal_cube && wire_connects(m, x - 1, y + 1, z, -1)) var19 = 1;
        if (BLOCKS[rb_world_block(m->w, x + 1, y, z)].normal_cube && wire_connects(m, x + 1, y + 1, z, -1)) var20 = 1;
        if (BLOCKS[rb_world_block(m->w, x, y, z - 1)].normal_cube && wire_connects(m, x, y + 1, z - 1, -1)) var21 = 1;
        if (BLOCKS[rb_world_block(m->w, x, y, z + 1)].normal_cube && wire_connects(m, x, y + 1, z + 1, -1)) var22 = 1;
    }

    float var23 = (float)(x + 0);
    float var24 = (float)(x + 1);
    float var25 = (float)(z + 0);
    float var26 = (float)(z + 1);
    int var27 = 0;

    if ((var19 || var20) && !var21 && !var22) var27 = 1;
    if ((var21 || var22) && !var20 && !var19) var27 = 2;

    if (var27 == 0)
    {
        int var28 = 0;
        int var29 = 0;
        int var30 = 16;
        int var31 = 16;

        if (!var19) { var23 += 0.3125F; var28 += 5; }
        if (!var20) { var24 -= 0.3125F; var30 -= 5; }
        if (!var21) { var25 += 0.3125F; var29 += 5; }
        if (!var22) { var26 -= 0.3125F; var31 -= 5; }

        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var26, (double)interp_u(var7, var30), (double)interp_v(var7, var31));
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var25, (double)interp_u(var7, var30), (double)interp_v(var7, var29));
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var25, (double)interp_u(var7, var28), (double)interp_v(var7, var29));
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var26, (double)interp_u(var7, var28), (double)interp_v(var7, var31));
        tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var26, (double)interp_u(var9, var30), (double)interp_v(var9, var31));
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var25, (double)interp_u(var9, var30), (double)interp_v(var9, var29));
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var25, (double)interp_u(var9, var28), (double)interp_v(var9, var29));
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var26, (double)interp_u(var9, var28), (double)interp_v(var9, var31));
    }
    else if (var27 == 1)
    {
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var26, (double)var8->max_u, (double)var8->max_v);
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var25, (double)var8->max_u, (double)var8->min_v);
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var25, (double)var8->min_u, (double)var8->min_v);
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var26, (double)var8->min_u, (double)var8->max_v);
        tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var26, (double)var10->max_u, (double)var10->max_v);
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var25, (double)var10->max_u, (double)var10->min_v);
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var25, (double)var10->min_u, (double)var10->min_v);
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var26, (double)var10->min_u, (double)var10->max_v);
    }
    else
    {
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var26, (double)var8->max_u, (double)var8->max_v);
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var25, (double)var8->min_u, (double)var8->max_v);
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var25, (double)var8->min_u, (double)var8->min_v);
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var26, (double)var8->max_u, (double)var8->min_v);
        tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var26, (double)var10->max_u, (double)var10->max_v);
        tess_add_vertex_with_uv(t, (double)var24, (double)y + 0.015625, (double)var25, (double)var10->min_u, (double)var10->max_v);
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var25, (double)var10->min_u, (double)var10->min_v);
        tess_add_vertex_with_uv(t, (double)var23, (double)y + 0.015625, (double)var26, (double)var10->max_u, (double)var10->min_v);
    }

    if (!BLOCKS[rb_world_block(m->w, x, y + 1, z)].normal_cube)
    {
        (void)0;   /* var33 0.021875F inlined below, as in RenderBlocks */

        if (BLOCKS[rb_world_block(m->w, x - 1, y, z)].normal_cube && rb_world_block(m->w, x - 1, y + 1, z) == id)
        {
            tess_set_color_opaque_f(t, var12, var13, var14);
            tess_add_vertex_with_uv(t, (double)x + 0.015625, (double)((float)(y + 1) + 0.021875F), (double)(z + 1), (double)var8->max_u, (double)var8->min_v);
            tess_add_vertex_with_uv(t, (double)x + 0.015625, (double)(y + 0), (double)(z + 1), (double)var8->min_u, (double)var8->min_v);
            tess_add_vertex_with_uv(t, (double)x + 0.015625, (double)(y + 0), (double)(z + 0), (double)var8->min_u, (double)var8->max_v);
            tess_add_vertex_with_uv(t, (double)x + 0.015625, (double)((float)(y + 1) + 0.021875F), (double)(z + 0), (double)var8->max_u, (double)var8->max_v);
            tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
            tess_add_vertex_with_uv(t, (double)x + 0.015625, (double)((float)(y + 1) + 0.021875F), (double)(z + 1), (double)var10->max_u, (double)var10->min_v);
            tess_add_vertex_with_uv(t, (double)x + 0.015625, (double)(y + 0), (double)(z + 1), (double)var10->min_u, (double)var10->min_v);
            tess_add_vertex_with_uv(t, (double)x + 0.015625, (double)(y + 0), (double)(z + 0), (double)var10->min_u, (double)var10->max_v);
            tess_add_vertex_with_uv(t, (double)x + 0.015625, (double)((float)(y + 1) + 0.021875F), (double)(z + 0), (double)var10->max_u, (double)var10->max_v);
        }

        if (BLOCKS[rb_world_block(m->w, x + 1, y, z)].normal_cube && rb_world_block(m->w, x + 1, y + 1, z) == id)
        {
            tess_set_color_opaque_f(t, var12, var13, var14);
            tess_add_vertex_with_uv(t, (double)(x + 1) - 0.015625, (double)(y + 0), (double)(z + 1), (double)var8->min_u, (double)var8->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 1) - 0.015625, (double)((float)(y + 1) + 0.021875F), (double)(z + 1), (double)var8->max_u, (double)var8->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 1) - 0.015625, (double)((float)(y + 1) + 0.021875F), (double)(z + 0), (double)var8->max_u, (double)var8->min_v);
            tess_add_vertex_with_uv(t, (double)(x + 1) - 0.015625, (double)(y + 0), (double)(z + 0), (double)var8->min_u, (double)var8->min_v);
            tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
            tess_add_vertex_with_uv(t, (double)(x + 1) - 0.015625, (double)(y + 0), (double)(z + 1), (double)var10->min_u, (double)var10->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 1) - 0.015625, (double)((float)(y + 1) + 0.021875F), (double)(z + 1), (double)var10->max_u, (double)var10->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 1) - 0.015625, (double)((float)(y + 1) + 0.021875F), (double)(z + 0), (double)var10->max_u, (double)var10->min_v);
            tess_add_vertex_with_uv(t, (double)(x + 1) - 0.015625, (double)(y + 0), (double)(z + 0), (double)var10->min_u, (double)var10->min_v);
        }

        if (BLOCKS[rb_world_block(m->w, x, y, z - 1)].normal_cube && rb_world_block(m->w, x, y + 1, z - 1) == id)
        {
            tess_set_color_opaque_f(t, var12, var13, var14);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), (double)z + 0.015625, (double)var8->min_u, (double)var8->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)(y + 1) + 0.021875F), (double)z + 0.015625, (double)var8->max_u, (double)var8->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 1) + 0.021875F), (double)z + 0.015625, (double)var8->max_u, (double)var8->min_v);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), (double)z + 0.015625, (double)var8->min_u, (double)var8->min_v);
            tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), (double)z + 0.015625, (double)var10->min_u, (double)var10->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)(y + 1) + 0.021875F), (double)z + 0.015625, (double)var10->max_u, (double)var10->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 1) + 0.021875F), (double)z + 0.015625, (double)var10->max_u, (double)var10->min_v);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), (double)z + 0.015625, (double)var10->min_u, (double)var10->min_v);
        }

        if (BLOCKS[rb_world_block(m->w, x, y, z + 1)].normal_cube && rb_world_block(m->w, x, y + 1, z + 1) == id)
        {
            tess_set_color_opaque_f(t, var12, var13, var14);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)(y + 1) + 0.021875F), (double)(z + 1) - 0.015625, (double)var8->max_u, (double)var8->min_v);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), (double)(z + 1) - 0.015625, (double)var8->min_u, (double)var8->min_v);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), (double)(z + 1) - 0.015625, (double)var8->min_u, (double)var8->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 1) + 0.021875F), (double)(z + 1) - 0.015625, (double)var8->max_u, (double)var8->max_v);
            tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)((float)(y + 1) + 0.021875F), (double)(z + 1) - 0.015625, (double)var10->max_u, (double)var10->min_v);
            tess_add_vertex_with_uv(t, (double)(x + 1), (double)(y + 0), (double)(z + 1) - 0.015625, (double)var10->min_u, (double)var10->min_v);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)(y + 0), (double)(z + 1) - 0.015625, (double)var10->min_u, (double)var10->max_v);
            tess_add_vertex_with_uv(t, (double)(x + 0), (double)((float)(y + 1) + 0.021875F), (double)(z + 1) - 0.015625, (double)var10->max_u, (double)var10->max_v);
        }
    }

    return 1;
}

/* ------------------------------------------------------------------ crops */

/** RenderBlocks.renderBlockCropsImpl. */
static void render_crops_impl(struct rb_mesher *m, int id, int meta, double x, double y, double z)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *ic = icon_from_side_meta(m, id, 0, meta);

    if (m->override) ic = m->override;

    double u0 = (double)ic->min_u, v0 = (double)ic->min_v;
    double u1 = (double)ic->max_u, v1 = (double)ic->max_v;
    double x0 = x + 0.5 - 0.25, x1 = x + 0.5 + 0.25;
    double z0 = z + 0.5 - 0.5, z1 = z + 0.5 + 0.5;
    tess_add_vertex_with_uv(t, x0, y + 1.0, z0, u0, v0);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z0, u0, v1);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z1, u1, v1);
    tess_add_vertex_with_uv(t, x0, y + 1.0, z1, u1, v0);
    tess_add_vertex_with_uv(t, x0, y + 1.0, z1, u0, v0);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z1, u0, v1);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z0, u1, v1);
    tess_add_vertex_with_uv(t, x0, y + 1.0, z0, u1, v0);
    tess_add_vertex_with_uv(t, x1, y + 1.0, z1, u0, v0);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z1, u0, v1);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z0, u1, v1);
    tess_add_vertex_with_uv(t, x1, y + 1.0, z0, u1, v0);
    tess_add_vertex_with_uv(t, x1, y + 1.0, z0, u0, v0);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z0, u0, v1);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z1, u1, v1);
    tess_add_vertex_with_uv(t, x1, y + 1.0, z1, u1, v0);
    x0 = x + 0.5 - 0.5;
    x1 = x + 0.5 + 0.5;
    z0 = z + 0.5 - 0.25;
    z1 = z + 0.5 + 0.25;
    tess_add_vertex_with_uv(t, x0, y + 1.0, z0, u0, v0);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z0, u0, v1);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z0, u1, v1);
    tess_add_vertex_with_uv(t, x1, y + 1.0, z0, u1, v0);
    tess_add_vertex_with_uv(t, x1, y + 1.0, z0, u0, v0);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z0, u0, v1);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z0, u1, v1);
    tess_add_vertex_with_uv(t, x0, y + 1.0, z0, u1, v0);
    tess_add_vertex_with_uv(t, x1, y + 1.0, z1, u0, v0);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z1, u0, v1);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z1, u1, v1);
    tess_add_vertex_with_uv(t, x0, y + 1.0, z1, u1, v0);
    tess_add_vertex_with_uv(t, x0, y + 1.0, z1, u0, v0);
    tess_add_vertex_with_uv(t, x0, y + 0.0, z1, u0, v1);
    tess_add_vertex_with_uv(t, x1, y + 0.0, z1, u1, v1);
    tess_add_vertex_with_uv(t, x1, y + 1.0, z1, u1, v0);
}

/** RenderBlocks.renderBlockCrops. */
int rb2_render_crops(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    render_crops_impl(m, id, rb_world_meta(m->w, x, y, z),
                      (double)x, (double)((float)y - 0.0625F), (double)z);
    return 1;
}

/* ------------------------------------------------------------------- door */

/** BlockDoor.func_150012_g: the lower meta, the upper flag and the hinge bit. */
int door_state(struct rb_mesher *m, int x, int y, int z)
{
    int meta = rb_world_meta(m->w, x, y, z);
    int upper = (meta & 8) != 0;
    int lower, other;

    if (upper)
    {
        lower = rb_world_meta(m->w, x, y - 1, z);
        other = meta;
    }
    else
    {
        lower = meta;
        other = rb_world_meta(m->w, x, y + 1, z);
    }

    return (lower & 7) | (upper ? 8 : 0) | ((other & 1) != 0 ? 16 : 0);
}

/** BlockDoor.func_150011_b: the block bounds of one door half, in doubles. */
void door_bounds(int state, double *out)
{
    float var2 = 0.1875F;
    int var3 = state & 3;
    int var4 = (state & 4) != 0;
    int var5 = (state & 16) != 0;

    out[0] = 0.0; out[1] = 0.0; out[2] = 0.0; out[3] = 1.0; out[4] = 1.0; out[5] = 1.0;

    if (var3 == 0)
    {
        if (var4) { if (!var5) out[5] = var2; else { out[2] = 1.0F - var2; } }
        else { out[3] = var2; }
    }
    else if (var3 == 1)
    {
        if (var4) { if (!var5) { out[0] = 1.0F - var2; } else { out[3] = var2; } }
        else { out[5] = var2; }
    }
    else if (var3 == 2)
    {
        if (var4) { if (!var5) { out[2] = 1.0F - var2; } else { out[5] = var2; } }
        else { out[0] = 1.0F - var2; }
    }
    else
    {
        if (var4) { if (!var5) { out[3] = var2; } else { out[0] = 1.0F - var2; } }
        else { out[2] = 1.0F - var2; }
    }
}

/** The "_upper" icon of a door, resolved by name from the lower one's name. */
static const struct rb_uv *door_upper_uv(struct rb_mesher *m, int id)
{
    const char *lower_name = icon_name(m->tab, m->tab->icon_index[(id * RB_METAS + 0) * RB_SIDES + 0]);
    char buf[64];
    snprintf(buf, sizeof buf, "%s", lower_name);
    size_t n = strlen(buf);
    if (n > 6 && strcmp(buf + n - 6, "_lower") == 0) strcpy(buf + n - 6, "_upper");
    const struct rb_uv *uv = rb_atlas_by_name(m, buf);

    if (!uv)
    {
        fprintf(stderr, "render_blocks2: door icon %s is not in atlas.json\n", buf);
        exit(2);
    }

    return uv;
}

/** BlockDoor.getIcon(world, x, y, z, side): the flipped icon swaps min/max u. */
void door_icon(struct rb_mesher *m, int id, int side, int state, struct rb_uv *out)
{
    const struct rb_uv *lower = icon_from_side_meta(m, id, 0, 0);

    if (side == 1 || side == 0)
    {
        *out = *lower;
        return;
    }

    int var3 = state & 3;
    int var4 = (state & 4) != 0;
    int var9 = 0;
    int upper = (state & 8) != 0;

    if (var4)
    {
        if ((var3 == 0 && side == 2) || (var3 == 1 && side == 5)
            || (var3 == 2 && side == 3) || (var3 == 3 && side == 4))
        {
            var9 = 1;
        }
    }
    else
    {
        if ((var3 == 0 && side == 5) || (var3 == 1 && side == 3)
            || (var3 == 2 && side == 4) || (var3 == 3 && side == 2))
        {
            var9 = 1;
        }

        if ((state & 16) != 0) var9 = !var9;
    }

    const struct rb_uv *base = upper ? door_upper_uv(m, id) : lower;
    *out = *base;

    if (var9)
    {
        float s = out->min_u;
        out->min_u = out->max_u;
        out->max_u = s;
    }
}

/** RenderBlocks.renderBlockDoor. */
int rb2_render_door(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    int above = rb_world_block(m->w, x, y + 1, z);
    int below = rb_world_block(m->w, x, y - 1, z);

    if ((meta & 8) != 0)
    {
        if (below != id) return 0;
    }
    else if (above != id)
    {
        return 0;
    }

    int state = door_state(m, x, y, z);
    double b[6];
    door_bounds(state, b);
    rb2_set_render_bounds(m, b[0], b[1], b[2], b[3], b[4], b[5]);
    int drawn = 0;
    float var8 = 0.5F, var9 = 1.0F, var10 = 0.8F, var11 = 0.6F;
    int var12 = block_brightness(m, id, x, y, z);
    struct rb_uv ic;

    tess_set_brightness(t, m->min_y > 0.0 ? var12 : block_brightness(m, id, x, y - 1, z));
    tess_set_color_opaque_f(t, var8, var8, var8);
    door_icon(m, id, 0, state, &ic);
    render_face_y_neg(m, (double)x, (double)y, (double)z, &ic);
    drawn = 1;
    tess_set_brightness(t, m->max_y < 1.0 ? var12 : block_brightness(m, id, x, y + 1, z));
    tess_set_color_opaque_f(t, var9, var9, var9);
    door_icon(m, id, 1, state, &ic);
    render_face_y_pos(m, (double)x, (double)y, (double)z, &ic);
    drawn = 1;
    tess_set_brightness(t, m->min_z > 0.0 ? var12 : block_brightness(m, id, x, y, z - 1));
    tess_set_color_opaque_f(t, var10, var10, var10);
    door_icon(m, id, 2, state, &ic);
    m->flip_texture = 0;
    render_face_z_neg(m, (double)x, (double)y, (double)z, &ic);
    drawn = 1;
    tess_set_brightness(t, m->max_z < 1.0 ? var12 : block_brightness(m, id, x, y, z + 1));
    tess_set_color_opaque_f(t, var10, var10, var10);
    door_icon(m, id, 3, state, &ic);
    m->flip_texture = 0;
    render_face_z_pos(m, (double)x, (double)y, (double)z, &ic);
    drawn = 1;
    tess_set_brightness(t, m->min_x > 0.0 ? var12 : block_brightness(m, id, x - 1, y, z));
    tess_set_color_opaque_f(t, var11, var11, var11);
    door_icon(m, id, 4, state, &ic);
    m->flip_texture = 0;
    render_face_x_neg(m, (double)x, (double)y, (double)z, &ic);
    drawn = 1;
    tess_set_brightness(t, m->max_x < 1.0 ? var12 : block_brightness(m, id, x + 1, y, z));
    tess_set_color_opaque_f(t, var11, var11, var11);
    door_icon(m, id, 5, state, &ic);
    m->flip_texture = 0;
    render_face_x_pos(m, (double)x, (double)y, (double)z, &ic);
    drawn = 1;
    m->flip_texture = 0;
    return drawn;
}

/* ----------------------------------------------------------------- ladder */

/** RenderBlocks.renderBlockLadder. */
int rb2_render_ladder(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    const struct rb_uv *ic = icon_from_side(m, id, 0);

    if (m->override) ic = m->override;

    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    double var7 = (double)ic->min_u, var9 = (double)ic->min_v;
    double var11 = (double)ic->max_u, var13 = (double)ic->max_v;
    int meta = rb_world_meta(m->w, x, y, z);
    double var16 = 0.0;
    double var18 = 0.05000000074505806;

    if (meta == 5)
    {
        tess_add_vertex_with_uv(t, (double)x + var18, (double)(y + 1) + var16, (double)(z + 1) + var16, var7, var9);
        tess_add_vertex_with_uv(t, (double)x + var18, (double)(y + 0) - var16, (double)(z + 1) + var16, var7, var13);
        tess_add_vertex_with_uv(t, (double)x + var18, (double)(y + 0) - var16, (double)(z + 0) - var16, var11, var13);
        tess_add_vertex_with_uv(t, (double)x + var18, (double)(y + 1) + var16, (double)(z + 0) - var16, var11, var9);
    }

    if (meta == 4)
    {
        tess_add_vertex_with_uv(t, (double)(x + 1) - var18, (double)(y + 0) - var16, (double)(z + 1) + var16, var11, var13);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var18, (double)(y + 1) + var16, (double)(z + 1) + var16, var11, var9);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var18, (double)(y + 1) + var16, (double)(z + 0) - var16, var7, var9);
        tess_add_vertex_with_uv(t, (double)(x + 1) - var18, (double)(y + 0) - var16, (double)(z + 0) - var16, var7, var13);
    }

    if (meta == 3)
    {
        tess_add_vertex_with_uv(t, (double)(x + 1) + var16, (double)(y + 0) - var16, (double)z + var18, var11, var13);
        tess_add_vertex_with_uv(t, (double)(x + 1) + var16, (double)(y + 1) + var16, (double)z + var18, var11, var9);
        tess_add_vertex_with_uv(t, (double)(x + 0) - var16, (double)(y + 1) + var16, (double)z + var18, var7, var9);
        tess_add_vertex_with_uv(t, (double)(x + 0) - var16, (double)(y + 0) - var16, (double)z + var18, var7, var13);
    }

    if (meta == 2)
    {
        tess_add_vertex_with_uv(t, (double)(x + 1) + var16, (double)(y + 1) + var16, (double)(z + 1) - var18, var7, var9);
        tess_add_vertex_with_uv(t, (double)(x + 1) + var16, (double)(y + 0) - var16, (double)(z + 1) - var18, var7, var13);
        tess_add_vertex_with_uv(t, (double)(x + 0) - var16, (double)(y + 0) - var16, (double)(z + 1) - var18, var11, var13);
        tess_add_vertex_with_uv(t, (double)(x + 0) - var16, (double)(y + 1) + var16, (double)(z + 1) - var18, var11, var9);
    }

    return 1;
}

/* ------------------------------------------------------------------- rail */

/** RenderBlocks.renderBlockMinecartTrack. */
int rb2_render_rail(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    const struct rb_uv *var7 = icon_from_side_meta(m, id, 0, meta);

    if (m->override) var7 = m->override;

    if (strcmp(BLOCKS[id].class_name, "BlockRailPowered") == 0
        || strcmp(BLOCKS[id].class_name, "BlockRailDetector") == 0)
    {
        meta &= 7;
    }

    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    double var8 = (double)var7->min_u;
    double var10 = (double)var7->min_v;
    double var12 = (double)var7->max_u;
    double var14 = (double)var7->max_v;
    double var16 = 0.0625;
    double var18 = (double)(x + 1);
    double var20 = (double)(x + 1);
    double var22 = (double)(x + 0);
    double var24 = (double)(x + 0);
    double var26 = (double)(z + 0);
    double var28 = (double)(z + 1);
    double var30 = (double)(z + 1);
    double var32 = (double)(z + 0);
    double var34 = (double)y + var16;
    double var36 = (double)y + var16;
    double var38 = (double)y + var16;
    double var40 = (double)y + var16;

    if (meta != 1 && meta != 2 && meta != 3 && meta != 7)
    {
        if (meta == 8)
        {
            var18 = var20 = (double)(x + 0);
            var22 = var24 = (double)(x + 1);
            var26 = var32 = (double)(z + 1);
            var28 = var30 = (double)(z + 0);
        }
        else if (meta == 9)
        {
            var18 = var24 = (double)(x + 0);
            var20 = var22 = (double)(x + 1);
            var26 = var28 = (double)(z + 0);
            var30 = var32 = (double)(z + 1);
        }
    }
    else
    {
        var18 = var24 = (double)(x + 1);
        var20 = var22 = (double)(x + 0);
        var26 = var28 = (double)(z + 1);
        var30 = var32 = (double)(z + 0);
    }

    if (meta != 2 && meta != 4)
    {
        if (meta == 3 || meta == 5)
        {
            ++var36;
            ++var38;
        }
    }
    else
    {
        ++var34;
        ++var40;
    }

    tess_add_vertex_with_uv(t, var18, var34, var26, var12, var10);
    tess_add_vertex_with_uv(t, var20, var36, var28, var12, var14);
    tess_add_vertex_with_uv(t, var22, var38, var30, var8, var14);
    tess_add_vertex_with_uv(t, var24, var40, var32, var8, var10);
    tess_add_vertex_with_uv(t, var24, var40, var32, var8, var10);
    tess_add_vertex_with_uv(t, var22, var38, var30, var8, var14);
    tess_add_vertex_with_uv(t, var20, var36, var28, var12, var14);
    tess_add_vertex_with_uv(t, var18, var34, var26, var12, var10);
    return 1;
}

/* ------------------------------------------------------------------ stairs */

/** BlockStairs.func_150148_a. */
static int is_stairs(int id)
{
    return strcmp(BLOCKS[id].class_name, "BlockStairs") == 0;
}

/** BlockStairs.func_150146_f: a stairs of exactly this metadata. */
static int stairs_same(struct rb_mesher *m, int x, int y, int z, int meta)
{
    return is_stairs(rb_world_block(m->w, x, y, z)) && rb_world_meta(m->w, x, y, z) == meta;
}

/** BlockStairs.func_150145_f: the top half's bounds, and whether the corner connects. */
int stairs_top_bounds(struct rb_mesher *m, int x, int y, int z, double *out)
{
    int meta = rb_world_meta(m->w, x, y, z);
    int var6 = meta & 3;
    double var7 = (meta & 4) != 0 ? 0.0 : 0.5;
    double var8 = (meta & 4) != 0 ? 0.5 : 1.0;
    double var9 = 0.0, var10 = 1.0, var11 = 0.0, var12 = 0.5;
    int var13 = 1;

    if (var6 == 0)
    {
        var9 = 0.5;
        var12 = 1.0;
        if (is_stairs(rb_world_block(m->w, x + 1, y, z))
            && (meta & 4) == (rb_world_meta(m->w, x + 1, y, z) & 4))
        {
            int var16 = rb_world_meta(m->w, x + 1, y, z) & 3;

            if (var16 == 3 && !stairs_same(m, x, y, z + 1, meta))
            {
                var12 = 0.5;
                var13 = 0;
            }
            else if (var16 == 2 && !stairs_same(m, x, y, z - 1, meta))
            {
                var11 = 0.5;
                var13 = 0;
            }
        }
    }
    else if (var6 == 1)
    {
        var10 = 0.5;
        var12 = 1.0;
        int nid = rb_world_block(m->w, x - 1, y, z);
        int nmeta = rb_world_meta(m->w, x - 1, y, z);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 3 && !stairs_same(m, x, y, z + 1, meta))
            {
                var12 = 0.5;
                var13 = 0;
            }
            else if (var16 == 2 && !stairs_same(m, x, y, z - 1, meta))
            {
                var11 = 0.5;
                var13 = 0;
            }
        }
    }
    else if (var6 == 2)
    {
        var11 = 0.5;
        var12 = 1.0;
        int nid = rb_world_block(m->w, x, y, z + 1);
        int nmeta = rb_world_meta(m->w, x, y, z + 1);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 1 && !stairs_same(m, x + 1, y, z, meta))
            {
                var10 = 0.5;
                var13 = 0;
            }
            else if (var16 == 0 && !stairs_same(m, x - 1, y, z, meta))
            {
                var9 = 0.5;
                var13 = 0;
            }
        }
    }
    else
    {
        int nid = rb_world_block(m->w, x, y, z - 1);
        int nmeta = rb_world_meta(m->w, x, y, z - 1);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 1 && !stairs_same(m, x + 1, y, z, meta))
            {
                var10 = 0.5;
                var13 = 0;
            }
            else if (var16 == 0 && !stairs_same(m, x - 1, y, z, meta))
            {
                var9 = 0.5;
                var13 = 0;
            }
        }
    }

    out[0] = var9; out[1] = var7; out[2] = var11; out[3] = var10; out[4] = var8; out[5] = var12;
    return var13;
}

/** BlockStairs.func_150144_g: the connected corner's bounds, or 0 when it does not. */
int stairs_corner_bounds(struct rb_mesher *m, int x, int y, int z, double *out)
{
    int meta = rb_world_meta(m->w, x, y, z);
    int var6 = meta & 3;
    double var7 = (meta & 4) != 0 ? 0.0 : 0.5;
    double var8 = (meta & 4) != 0 ? 0.5 : 1.0;
    double var9 = 0.0, var10 = 0.5, var11 = 0.5, var12 = 1.0;
    int var13 = 0;

    if (var6 == 0)
    {
        int nid = rb_world_block(m->w, x - 1, y, z);
        int nmeta = rb_world_meta(m->w, x - 1, y, z);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 3 && !stairs_same(m, x, y, z - 1, meta))
            {
                var11 = 0.0;
                var12 = 0.5;
                var13 = 1;
            }
            else if (var16 == 2 && !stairs_same(m, x, y, z + 1, meta))
            {
                var11 = 0.5;
                var12 = 1.0;
                var13 = 1;
            }
        }
    }
    else if (var6 == 1)
    {
        int nid = rb_world_block(m->w, x + 1, y, z);
        int nmeta = rb_world_meta(m->w, x + 1, y, z);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            var9 = 0.5;
            var10 = 1.0;
            int var16 = nmeta & 3;

            if (var16 == 3 && !stairs_same(m, x, y, z - 1, meta))
            {
                var11 = 0.0;
                var12 = 0.5;
                var13 = 1;
            }
            else if (var16 == 2 && !stairs_same(m, x, y, z + 1, meta))
            {
                var11 = 0.5;
                var12 = 1.0;
                var13 = 1;
            }
        }
    }
    else if (var6 == 2)
    {
        int nid = rb_world_block(m->w, x, y, z - 1);
        int nmeta = rb_world_meta(m->w, x, y, z - 1);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            var11 = 0.0;
            var12 = 0.5;
            int var16 = nmeta & 3;

            if (var16 == 1 && !stairs_same(m, x - 1, y, z, meta))
            {
                var13 = 1;
            }
            else if (var16 == 0 && !stairs_same(m, x + 1, y, z, meta))
            {
                var9 = 0.5;
                var10 = 1.0;
                var13 = 1;
            }
        }
    }
    else
    {
        int nid = rb_world_block(m->w, x, y, z + 1);
        int nmeta = rb_world_meta(m->w, x, y, z + 1);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 1 && !stairs_same(m, x - 1, y, z, meta))
            {
                var13 = 1;
            }
            else if (var16 == 0 && !stairs_same(m, x + 1, y, z, meta))
            {
                var9 = 0.5;
                var10 = 1.0;
                var13 = 1;
            }
        }
    }

    if (var13)
    {
        out[0] = var9; out[1] = var7; out[2] = var11; out[3] = var10; out[4] = var8; out[5] = var12;
    }

    return var13;
}

/** RenderBlocks.renderBlockStairs. */
int rb2_render_stairs(struct rb_mesher *m, int id, int x, int y, int z)
{
    int meta = rb_world_meta(m->w, x, y, z);
    double b[6];

    /* func_150147_e: the slab half */
    m->f152631f = 0;
    rb2_set_render_bounds(m, 0.0, (meta & 4) != 0 ? 0.5 : 0.0, 0.0,
                          1.0, (meta & 4) != 0 ? 1.0 : 0.5, 1.0);
    render_standard_block(m, id, x, y, z);

    m->f152631f = 1;
    int top = stairs_top_bounds(m, x, y, z, b);
    rb2_set_render_bounds(m, b[0], b[1], b[2], b[3], b[4], b[5]);
    render_standard_block(m, id, x, y, z);

    if (top && stairs_corner_bounds(m, x, y, z, b))
    {
        rb2_set_render_bounds(m, b[0], b[1], b[2], b[3], b[4], b[5]);
        render_standard_block(m, id, x, y, z);
    }

    m->f152631f = 0;
    return 1;
}

/* ------------------------------------------------------------------ fence */

/** BlockFence.func_149826_e: can this fence connect to the block here. */
int fence_connects(struct rb_mesher *m, int id, int x, int y, int z)
{
    int nid = rb_world_block(m->w, x, y, z);

    if (nid == id) return 1;
    if (strcmp(BLOCKS[nid].name, "minecraft:fence_gate") == 0) return 1;

    if (MATERIALS[BLOCKS[nid].material].is_opaque && BLOCKS[nid].normal_block)
        return strcmp(MATERIALS[BLOCKS[nid].material].name, "field_151572_C") != 0;

    return 0;
}

/** RenderBlocks.renderBlockFence. */
int rb2_render_fence(struct rb_mesher *m, int id, int x, int y, int z)
{
    int drawn = 0;
    float var6 = 0.375F;
    float var7 = 0.625F;
    rb2_set_render_bounds(m, var6, 0.0, var6, var7, 1.0, var7);
    render_standard_block(m, id, x, y, z);
    drawn = 1;

    int var10 = fence_connects(m, id, x - 1, y, z);
    int var11 = fence_connects(m, id, x + 1, y, z);
    int var12 = fence_connects(m, id, x, y, z - 1);
    int var13 = fence_connects(m, id, x, y, z + 1);
    int var8 = (var10 || var11) ? 1 : 0;
    int var9 = (var12 || var13) ? 1 : 0;

    if (!var8 && !var9) var8 = 1;

    var6 = 0.4375F;
    var7 = 0.5625F;
    float var14 = 0.75F;
    float var15 = 0.9375F;
    float var16 = var10 ? 0.0F : var6;
    float var17 = var11 ? 1.0F : var7;
    float var18 = var12 ? 0.0F : var6;
    float var19 = var13 ? 1.0F : var7;
    m->f152631f = 1;

    if (var8)
    {
        rb2_set_render_bounds(m, var16, var14, var6, var17, var15, var7);
        render_standard_block(m, id, x, y, z);
        drawn = 1;
    }

    if (var9)
    {
        rb2_set_render_bounds(m, var6, var14, var18, var7, var15, var19);
        render_standard_block(m, id, x, y, z);
        drawn = 1;
    }

    var14 = 0.375F;
    var15 = 0.5625F;

    if (var8)
    {
        rb2_set_render_bounds(m, var16, var14, var6, var17, var15, var7);
        render_standard_block(m, id, x, y, z);
        drawn = 1;
    }

    if (var9)
    {
        rb2_set_render_bounds(m, var6, var14, var18, var7, var15, var19);
        render_standard_block(m, id, x, y, z);
        drawn = 1;
    }

    m->f152631f = 0;
    return drawn;
}

#if defined(__NVPTX__)
/* The mesher words rb2_render_stairs, rb2_render_door and
 * rb2_render_fence leave after render_block_begin: their last part's
 * bounds, the flags back at 0 (their face draws change no other word the
 * device mesher compares). The device mesher's guess of the state after such a cell
 * (cuda/meshing meshing/kernels.c guess_as), held there against the lane before's end
 * like any guess. */
void rb2_stairs_state(struct rb_mesher *m, int id, int x, int y, int z)
{
    double b[6];
    (void)id;
    m->f152631f = 1;
    int top = stairs_top_bounds(m, x, y, z, b);
    rb2_set_render_bounds(m, b[0], b[1], b[2], b[3], b[4], b[5]);
    if (top && stairs_corner_bounds(m, x, y, z, b)) rb2_set_render_bounds(m, b[0], b[1], b[2], b[3], b[4], b[5]);
    m->f152631f = 0;
}

void rb2_door_state(struct rb_mesher *m, int id, int x, int y, int z)
{
    int meta = rb_world_meta(m->w, x, y, z);
    int above = rb_world_block(m->w, x, y + 1, z);
    int below = rb_world_block(m->w, x, y - 1, z);
    if ((meta & 8) != 0 ? below != id : above != id) return;
    double b[6];
    door_bounds(door_state(m, x, y, z), b);
    rb2_set_render_bounds(m, b[0], b[1], b[2], b[3], b[4], b[5]);
    m->flip_texture = 0;
}

void rb2_fence_state(struct rb_mesher *m, int id, int x, int y, int z)
{
    int var10 = fence_connects(m, id, x - 1, y, z);
    int var11 = fence_connects(m, id, x + 1, y, z);
    int var12 = fence_connects(m, id, x, y, z - 1);
    int var13 = fence_connects(m, id, x, y, z + 1);
    int var8 = (var10 || var11) ? 1 : 0;
    int var9 = (var12 || var13) ? 1 : 0;
    if (!var8 && !var9) var8 = 1;
    float var6 = 0.4375F, var7 = 0.5625F, var14 = 0.375F, var15 = 0.5625F;
    float var16 = var10 ? 0.0F : var6, var17 = var11 ? 1.0F : var7;
    float var18 = var12 ? 0.0F : var6, var19 = var13 ? 1.0F : var7;
    if (var9) rb2_set_render_bounds(m, var6, var14, var18, var7, var15, var19);
    else rb2_set_render_bounds(m, var16, var14, var6, var17, var15, var7);
    m->f152631f = 0;
}
#endif

/* ------------------------------------------------------------------ lever */

/** Vec3.rotateAroundX with MathHelper's table cos and sin. */
void vec_rot_x(struct rb_mesher *m, double *x, double *y, double *z, float angle)
{
    float c = mh_cos(m, angle), s = mh_sin(m, angle);
    double y2 = *y * (double)c + *z * (double)s;
    double z2 = *z * (double)c - *y * (double)s;
    (void)x;
    *y = y2;
    *z = z2;
}

/** Vec3.rotateAroundY. */
void vec_rot_y(struct rb_mesher *m, double *x, double *y, double *z, float angle)
{
    float c = mh_cos(m, angle), s = mh_sin(m, angle);
    double x2 = *x * (double)c + *z * (double)s;
    double z2 = *z * (double)c - *x * (double)s;
    (void)y;
    *x = x2;
    *z = z2;
}

/** Vec3.rotateAroundZ. */
static void vec_rot_z(struct rb_mesher *m, double *x, double *y, double *z, float angle)
{
    float c = mh_cos(m, angle), s = mh_sin(m, angle);
    double x2 = *x * (double)c + *y * (double)s;
    double y2 = *y * (double)c - *x * (double)s;
    (void)z;
    *x = x2;
    *y = y2;
}

/** RenderBlocks.renderBlockLever. */
int rb2_render_lever(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    int meta = rb_world_meta(m->w, x, y, z);
    int var6 = meta & 7;
    int var7 = (meta & 8) > 0;
    int had_override = m->override != NULL;
    const struct rb_uv *cobble = NULL;

    if (!had_override)
    {
        cobble = icon_from_side(m, 4, 0);   /* Blocks.cobblestone, side 0 */
        m->override = cobble;
    }

    float var10 = 0.25F;
    float var11 = 0.1875F;
    float var12 = 0.1875F;

    if (var6 == 5)      rb2_set_render_bounds(m, 0.5F - var11, 0.0, 0.5F - var10, 0.5F + var11, var12, 0.5F + var10);
    else if (var6 == 6) rb2_set_render_bounds(m, 0.5F - var10, 0.0, 0.5F - var11, 0.5F + var10, var12, 0.5F + var11);
    else if (var6 == 4) rb2_set_render_bounds(m, 0.5F - var11, 0.5F - var10, 1.0F - var12, 0.5F + var11, 0.5F + var10, 1.0);
    else if (var6 == 3) rb2_set_render_bounds(m, 0.5F - var11, 0.5F - var10, 0.0, 0.5F + var11, 0.5F + var10, var12);
    else if (var6 == 2) rb2_set_render_bounds(m, 1.0F - var12, 0.5F - var10, 0.5F - var11, 1.0, 0.5F + var10, 0.5F + var11);
    else if (var6 == 1) rb2_set_render_bounds(m, 0.0, 0.5F - var10, 0.5F - var11, var12, 0.5F + var10, 0.5F + var11);
    else if (var6 == 0) rb2_set_render_bounds(m, 0.5F - var10, 1.0F - var12, 0.5F - var11, 0.5F + var10, 1.0, 0.5F + var11);
    else if (var6 == 7) rb2_set_render_bounds(m, 0.5F - var11, 1.0F - var12, 0.5F - var10, 0.5F + var11, 1.0, 0.5F + var10);

    render_standard_block(m, id, x, y, z);

    if (!had_override) m->override = NULL;

    tess_set_brightness(t, block_brightness(m, id, x, y, z));
    tess_set_color_opaque_f(t, 1.0F, 1.0F, 1.0F);
    const struct rb_uv *var13 = icon_from_side(m, id, 0);

    if (m->override) var13 = m->override;

    double v[8][3];
    float var23 = 0.0625F;
    float var25 = 0.625F;
    v[0][0] = (double)(-var23); v[0][1] = 0.0; v[0][2] = (double)(-var23);
    v[1][0] = (double)var23;    v[1][1] = 0.0; v[1][2] = (double)(-var23);
    v[2][0] = (double)var23;    v[2][1] = 0.0; v[2][2] = (double)var23;
    v[3][0] = (double)(-var23); v[3][1] = 0.0; v[3][2] = (double)var23;
    v[4][0] = (double)(-var23); v[4][1] = (double)var25; v[4][2] = (double)(-var23);
    v[5][0] = (double)var23; v[5][1] = (double)var25; v[5][2] = (double)(-var23);
    v[6][0] = (double)var23; v[6][1] = (double)var25; v[6][2] = (double)var23;
    v[7][0] = (double)(-var23); v[7][1] = (double)var25; v[7][2] = (double)var23;

    float a29 = (float)3.14159265358979323846 * 2.0F / 9.0F;
    float a92 = (float)3.14159265358979323846 / 2.0F;

    for (int i = 0; i < 8; ++i)
    {
        if (var7)
        {
            v[i][2] -= 0.0625;
            vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], a29);
        }
        else
        {
            v[i][2] += 0.0625;
            vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], -a29);
        }

        if (var6 == 0 || var6 == 7) vec_rot_z(m, &v[i][0], &v[i][1], &v[i][2], (float)3.14159265358979323846);
        if (var6 == 6 || var6 == 0) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], a92);

        if (var6 > 0 && var6 < 5)
        {
            v[i][1] -= 0.375;
            vec_rot_x(m, &v[i][0], &v[i][1], &v[i][2], a92);

            if (var6 == 4) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], 0.0F);
            if (var6 == 3) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], (float)3.14159265358979323846);
            if (var6 == 2) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], a92);
            if (var6 == 1) vec_rot_y(m, &v[i][0], &v[i][1], &v[i][2], -a92);

            v[i][0] += (double)x + 0.5;
            v[i][1] += (double)((float)y + 0.5F);
            v[i][2] += (double)z + 0.5;
        }
        else if (var6 != 0 && var6 != 7)
        {
            v[i][0] += (double)x + 0.5;
            v[i][1] += (double)((float)y + 0.125F);
            v[i][2] += (double)z + 0.5;
        }
        else
        {
            v[i][0] += (double)x + 0.5;
            v[i][1] += (double)((float)y + 0.875F);
            v[i][2] += (double)z + 0.5;
        }
    }

    double u7 = (double)interp_u(var13, 7.0);
    double v6 = (double)interp_v(var13, 6.0);
    double u9 = (double)interp_u(var13, 9.0);
    double v8 = (double)interp_v(var13, 8.0);
    double vmax = (double)var13->max_v;

    static const int Q[6][4] = {
        {0, 1, 2, 3}, {7, 6, 5, 4}, {1, 0, 4, 5},
        {2, 1, 5, 6}, {3, 2, 6, 7}, {0, 3, 7, 4}
    };

    for (int q = 0; q < 6; ++q)
    {
        /* var30 == 0 picks v6..v8 for the first two quads, var30 == 2 switches
         * to max_v for the rest and stays */
        double vv1 = q <= 1 ? v8 : vmax;

        tess_add_vertex_with_uv(t, v[Q[q][0]][0], v[Q[q][0]][1], v[Q[q][0]][2], u7, vv1);
        tess_add_vertex_with_uv(t, v[Q[q][1]][0], v[Q[q][1]][1], v[Q[q][1]][2], u9, vv1);
        tess_add_vertex_with_uv(t, v[Q[q][2]][0], v[Q[q][2]][1], v[Q[q][2]][2], u9, v6);
        tess_add_vertex_with_uv(t, v[Q[q][3]][0], v[Q[q][3]][1], v[Q[q][3]][2], u7, v6);
    }

    return 1;
}

/* ----------------------------------------------------------------- cactus */

/** RenderBlocks.renderBlockCactusImpl: colorMultiplier is the default white. */
int rb2_render_cactus(struct rb_mesher *m, int id, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    float var10 = 0.5F, var11 = 1.0F, var12 = 0.8F, var13 = 0.6F;
    int var27 = block_brightness(m, id, x, y, z);

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y - 1, z, 0))
    {
        tess_set_brightness(t, m->min_y > 0.0 ? var27 : block_brightness(m, id, x, y - 1, z));
        tess_set_color_opaque_f(t, var10, var10, var10);
        render_face_y_neg(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 0));
    }

    if (m->render_all_faces || should_side_be_rendered(m, id, x, y + 1, z, 1))
    {
        tess_set_brightness(t, m->max_y < 1.0 ? var27 : block_brightness(m, id, x, y + 1, z));
        tess_set_color_opaque_f(t, var11, var11, var11);
        render_face_y_pos(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 1));
    }

    tess_set_brightness(t, var27);
    tess_set_color_opaque_f(t, var12, var12, var12);
    t->zoff += 0.0625;
    render_face_z_neg(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 2));
    t->zoff -= 0.0625;
    t->zoff -= 0.0625;
    render_face_z_pos(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 3));
    t->zoff += 0.0625;
    tess_set_color_opaque_f(t, var13, var13, var13);
    t->xoff += 0.0625;
    render_face_x_neg(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 4));
    t->xoff -= 0.0625;
    t->xoff -= 0.0625;
    render_face_x_pos(m, (double)x, (double)y, (double)z, block_icon_world(m, id, x, y, z, 5));
    t->xoff += 0.0625;
    return 1;
}