/* EntityRenderer.renderRainSnow and ItemRenderer.renderOverlays, ported
 * statement by statement. See raster_worldfx.h for the contract.
 *
 * Java computes the weather in double and rounds to float only where the
 * Tessellator stores a vertex; the per-column Random is java.util.Random
 * (det_rng carries the nextGaussian cache plain Random has), and each column
 * re-seeds it, so no stream state leaves this file. -ffp-contract=off keeps C
 * from fusing the arithmetic Java rounds twice. */
#define _POSIX_C_SOURCE 200809L

#include "raster_worldfx.h"

#include "raster_entity_quad.h"
#include "biomes.h"
#include "blocks.h"
#include "det.h"
#include "render_blocks.h"
#include "render_blocks_int.h"
#include "renderstate.h"

#include <math.h>
#include <stdint.h>

/* One vertex of a quad drawn through the shared entity rasterizer: clip space
 * from the projection and the modelview, the fog coordinate llvmpipe reads
 * (GL_NV_fog_distance is exposed, so setupFog selects the eye radial distance),
 * and the lightmap pair in the mesh's own packed convention. */
static struct entity_clip_vertex wfx_clip(const float proj[16], const float mv[16],
                                          float ex, float ey, float ez,
                                          float u, float v, float lb, float ls, float alpha)
{
    struct entity_clip_vertex t = {0};
    float eye[4];

    t.u = u;
    t.v = v;
    t.light[0] = lb;
    t.light[1] = ls;
    t.diffuse = 1.0f;
    t.alpha = alpha;

    for (int r = 0; r < 4; ++r)
        eye[r] = mv[r] * ex + mv[4 + r] * ey + mv[8 + r] * ez + mv[12 + r];

    for (int r = 0; r < 4; ++r)
        t.clip[r] = proj[r] * eye[0] + proj[4 + r] * eye[1] +
                    proj[8 + r] * eye[2] + proj[12 + r] * eye[3];

    t.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    return t;
}

/* World.getLightBrightnessForSkyBlocks(x, y, z, 0). */
static int sky_block_light(const struct worldfx_in *v, int x, int y, int z)
{
    return (rb_world_sky(v->world, x, y, z) << 20) |
           (rb_world_blocklight(v->world, x, y, z) << 4);
}

/* One rain column: the Tessellator quads of renderRainSnow's first branch. */
static void rain_column(const struct worldfx_in *v, const struct entity_raster_target *t,
                        int x, int z, int y0, int y1, int ytop, float ux, float uz,
                        float strength, float var32, int radius)
{
    /* the falloff is measured from the entity's own position (posX, posZ),
     * not the interpolated one the quads are translated by:
     * MathHelper.sqrt_double(...) / (float)var16, a float division */
    double dx = (double)((float)x + 0.5f) - v->eye_now_x;
    double dz = (double)((float)z + 0.5f) - v->eye_now_z;
    float dist = (float)sqrt(dx * dx + dz * dz) / (float)radius;
    int bright = sky_block_light(v, x, ytop, z);
    float alpha = ((1.0f - dist * dist) * 0.5f + 0.5f) * strength;
    float lb = (float)(bright & 65535), ls = (float)(bright >> 16);
    /* The Tessellator rounds (position - translation) once, in double. */
    float xr = (float)((double)((float)x - ux) + 0.5 - v->eye_x);
    float xr2 = (float)((double)((float)x + ux) + 0.5 - v->eye_x);
    float zr = (float)((double)((float)z - uz) + 0.5 - v->eye_z);
    float zr2 = (float)((double)((float)z + uz) + 0.5 - v->eye_z);
    float ya = (float)((double)y0 - v->eye_y);
    float yb = (float)((double)y1 - v->eye_y);
    float va = (float)y0 / 4.0f + var32;
    float vb = (float)y1 / 4.0f + var32;
    struct entity_clip_vertex q[4];

    q[0] = wfx_clip(v->proj, v->mv, xr, ya, zr, 0.0f, va, lb, ls, alpha);
    q[1] = wfx_clip(v->proj, v->mv, xr2, ya, zr2, 1.0f, va, lb, ls, alpha);
    q[2] = wfx_clip(v->proj, v->mv, xr2, yb, zr2, 1.0f, vb, lb, ls, alpha);
    q[3] = wfx_clip(v->proj, v->mv, xr, yb, zr, 0.0f, vb, lb, ls, alpha);
    raster_entity_quad(t, v->rain_tex, v->rain_w, v->rain_h, q);
}

static void snow_column(const struct worldfx_in *v, const struct entity_raster_target *t,
                        int x, int z, int y0, int y1, int ytop, float ux, float uz,
                        float strength, float var32, float uoff, float voff, int radius)
{
    double dx = (double)((float)x + 0.5f) - v->eye_now_x;
    double dz = (double)((float)z + 0.5f) - v->eye_now_z;
    float dist = (float)sqrt(dx * dx + dz * dz) / (float)radius;
    int packed = (int)(((int64_t)sky_block_light(v, x, ytop, z) * 3 + 15728880) / 4);
    float alpha = ((1.0f - dist * dist) * 0.3f + 0.5f) * strength;
    float lb = (float)(packed & 65535), ls = (float)(packed >> 16);
    float xr = (float)((double)((float)x - ux) + 0.5 - v->eye_x);
    float xr2 = (float)((double)((float)x + ux) + 0.5 - v->eye_x);
    float zr = (float)((double)((float)z - uz) + 0.5 - v->eye_z);
    float zr2 = (float)((double)((float)z + uz) + 0.5 - v->eye_z);
    float ya = (float)((double)y0 - v->eye_y);
    float yb = (float)((double)y1 - v->eye_y);
    float va = (float)y0 / 4.0f + var32 + voff;
    float vb = (float)y1 / 4.0f + var32 + voff;
    struct entity_clip_vertex q[4];

    q[0] = wfx_clip(v->proj, v->mv, xr, ya, zr, 0.0f + uoff, va, lb, ls, alpha);
    q[1] = wfx_clip(v->proj, v->mv, xr2, ya, zr2, 1.0f + uoff, va, lb, ls, alpha);
    q[2] = wfx_clip(v->proj, v->mv, xr2, yb, zr2, 1.0f + uoff, vb, lb, ls, alpha);
    q[3] = wfx_clip(v->proj, v->mv, xr, yb, zr, 0.0f + uoff, vb, lb, ls, alpha);
    raster_entity_quad(t, v->snow_tex, v->snow_w, v->snow_h, q);
}

void worldfx_weather(const struct worldfx_in *v)
{
    float strength = v->prain + (v->rain - v->prain) * v->pt;

    if (!(strength > 0.0f)) return;
    if (!v->world || !v->mesher || !v->rain_tex || !v->snow_tex) return;

    /* rainXCoords/rainYCoords: built once, the row/column offsets 16 out from
     * the player. x = -dz/len, y = dx/len; len is 0 on the player's own column,
     * so that column's offsets are NaN and its quad clips away, as in vanilla. */
    static float coords[2048];
    static int ready;

    if (!ready)
    {
        for (int row = 0; row < 32; ++row)
        {
            for (int col = 0; col < 32; ++col)
            {
                float f5 = (float)(col - 16);
                float f6 = (float)(row - 16);
                float f7 = sqrtf(f5 * f5 + f6 * f6);
                coords[(row << 5 | col)] = -f6 / f7;
                coords[1024 + (row << 5 | col)] = f5 / f7;
            }
        }
        ready = 1;
    }

    struct entity_raster_target t = {0};
    t.w = v->w;
    t.h = v->h;
    t.proj = v->proj;
    t.mv = v->mv;
    t.fog = v->fog;
    t.fogs = v->fogs;
    t.foge = v->foge;
    t.fogd = v->fogd;
    t.fogm = v->fogm;
    t.lm = v->lm;
    t.depth = v->depth;
    t.rgb = v->rgb;
    t.triangles = v->triangles;
    t.samples = v->samples;
    t.blend = 1;        /* GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA, GL_DEPTH_MASK false */
    t.vertex_alpha = 1;
    t.two_sided = 1;    /* renderRainSnow disables GL_CULL_FACE */
    t.material[0] = t.material[1] = t.material[2] = 1.0f;

    int ix = (int)floor(v->eye_now_x), iy = (int)floor(v->eye_now_y),
        iz = (int)floor(v->eye_now_z);
    int eye_y_block = (int)floor(v->eye_y);
    float tick = (float)v->ruc + v->pt;
    int radius = v->fancy ? 10 : 5;   /* var16 */

    for (int z = iz - radius; z <= iz + radius; ++z)
    {
        for (int x = ix - radius; x <= ix + radius; ++x)
        {
            int idx = (z - iz + 16) * 32 + x - ix + 16;
            float ux = coords[idx] * 0.5f;
            float uz = coords[1024 + idx] * 0.5f;
            int biome = rb_world_biome(v->world, x, z);

            if (!(BIOMES[biome].lightning || BIOMES[biome].snow)) continue;

            int ph = rb_world_precipitation_height(v->world, x, z);
            int y0 = iy - radius, y1 = iy + radius;

            if (y0 < ph) y0 = ph;
            if (y1 < ph) y1 = ph;

            int ytop = ph < eye_y_block ? eye_y_block : ph;

            if (y0 == y1) continue;

            /* this.random.setSeed((x*x*3121 + x*45238971) ^ (z*z*418711 + z*13761)) */
            uint32_t h = (uint32_t)(x * x * 3121 + x * 45238971) ^
                         (uint32_t)(z * z * 418711 + z * 13761);
            det_rng rr;
            det_rng_set_seed(&rr, (int64_t)(int32_t)h);

            float temp = rb_biome_temperature(v->mesher, x, y0, z);

            if (temp >= 0.15f)   /* WorldChunkManager.getTemperatureAtHeight is the identity */
            {
                uint32_t bits = (uint32_t)(v->ruc + x * x * 3121 + x * 45238971 +
                                           z * z * 418711 + z * 13761) & 31u;
                float var32 = ((float)(int32_t)bits + v->pt) / 32.0f *
                              (3.0f + det_rng_float(&rr));
                rain_column(v, &t, x, z, y0, y1, ytop, ux, uz, strength, var32, radius);
            }
            else
            {
                float var32 = ((float)(v->ruc & 511) + v->pt) / 512.0f;
                float var46 = det_rng_float(&rr) + tick * 0.01f * (float)det_rng_gaussian(&rr);
                float var34 = det_rng_float(&rr) + tick * (float)det_rng_gaussian(&rr) * 0.001f;
                snow_column(v, &t, x, z, y0, y1, ytop, ux, uz, strength, var32, var46, var34, radius);
            }
        }
    }
}

/* ------------------------------------------------------- the screen overlays */

/* ItemRenderer.renderInsideOfBlock: the block's side-2 texture over a
 * camera-facing quad, colour 0.1/0.1/0.1 with alpha 0.5. It draws opaque:
 * renderWorld disables GL_BLEND before renderHand (EntityRenderer.java:1444),
 * the fire overlay before it disables it again, and nothing enables it, so
 * the quad covers the frame and its alpha is never used. */
static void overlay_inside(const struct worldfx_in *v, const float mvid[16],
                           const float *uv)
{
    struct entity_raster_target t = {0};
    struct entity_clip_vertex q[4];
    const float s = 1.0f, z = -0.5f;

    t.w = v->w;
    t.h = v->h;
    t.proj = v->hand_proj;
    t.mv = mvid;
    t.fog = v->fog;
    t.fogm = 0;          /* renderOverlays draws with GL_FOG whatever it was; the quad is at 0.5 units */
    t.depth = v->depth;
    t.rgb = v->rgb;
    t.triangles = v->triangles;
    t.samples = v->samples;
    t.blend = 0;
    t.alpha = 0.5f;
    t.no_light = 1;
    t.no_alpha = 1;      /* renderOverlays disables GL_ALPHA_TEST */
    t.material[0] = t.material[1] = t.material[2] = 0.10000000149011612f;

    q[0] = wfx_clip(v->hand_proj, mvid, -s, -s, z, uv[1], uv[3], 0, 0, 1.0f);
    q[1] = wfx_clip(v->hand_proj, mvid, s, -s, z, uv[0], uv[3], 0, 0, 1.0f);
    q[2] = wfx_clip(v->hand_proj, mvid, s, s, z, uv[0], uv[2], 0, 0, 1.0f);
    q[3] = wfx_clip(v->hand_proj, mvid, -s, s, z, uv[1], uv[2], 0, 0, 1.0f);
    raster_entity_quad(&t, v->atlas, v->atlas_w, v->atlas_h, q);
}

/* ItemRenderer.renderWarpedTextureOverlay. */
static void overlay_water(const struct worldfx_in *v, const float mvid[16])
{
    struct entity_raster_target t = {0};
    struct entity_clip_vertex q[4];
    float u0 = -v->yaw / 64.0f;
    float v0 = v->pitch / 64.0f;
    float c = v->brightness;

    t.w = v->w;
    t.h = v->h;
    t.proj = v->hand_proj;
    t.mv = mvid;
    t.fog = v->fog;
    t.fogm = 0;
    t.depth = v->depth;
    t.rgb = v->rgb;
    t.triangles = v->triangles;
    t.samples = v->samples;
    t.blend = 1;
    t.alpha = 0.5f;
    t.no_light = 1;
    t.no_alpha = 1;
    t.material[0] = t.material[1] = t.material[2] = c;

    q[0] = wfx_clip(v->hand_proj, mvid, -1.0f, -1.0f, -0.5f, 4.0f + u0, 4.0f + v0, 0, 0, 1.0f);
    q[1] = wfx_clip(v->hand_proj, mvid, 1.0f, -1.0f, -0.5f, 0.0f + u0, 4.0f + v0, 0, 0, 1.0f);
    q[2] = wfx_clip(v->hand_proj, mvid, 1.0f, 1.0f, -0.5f, 0.0f + u0, 0.0f + v0, 0, 0, 1.0f);
    q[3] = wfx_clip(v->hand_proj, mvid, -1.0f, 1.0f, -0.5f, 4.0f + u0, 0.0f + v0, 0, 0, 1.0f);
    raster_entity_quad(&t, v->underwater_tex, v->underwater_w, v->underwater_h, q);
}

/* ItemRenderer.renderFireInFirstPerson: two rotated quads of the block atlas'
 * fire_layer_1 sprite, colour white with alpha 0.9. */
static void overlay_fire(const struct worldfx_in *v)
{
    struct entity_raster_target t = {0};

    t.w = v->w;
    t.h = v->h;
    t.proj = v->hand_proj;
    t.fog = v->fog;
    t.fogm = 0;
    t.depth = v->depth;
    t.rgb = v->rgb;
    t.triangles = v->triangles;
    t.samples = v->samples;
    t.blend = 3;         /* the depth mask is on: the second quad hides behind the first */
    t.linear_depth = 1;
    t.alpha = 0.8999999761581421f;
    t.no_light = 1;
    t.no_alpha = 1;
    t.material[0] = t.material[1] = t.material[2] = 1.0f;

    for (int i = 0; i < 2; ++i)
    {
        float m[16];
        struct entity_clip_vertex q[4];
        const float a = -0.5f, b = 0.5f, z = -0.5f;

        rs_gl_identity(m);
        rs_gl_translate(m, (float)(-(i * 2 - 1)) * 0.23999999463558197f, -0.30000001192092896f, 0.0f);
        rs_gl_rotate(m, (float)(i * 2 - 1) * 10.0f, 0.0f, 1.0f, 0.0f);
        t.mv = m;

        q[0] = wfx_clip(v->hand_proj, m, a, a, z, v->fire_u1, v->fire_v1, 0, 0, 1.0f);
        q[1] = wfx_clip(v->hand_proj, m, b, a, z, v->fire_u0, v->fire_v1, 0, 0, 1.0f);
        q[2] = wfx_clip(v->hand_proj, m, b, b, z, v->fire_u0, v->fire_v0, 0, 0, 1.0f);
        q[3] = wfx_clip(v->hand_proj, m, a, b, z, v->fire_u1, v->fire_v0, 0, 0, 1.0f);
        raster_entity_quad(&t, v->atlas, v->atlas_w, v->atlas_h, q);
    }
}

void worldfx_screen(const struct worldfx_in *v)
{
    float mvid[16];

    rs_gl_identity(mvid);

    if (v->burning) overlay_fire(v);
    for (int i = 0; i < v->inside_count; ++i) overlay_inside(v, mvid, v->inside_uv);
    if (v->underwater && v->underwater_tex) overlay_water(v, mvid);
}

/* Entity.isEntityInsideOpaqueBlock: the eight sample points around the eye. */
int worldfx_is_inside_opaque(const struct rb_world *w, double pos_x, double pos_y,
                             double pos_z, double width, double eye_height)
{
    for (int i = 0; i < 8; ++i)
    {
        float dx = ((float)((i >> 0) % 2) - 0.5f) * (float)width * 0.8f;
        float dy = ((float)((i >> 1) % 2) - 0.5f) * 0.1f;
        float dz = ((float)((i >> 2) % 2) - 0.5f) * (float)width * 0.8f;
        int x = (int)floor(pos_x + (double)dx);
        int y = (int)floor(pos_y + eye_height + (double)dy);
        int z = (int)floor(pos_z + (double)dz);

        if (BLOCKS[rb_world_block(w, x, y, z)].normal_cube) return 1;
    }
    return 0;
}

int worldfx_inside_block(const struct rb_world *w, const struct rb_mesher *m,
                         double pos_x, double pos_y, double pos_z,
                         double width, double height, float uv[4])
{
    int bx = (int)floor(pos_x), by = (int)floor(pos_y), bz = (int)floor(pos_z);
    int id = rb_world_block(w, bx, by, bz);
    int feet_normal = BLOCKS[id].normal_cube;

    if (!feet_normal)
    {
        for (int i = 0; i < 8; ++i)
        {
            float dx = ((float)((i >> 0) % 2) - 0.5f) * (float)width * 0.9f;
            float dy = ((float)((i >> 1) % 2) - 0.5f) * (float)height * 0.2f;
            float dz = ((float)((i >> 2) % 2) - 0.5f) * (float)width * 0.9f;
            int x = (int)floor((float)bx + dx);
            int y = (int)floor((float)by + dy);
            int z = (int)floor((float)bz + dz);
            int n = rb_world_block(w, x, y, z);

            if (BLOCKS[n].normal_cube) id = n;
        }
    }

    if (BLOCKS[id].material == 0) return 0;   /* Material.air: nothing drawn */

    /* Block.getBlockTextureFromSide(2): getIcon(2, 0), through the table's icon index */
    const struct rb_uv *icon = table_icon(m, id, 0, 2);
    uv[0] = icon->min_u;
    uv[1] = icon->max_u;
    uv[2] = icon->min_v;
    uv[3] = icon->max_v;
    return feet_normal ? 2 : 1;
}