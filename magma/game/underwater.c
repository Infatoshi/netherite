/* game/underwater.c - eye-in-fluid render state. Contract + vanilla line
 * references in game/underwater.h. */
#include "game/underwater.h"

#define MAGMA_POTION_RENDER_FOG_ONLY
#include "game/potion_render.h"
#undef MAGMA_POTION_RENDER_FOG_ONLY
#include "game/sky.h"           /* GM_TERRAIN_FOG_FAR (pinned render distance) */
#include "assets/underwater_tex.h"
#include "world/light.h"        /* cr_k14_light_query */
#include "world/lightmap.h"
#include "mc_math.h"

#include <math.h>

/* BlockLiquid.getLiquidHeightPercent: the EMPTY fraction from the top of the
 * cell. meta >= 8 (falling) counts as a full block. */
static float liquid_height_percent(int meta) {
    if (meta >= 8) meta = 0;
    return (float)(meta + 1) / 9.0f;
}

static int is_water(int id) { return id == 8 || id == 9; }
static int is_lava(int id)  { return id == 10 || id == 11; }
static int is_stairs(int id) { return id == 53 || id == 67; }

/* ActiveRenderInfo.getBlockStateAtEntityViewpoint -> material at the eye:
 * 0 none, 1 water, 2 lava. */
static int viewpoint_fluid(const GmWorld *w, double ex, double ey, double ez) {
    int x = mc_floor(ex), y = mc_floor(ey), z = mc_floor(ez);
    int id = gm_world_block(w, x, y, z);
    if (is_water(id) || is_lava(id)) {
        float f = liquid_height_percent(gm_world_meta(w, x, y, z)) - 0.11111111f;
        float f1 = (float)(y + 1) - f;
        if (ey >= (double)f1) {
            id = gm_world_block(w, x, y + 1, z);
            y = y + 1;
        }
    }
    if (is_water(id)) return 1;
    if (is_lava(id)) return 2;
    return 0;
}

static int combined_light_at(const GmWorld *w, int x, int y, int z) {
    int sky = gm_world_sky_light(w, x, y, z);
    int blk = gm_world_block_light(w, x, y, z);
    return sky > blk ? sky : blk;
}

/* World.getLightBrightness -> getLightFromNeighbors -> getLight(pos, true).
 * Blocks with useNeighborBrightness select the maximum stored combined light
 * at up/east/west/south/north; World.java deliberately does not sample down.
 * Registry finalization enables it for zero-opacity lava and partial slabs,
 * but not opacity-3 water (MaterialLiquid.blocksLight remains true). Tapes are
 * clear weather, so skylightSubtracted is zero. */
static float light_brightness_at(const GmWorld *w, int dim,
                                 int x, int y, int z) {
    int own = combined_light_at(w, x, y, z);
    int id = gm_world_block(w, x, y, z);
    int l = cr_k14_light_query(
        is_lava(id) || is_stairs(id) || id == 65 ||
        id == 44 || id == 126 || id == 182,
        combined_light_at(w, x, y + 1, z),
        combined_light_at(w, x + 1, y, z),
        combined_light_at(w, x - 1, y, z),
        combined_light_at(w, x, y, z + 1),
        combined_light_at(w, x, y, z - 1),
        own);
    return cr_light_brightness(dim, l);
}

float gm_uw_fog_c1_step(float c1, const GmWorld *w, int dim,
                        double feet_x, double feet_y, double feet_z) {
    /* EntityRenderer.updateRenderer:
     *   f3 = world.getLightBrightness(BlockPos(entity))         (FEET pos)
     *   f4 = renderDistanceChunks / 32
     *   f2 = f3 * (1 - f4) + f4;  fogColor1 += (f2 - fogColor1) * 0.1 */
    float f3 = light_brightness_at(w, dim, mc_floor(feet_x), mc_floor(feet_y),
                                   mc_floor(feet_z));
    float f4 = (GM_TERRAIN_FOG_FAR / 16.0f) / 32.0f;   /* RD 8 -> 0.25 */
    float f2 = f3 * (1.0f - f4) + f4;
    return c1 + (f2 - c1) * 0.1f;
}

float gm_uw_fog_c1_seed(const GmWorld *w, int dim,
                        double feet_x, double feet_y, double feet_z) {
    /* steady state of the smoother: c1 == f2 */
    float f3 = light_brightness_at(w, dim, mc_floor(feet_x), mc_floor(feet_y),
                                   mc_floor(feet_z));
    float f4 = (GM_TERRAIN_FOG_FAR / 16.0f) / 32.0f;
    return f3 * (1.0f - f4) + f4;
}

/* ActiveRenderInfo.projectViewFromEntity: the viewpoint is the near-plane
 * centre, which orientCamera's translate(0,0,0.05) plus gluPerspective's
 * zNear 0.05F put 0.1 ahead of the eye along the look vector (derivation and
 * line references in underwater.h). GlStateManager.rotate feeds glRotatef, so
 * this uses real trig, not MathHelper's LUT. */
#define GM_UW_VIEWPOINT_AHEAD 0.1
static void viewpoint_offset(float yaw, float pitch, double *dx, double *dy,
                             double *dz) {
    const float d2r = 0.017453292f;
    float cp = cosf(pitch * d2r);
    *dx = (double)(-sinf(yaw * d2r) * cp) * GM_UW_VIEWPOINT_AHEAD;
    *dy = (double)(-sinf(pitch * d2r))    * GM_UW_VIEWPOINT_AHEAD;
    *dz = (double)( cosf(yaw * d2r) * cp) * GM_UW_VIEWPOINT_AHEAD;
}

void gm_uw_eval(const GmWorld *w, int dim, const GmPlayerView *pv,
                float fog_c1, float night_vision, int blindness_duration,
                double void_fog_y_factor, GmUnderwater *out) {
    double ex = (double)pv->x;
    double ey = (double)pv->y + (double)pv->eye_height;
    double ez = (double)pv->z;
    double vdx, vdy, vdz;
    viewpoint_offset(pv->yaw, pv->pitch, &vdx, &vdy, &vdz);
    out->fluid = viewpoint_fluid(w, ex + vdx, ey + vdy, ez + vdz);
    out->blindness = blindness_duration > 0;
    out->overlay = 0;
    out->fog01 = (CrVec3){0.0f, 0.0f, 0.0f};
    out->fog_rgba = (CrRgba){0, 0, 0, 255};
    out->density = 0.0f;
    out->fog_end = gm_blindness_fog_end(
        blindness_duration, GM_TERRAIN_FOG_FAR);
    out->fog_start = out->fog_end * 0.25f;
    out->fov_scale = 1.0f;
    out->brightness = 0.0f;

    /* ItemRenderer.renderOverlays is gated on player.isInsideOfMaterial(WATER)
     * alone, NOT on the viewpoint block, and isInsideOfMaterial uses the
     * entity's own eye (no near-plane look-ahead) -> ForgeHooks: the block at
     * the EYE BlockPos is water and eyes < pos.y + 1 + filled (filled =
     * getLiquidHeightPercent). The two tests disagree by up to the 0.1
     * look-ahead at a surface crossing. */
    {
        int x = mc_floor(ex), y = mc_floor(ey), z = mc_floor(ez);
        if (gm_uw_eye_inside_water(w, ex, (double)pv->y, ez,
                                   pv->eye_height)) {
            float filled = liquid_height_percent(gm_world_meta(w, x, y, z));
            out->overlay = ey < (double)(y + 1) + (double)filled;
            /* Entity.getBrightness: light brightness at the eye BlockPos. */
            if (out->overlay) out->brightness = light_brightness_at(w, dim, x, y, z);
        }
    }

    if (out->fluid == 0) return;

    /* updateFogColor fluid branches (no respiration / water breathing), then
     * f13, void/Blindness darkening, and Night Vision normalization. */
    float r, g, b;
    if (out->fluid == 1) {
        r = 0.02f; g = 0.02f; b = 0.2f;
        out->density = 0.1f;              /* setupFog water, no respiration */
        out->fov_scale = 60.0f / 70.0f;   /* getFOVModifier water */
    } else {
        r = 0.6f; g = 0.1f; b = 0.0f;
        out->density = 2.0f;              /* setupFog lava */
    }
    out->fog01 = (CrVec3){r * fog_c1, g * fog_c1, b * fog_c1};
    gm_void_blindness_rgb(
        &out->fog01.x, &out->fog01.y, &out->fog01.z,
        blindness_duration, (double)pv->y, void_fog_y_factor);
    {
        CrLightmapRgb nv = cr_night_vision_rgb(
            out->fog01.x, out->fog01.y, out->fog01.z, night_vision);
        out->fog01 = (CrVec3){nv.r, nv.g, nv.b};
    }
    float cr = out->fog01.x, cg = out->fog01.y, cb = out->fog01.z;
    cr = cr < 0.0f ? 0.0f : (cr > 1.0f ? 1.0f : cr);
    cg = cg < 0.0f ? 0.0f : (cg > 1.0f ? 1.0f : cg);
    cb = cb < 0.0f ? 0.0f : (cb > 1.0f ? 1.0f : cb);
    out->fog_rgba = (CrRgba){(u8)(cr * 255.0f + 0.5f), (u8)(cg * 255.0f + 0.5f),
                             (u8)(cb * 255.0f + 0.5f), 255};

}

void gm_uw_overlay_draw(CrFramebuffer *fb, const GmPlayerView *pv,
                        float brightness, float fov_deg) {
    /* renderWaterOverlayTexture: quad x,y in [-1,1] at view z = -0.5,
     *   pos(-1,-1) tex(4+f7, 4+f8) ... pos(1,1) tex(0+f7, 0+f8)
     * with f7 = -yaw/64, f8 = pitch/64, drawn through the hand projection
     * (gluPerspective fov_deg). Inverse-project each pixel onto the quad
     * plane: view-space (xq, yq) at |z| = 0.5 is ndc * tan(fov/2) * 0.5
     * (times aspect in x), so u = 2*(1-xq) + f7, v = 2*(1-yq) + f8.
     * misc textures bind unblurred -> GL_NEAREST, wrap REPEAT. Modulate by
     * color(brightness x3, 0.5), blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA. */
    const float d2r = 3.14159265358979323846f / 180.0f;
    float tanH = tanf(0.5f * fov_deg * d2r);
    float aspect = (float)fb->w / (float)fb->h;
    float f7 = -pv->yaw / 64.0f;
    float f8 = pv->pitch / 64.0f;
    for (int py = 0; py < fb->h; ++py) {
        float ndcy = 1.0f - 2.0f * ((float)py + 0.5f) / (float)fb->h;
        float yq = ndcy * tanH * 0.5f;
        float v = 2.0f * (1.0f - yq) + f8;
        float vv = v - floorf(v);
        int ty = (int)(vv * (float)CR_UNDERWATER_TEX_H);
        if (ty >= CR_UNDERWATER_TEX_H) ty = CR_UNDERWATER_TEX_H - 1;
        for (int px = 0; px < fb->w; ++px) {
            float ndcx = 2.0f * ((float)px + 0.5f) / (float)fb->w - 1.0f;
            float xq = ndcx * tanH * aspect * 0.5f;
            float u = 2.0f * (1.0f - xq) + f7;
            float uu = u - floorf(u);
            int tx = (int)(uu * (float)CR_UNDERWATER_TEX_W);
            if (tx >= CR_UNDERWATER_TEX_W) tx = CR_UNDERWATER_TEX_W - 1;
            const unsigned char *t =
                &CR_UNDERWATER_TEX[(ty * CR_UNDERWATER_TEX_W + tx) * 4];
            float a = ((float)t[3] / 255.0f) * 0.5f;
            CrRgba *dst = &fb->color[py * fb->w + px];
            float sr = ((float)t[0] / 255.0f) * brightness;
            float sg = ((float)t[1] / 255.0f) * brightness;
            float sb = ((float)t[2] / 255.0f) * brightness;
            float dr = (float)dst->r / 255.0f;
            float dg = (float)dst->g / 255.0f;
            float db = (float)dst->b / 255.0f;
            dr = sr * a + dr * (1.0f - a);
            dg = sg * a + dg * (1.0f - a);
            db = sb * a + db * (1.0f - a);
            dst->r = (u8)(dr * 255.0f + 0.5f);
            dst->g = (u8)(dg * 255.0f + 0.5f);
            dst->b = (u8)(db * 255.0f + 0.5f);
        }
    }
}
