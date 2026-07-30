/* game/hand.c - first-person HAND / HELD ITEM for magma_game.
 *
 * APPROACH (A): REAL 3D geometry through the magma pipeline, not a 2D blit.
 * We reconstruct MC 1.11.2's viewmodel path so the arm/item lands where real MC
 * draws it (lower-right, over the world, above the hotbar):
 *
 *   EntityRenderer.renderHand         -> modelview = identity (still player:
 *                                        hurtCamera/bob ~ 0), gluPerspective(fov).
 *   ItemRenderer.renderItemInFirstPerson
 *     empty main hand -> renderArmFirstPerson(equip, swing, RIGHT)
 *     non-empty       -> swing translate + transformSideFirstPerson +
 *                        transformFirstPerson + renderItemSide(FIRST_PERSON_RIGHT)
 *   RenderPlayer.renderRightArm (empty only)
 *     ModelBiped.bipedRightArm box (4x12x4 px, texU=40,texV=16) * 0.0625
 *
 * GL post-multiplies, so a vertex v maps to eye space as M*v where M is the
 * product of the translate/rotate calls IN CALL ORDER. The modelview starts at
 * identity, so "eye space" == the space cr_transform treats as world; we feed a
 * viewmodel camera at the origin (yaw=pitch=0, fov70) whose view matrix is
 * identity, then the normal perspective + viewport apply.
 *
 * DOUBLE-SIDED: MC calls GlStateManager.disableCull for the arm; magma's
 * rasterizer always backface-culls, so arm tris are emitted in BOTH windings.
 * Held items have correct front/back faces (generated plate / cube) so they
 * use single winding + CUTOUT alpha test for sprite transparency.
 *
 * DEPTH: we clear fb->depth to far before drawing (MC clears GL_DEPTH_BUFFER_BIT
 * per hand) so the viewmodel always composites on top of the world.
 *
 * Numbers below are the literal MC constants; see:
 *   client/renderer/ItemRenderer.java
 *     renderArmFirstPerson / transformSideFirstPerson / transformFirstPerson
 *     renderItemInFirstPerson (non-empty branch)
 *   client/renderer/block/model/ItemCameraTransforms + models/item/{generated,
 *     handheld}.json + models/block/block.json firstperson_righthand
 *   client/renderer/RenderItem.java renderItem (T(-0.5) before model)
 *   net.minecraftforge ItemLayerModel front/back plate z = 7.5/16 .. 8.5/16
 */
#include "game/hand.h"
#include "assets/hand_atlas.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"
#include "assets/item_atlas.h"
#include "game/block_registry.h"
#include "game/item_render.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define HAND_PI 3.14159265358979323846f
#define D2R (HAND_PI / 180.0f)

/* ---- small column-major mat4 builders (m[col*4+row], same as core/math.c) ---- */

static CrMat4 mat_translate(float x, float y, float z) {
    CrMat4 m = cr_mat4_identity();
    m.m[12] = x; m.m[13] = y; m.m[14] = z;
    return m;
}
/* GL glRotatef about a principal axis; angle in DEGREES (matches GlStateManager). */
static CrMat4 mat_rot_x(float deg) {
    float a = deg * D2R, c = cosf(a), s = sinf(a);
    CrMat4 m = cr_mat4_identity();
    m.m[5] = c;  m.m[9]  = -s;
    m.m[6] = s;  m.m[10] =  c;
    return m;
}
static CrMat4 mat_rot_y(float deg) {
    float a = deg * D2R, c = cosf(a), s = sinf(a);
    CrMat4 m = cr_mat4_identity();
    m.m[0] = c;  m.m[8]  =  s;
    m.m[2] = -s; m.m[10] =  c;
    return m;
}
static CrMat4 mat_rot_z(float deg) {
    float a = deg * D2R, c = cosf(a), s = sinf(a);
    CrMat4 m = cr_mat4_identity();
    m.m[0] = c;  m.m[4] = -s;
    m.m[1] = s;  m.m[5] =  c;
    return m;
}
static CrMat4 mat_scale(float sx, float sy, float sz) {
    CrMat4 m = cr_mat4_identity();
    m.m[0] = sx; m.m[5] = sy; m.m[10] = sz;
    return m;
}
/* post-multiply helper: M := M * R (GL's glMultMatrix order). */
static inline CrMat4 mul(CrMat4 a, CrMat4 b) { return cr_mat4_mul(a, b); }

/* ItemCameraTransforms.applyTransformSide rotation: one quat from XYZ degrees
 * equal to sequential Rx * Ry * Rz (ItemCameraTransforms.makeQuaternion /
 * TRSRTransformation.quatFromXYZ). */
static CrMat4 mat_rot_xyz(float rx, float ry, float rz) {
    CrMat4 m = cr_mat4_identity();
    if (rx != 0.0f) m = mul(m, mat_rot_x(rx));
    if (ry != 0.0f) m = mul(m, mat_rot_y(ry));
    if (rz != 0.0f) m = mul(m, mat_rot_z(rz));
    return m;
}

/* ---- ModelBiped right-arm box (default/non-slim model), MC pixel coords ----
 * addBox(-3,-2,-2, 4,12,4): x0..x1=-3..1, y0..y1=-2..10, z0..z1=-2..2.
 * 8 corners named as in ModelBox (P[i] == vertexPositions index i). */
static const float ARM_P[8][3] = {
    {-3.f, -2.f, -2.f}, /* P0 = (x,y,z)   */
    { 1.f, -2.f, -2.f}, /* P1 = (f,y,z)   */
    { 1.f, 10.f, -2.f}, /* P2 = (f,f1,z)  */
    {-3.f, 10.f, -2.f}, /* P3 = (x,f1,z)  */
    {-3.f, -2.f,  2.f}, /* P4 = (x,y,f2)  */
    { 1.f, -2.f,  2.f}, /* P5 = (f,y,f2)  */
    { 1.f, 10.f,  2.f}, /* P6 = (f,f1,f2) */
    {-3.f, 10.f,  2.f}, /* P7 = (x,f1,f2) */
};

/* ModelPlayer slim ("alex") right arm: addBox(-2,-2,-2, 3,12,4) at the same
 * tex origin (40,16); rotation point y is 2.5 instead of 2. */
static const float ARM_P_SLIM[8][3] = {
    {-2.f, -2.f, -2.f},
    { 1.f, -2.f, -2.f},
    { 1.f, 10.f, -2.f},
    {-2.f, 10.f, -2.f},
    {-2.f, -2.f,  2.f},
    { 1.f, -2.f,  2.f},
    { 1.f, 10.f,  2.f},
    {-2.f, 10.f,  2.f},
};

/* 6 quads: 4 corner indices + tex rect (u1,v1,u2,v2) in 64x64 skin px, exactly as
 * ModelBox lines 75-80 (texU=40,texV=16,dx=dz=4,dy=12). Per-vertex uv follows
 * TexturedQuad: v0->(u2,v1) v1->(u1,v1) v2->(u1,v2) v3->(u2,v2). */
typedef struct { int c[4]; float u1, v1, u2, v2; } ArmQuad;
static const ArmQuad ARM_QUADS[6] = {
    { {5,1,2,6}, 48.f, 20.f, 52.f, 32.f }, /* +X */
    { {0,4,7,3}, 40.f, 20.f, 44.f, 32.f }, /* -X */
    { {5,4,0,1}, 44.f, 16.f, 48.f, 20.f }, /* -Y (bottom of box) */
    { {2,3,7,6}, 48.f, 20.f, 52.f, 16.f }, /* +Y (top; v inverted) */
    { {1,0,3,2}, 44.f, 20.f, 48.f, 32.f }, /* -Z */
    { {4,5,6,7}, 52.f, 20.f, 56.f, 32.f }, /* +Z */
};

/* Same unwrap with dx=3 (slim). */
static const ArmQuad ARM_QUADS_SLIM[6] = {
    { {5,1,2,6}, 47.f, 20.f, 51.f, 32.f }, /* +X */
    { {0,4,7,3}, 40.f, 20.f, 44.f, 32.f }, /* -X */
    { {5,4,0,1}, 44.f, 16.f, 47.f, 20.f }, /* -Y (bottom of box) */
    { {2,3,7,6}, 47.f, 20.f, 50.f, 16.f }, /* +Y (top; v inverted) */
    { {1,0,3,2}, 44.f, 20.f, 47.f, 32.f }, /* -Z */
    { {4,5,6,7}, 51.f, 20.f, 54.f, 32.f }, /* +Z */
};

/* Offline players get steve or alex by UUID hash (DefaultPlayerSkin); the
 * tape header records which. 1 = slim/alex. */
static int g_slim = 0;

void gm_hand_set_skin(int slim) { g_slim = slim ? 1 : 0; }

#define ARM_SCALE 0.0625f          /* 1/16, ModelRenderer.render scale */

/* current attack swing progress in [0,1], pushed by gm_hand_set_swing. */
static float g_swing = 0.0f;
static float g_equip = 0.0f;
static int g_hurt_time = 0, g_max_hurt_time = 10;
static float g_hurt_yaw = 0.0f;
static int g_item_override = 0;
static int g_item_id = 0, g_item_meta = 0, g_item_count = 0;

void gm_hand_set_swing(float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    g_swing = progress;
}

void gm_hand_set_equip(float equip) {
    if (equip < 0.0f) equip = 0.0f;
    if (equip > 1.0f) equip = 1.0f;
    g_equip = equip;
}

void gm_hand_set_hurt(int hurt_time, int max_hurt_time, float attacked_yaw) {
    g_hurt_time = hurt_time;
    g_max_hurt_time = max_hurt_time;
    g_hurt_yaw = attacked_yaw;
}

static void hand_apply_hurt(CrVertex *v, int n) {
    /* Tape frames are captured at the tick boundary (partialTicks=1). */
    float f = (float)g_hurt_time - 1.0f;
    if (!v || n <= 0 || f < 0.0f || g_max_hurt_time <= 0) return;
    f /= (float)g_max_hurt_time;
    f = sinf(f * f * f * f * HAND_PI);
    CrMat4 m = cr_mat4_identity();
    m = mul(m, mat_rot_y(-g_hurt_yaw));
    m = mul(m, mat_rot_z(-f * 14.0f));
    m = mul(m, mat_rot_y(g_hurt_yaw));
    for (int i = 0; i < n; ++i) {
        CrVec4 p = {v[i].pos.x, v[i].pos.y, v[i].pos.z, 1.0f};
        p = cr_mat4_mul_vec4(m, p);
        v[i].pos = (CrVec3){p.x, p.y, p.z};
    }
}

void gm_hand_set_item_override(int item_id, int item_meta, int count) {
    g_item_override = 1;
    g_item_id = item_id;
    g_item_meta = item_meta;
    g_item_count = count;
}

/* ---- viewmodel environment (gm_hand_set_env; see hand.h) ---- */
static const CrRgba *g_lm = 0;
static float g_lm_sky = 15.0f, g_lm_blk = 0.0f;
static float g_mul_r = 1.0f, g_mul_g = 1.0f, g_mul_b = 1.0f;
static float g_fov_scale = 1.0f;
static float g_env_yaw = 0.0f, g_env_pitch = 0.0f;

void gm_hand_set_env(const CrRgba *lightmap, float sky, float blk,
                     float mul_r, float mul_g, float mul_b,
                     float fov_scale, float yaw_deg, float pitch_deg) {
    g_lm = lightmap;
    g_lm_sky = sky; g_lm_blk = blk;
    g_mul_r = mul_r; g_mul_g = mul_g; g_mul_b = mul_b;
    g_fov_scale = fov_scale > 0.01f ? fov_scale : 1.0f;
    g_env_yaw = yaw_deg; g_env_pitch = pitch_deg;
}

/* RenderHelper.enableStandardItemLighting under rotateArroundXAndY:
 * ambient 0.4 plus two 0.6-diffuse directional lights at
 * normalize(+-0.2, 1.0, -+0.7), transformed by the modelview
 * Rx(pitch)*Ry(yaw) active when the lights are set; GL clamps the summed
 * vertex light at 1. n is the face normal in eye space. */
static float hand_diffuse(CrVec3 n) {
    /* normalize(0.2, 1.0, -0.7): length sqrt(0.04+1+0.49)=sqrt(1.53) */
    const float il = 1.0f / sqrtf(1.53f);
    float l0[3] = { 0.2f * il, 1.0f * il, -0.7f * il };
    float l1[3] = { -0.2f * il, 1.0f * il, 0.7f * il };
    float yc = cosf(g_env_yaw * D2R), ys = sinf(g_env_yaw * D2R);
    float pc = cosf(g_env_pitch * D2R), ps = sinf(g_env_pitch * D2R);
    float sum = 0.4f;
    for (int i = 0; i < 2; ++i) {
        const float *L = i ? l1 : l0;
        /* Ry(yaw) then Rx(pitch), GL glRotatef conventions. */
        float x = yc * L[0] + ys * L[2];
        float z = -ys * L[0] + yc * L[2];
        float y = pc * L[1] - ps * z;
        z = ps * L[1] + pc * z;
        float d = n.x * x + n.y * y + n.z * z;
        if (d > 0.0f) sum += 0.6f * d;
    }
    return sum > 1.0f ? 1.0f : sum;
}

/* Eye-space normal of a transformed quad (enableRescaleNormal semantics:
 * renormalized after the model transform). Corner order is CCW-from-outside
 * everywhere in this file, so cross(v1-v0, v2-v1) points outward. */
static CrVec3 quad_normal(CrVec3 v0, CrVec3 v1, CrVec3 v2) {
    float ax = v1.x - v0.x, ay = v1.y - v0.y, az = v1.z - v0.z;
    float bx = v2.x - v1.x, by = v2.y - v1.y, bz = v2.z - v1.z;
    CrVec3 n = { ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx };
    float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (l > 1e-12f) { n.x /= l; n.y /= l; n.z /= l; }
    return n;
}

/* Fold the environment into a vertex: lightmap mode passes the 0..15 levels
 * (shade LUT applies the RGB); legacy mode folds the prefolded multiplier
 * into the tint. The RenderHelper diffuse always rides in vtx.ao. */
static void hand_light_vtx(CrVertex *v, float diffuse, CrRgba base_tint) {
    if (g_lm) {
        v->light = g_lm_sky;
        v->blk = g_lm_blk;
        v->tint = base_tint;
    } else {
        v->light = 1.0f;
        v->blk = 0.0f;
        v->tint.r = (unsigned char)((float)base_tint.r * g_mul_r + 0.5f);
        v->tint.g = (unsigned char)((float)base_tint.g * g_mul_g + 0.5f);
        v->tint.b = (unsigned char)((float)base_tint.b * g_mul_b + 0.5f);
        v->tint.a = base_tint.a;
    }
    v->ao = diffuse;
}

/* Build the full eye-space transform for the right arm box (empty hand).
 *
 * Call chain (MC 1.11.2):
 *   EntityRenderer.renderHand
 *     gluPerspective(getFOVModifier(pt, false)=70), modelview = identity
 *     (no orientCamera 0.05 eye-Z nudge - that is world-only)
 *     hurtCamera/applyBobbing at rest ~ identity (cameraYaw~0 when still)
 *   ItemRenderer.renderItemInFirstPerson
 *     rotateArroundXAndY pushes/pops (no net modelview change)
 *     rotateArm lag ~ 0 when look is stable
 *     equipProgress term f5 = 1 - equipped -> 0 when fully equipped
 *     renderArmFirstPerson(equip=0, swing, RIGHT)
 *   RenderPlayer.renderRightArm
 *     setRotationAngles(0,0,0,0,0,...) then rotateAngleX=0
 *     -> idle rotateAngleZ = cos(0)*0.05+0.05 = 0.1 rad remains
 *     ModelRenderer: T(rotPoint*scale) * Rz(Z) * Ry(Y) * Rx(X) * box
 *
 * bob_phase: reserved (true applyBobbing needs distanceWalkedModified +
 * cameraYaw/Pitch; at rest it is a no-op). Do NOT invent a rest offset.
 */
static CrMat4 build_arm_matrix(float swing, float bob_phase) {
    (void)bob_phase;
    CrMat4 M = cr_mat4_identity();

    /* cr_transform builds view via cr_look_yaw_pitch, which bakes the
     * EntityRenderer.orientCamera eye-Z nudge T_z(+0.05). renderHand's
     * modelview is identity WITHOUT that nudge, so cancel it here. */
    M = mul(M, mat_translate(0.0f, 0.0f, -0.05f));

    /* ---- ItemRenderer.renderArmFirstPerson(equip, swing, RIGHT), f = +1 ----
     * GlStateManager.translate(f*(f2+0.64000005F), f3 + -0.6F + equip*-0.6F, f4 + -0.71999997F)
     * equip = 1 - equippedProgress (g_equip), fully raised when 0. */
    float f1 = sqrtf(swing);
    float f2 = -0.3f * sinf(f1 * HAND_PI);
    float f3 =  0.4f * sinf(f1 * (HAND_PI * 2.0f));
    float f4 = -0.4f * sinf(swing * HAND_PI);
    const float equip = g_equip;
    M = mul(M, mat_translate(f2 + 0.64000005f,
                             f3 + -0.6f + equip * -0.6f,
                             f4 + -0.71999997f));
    M = mul(M, mat_rot_y(45.0f));
    float f5 = sinf(swing * swing * HAND_PI);
    float f6 = sinf(f1 * HAND_PI);
    M = mul(M, mat_rot_y(f6 * 70.0f));
    M = mul(M, mat_rot_z(f5 * -20.0f));
    M = mul(M, mat_translate(-1.0f, 3.6f, 3.5f));
    M = mul(M, mat_rot_z(120.0f));
    M = mul(M, mat_rot_x(200.0f));
    M = mul(M, mat_rot_y(-135.0f));
    M = mul(M, mat_translate(5.6f, 0.0f, 0.0f));

    /* ---- RenderPlayer.renderRightArm / ModelRenderer.render(0.0625) ----
     * bipedRightArm.setRotationPoint(-5, 2, 0) (slim: -5, 2.5, 0);
     * translate(rp * scale). setRotationAngles leaves rotateAngleZ = 0.1 rad
     * (ageInTicks=0 idle bob); renderRightArm only zeroes rotateAngleX. */
    M = mul(M, mat_translate(-5.0f * ARM_SCALE,
                             (g_slim ? 2.5f : 2.0f) * ARM_SCALE, 0.0f));
    const float idle_z_deg = 0.1f * (180.0f / HAND_PI); /* 0.1 rad -> deg */
    M = mul(M, mat_rot_z(idle_z_deg));
    return M;
}

/* ItemRenderer.renderItemInFirstPerson non-empty MAIN/RIGHT path (not using):
 *   translate(i*f, f1, f2) with swing terms
 *   transformSideFirstPerson(RIGHT, equip)
 *   transformFirstPerson(RIGHT, swing)
 * then RenderItem camera transform + T(-0.5) applied by caller after this. */
static CrMat4 build_held_item_base(float swing, float equip) {
    CrMat4 M = cr_mat4_identity();
    M = mul(M, mat_translate(0.0f, 0.0f, -0.05f)); /* cancel orientCamera eye-Z */

    /* swing translate (RIGHT i=+1) */
    float sq = sqrtf(swing);
    float f  = -0.4f * sinf(sq * HAND_PI);
    float f1 =  0.2f * sinf(sq * (HAND_PI * 2.0f));
    float f2 = -0.2f * sinf(swing * HAND_PI);
    M = mul(M, mat_translate(f, f1, f2));

    /* transformSideFirstPerson(RIGHT, equip): T(0.56, -0.52 + equip*-0.6, -0.72) */
    M = mul(M, mat_translate(0.56f, -0.52f + equip * -0.6f, -0.72f));

    /* transformFirstPerson(RIGHT, swing) */
    float s2 = sinf(swing * swing * HAND_PI);
    float s1 = sinf(sq * HAND_PI);
    M = mul(M, mat_rot_y(45.0f + s2 * -20.0f));
    M = mul(M, mat_rot_z(s1 * -20.0f));
    M = mul(M, mat_rot_x(s1 * -80.0f));
    M = mul(M, mat_rot_y(-45.0f));
    return M;
}

/* models/item/generated.json + handheld.json firstperson_righthand (identical):
 *   rotation [0, -90, 25], translation [1.13, 3.2, 1.13]/16, scale 0.68
 * models/block/block.json firstperson_righthand:
 *   rotation [0, 45, 0], translation 0, scale 0.40
 * Then RenderItem.renderItem: T(-0.5,-0.5,-0.5) so model is 0..1 space. */
static CrMat4 apply_fp_camera(CrMat4 M, int is_block) {
    if (is_block) {
        /* block firstperson_righthand */
        M = mul(M, mat_translate(0.0f, 0.0f, 0.0f));
        M = mul(M, mat_rot_xyz(0.0f, 45.0f, 0.0f));
        M = mul(M, mat_scale(0.40f, 0.40f, 0.40f));
    } else {
        /* item generated/handheld firstperson_righthand; translation /16 */
        M = mul(M, mat_translate(1.13f * ARM_SCALE, 3.2f * ARM_SCALE, 1.13f * ARM_SCALE));
        M = mul(M, mat_rot_xyz(0.0f, -90.0f, 25.0f));
        M = mul(M, mat_scale(0.68f, 0.68f, 0.68f));
    }
    M = mul(M, mat_translate(-0.5f, -0.5f, -0.5f));
    return M;
}

static CrVec3 xform_pt_scaled(CrMat4 m, float px, float py, float pz, float scale) {
    CrVec4 v = { px * scale, py * scale, pz * scale, 1.0f };
    CrVec4 e = cr_mat4_mul_vec4(m, v);
    CrVec3 r = { e.x, e.y, e.z };
    return r;
}

static CrVec3 xform_pt01(CrMat4 m, float px, float py, float pz) {
    CrVec4 v = { px, py, pz, 1.0f };
    CrVec4 e = cr_mat4_mul_vec4(m, v);
    CrVec3 r = { e.x, e.y, e.z };
    return r;
}

static void vtx_init(CrVertex *v, CrVec3 pos, float u, float vv, float light, CrRgba tint) {
    v->pos = pos;
    v->uv.x = u; v->uv.y = vv;
    v->light = light;
    v->ao = 1.0f;
    v->tint = tint;
    v->blk = 0.0f;
}

/* Unit-cube face template (0..1), CCW from outside, mesh_mc/item_render order. */
static const struct { float shade; int c[4][3]; } HAND_CUBE_FACES[6] = {
    { 0.5f, { {0,0,0},{1,0,0},{1,0,1},{0,0,1} } },  /* DOWN  */
    { 1.0f, { {0,1,0},{0,1,1},{1,1,1},{1,1,0} } },  /* UP    */
    { 0.8f, { {0,0,0},{0,1,0},{1,1,0},{1,0,0} } },  /* NORTH */
    { 0.8f, { {0,0,1},{1,0,1},{1,1,1},{0,1,1} } },  /* SOUTH */
    { 0.6f, { {0,0,0},{0,0,1},{0,1,1},{0,1,0} } },  /* WEST  */
    { 0.6f, { {1,0,1},{1,0,0},{1,1,0},{1,1,1} } },  /* EAST  */
};
static const float HAND_CUV[4][2] = { {0,1}, {1,1}, {1,0}, {0,0} };
static const int   HAND_TRI[6] = { 0, 1, 2, 0, 2, 3 };

/* Fixed plains-ish tint for held tinted blocks (matches item_render). */
static CrRgba hand_tint(int tint_class) {
    switch (tint_class) {
        case BM_TINT_GRASS:         return (CrRgba){145, 189,  89, 255};
        case BM_TINT_FOLIAGE:       return (CrRgba){119, 171,  47, 255};
        case BM_TINT_FOLIAGE_PINE:  return (CrRgba){ 97, 153,  97, 255};
        case BM_TINT_FOLIAGE_BIRCH: return (CrRgba){128, 167,  85, 255};
        default:                    return (CrRgba){255, 255, 255, 255};
    }
}

/* Emit a 0..1 block cube through M (36 verts). */
static int emit_held_block(CrMat4 M, const BmBlock *m, CrVertex *out, int max) {
    if (max < 36) return 0;
    int w = 0;
    for (int f = 0; f < 6; ++f) {
        float u0, v0, u1, v1;
        bm_sprite_uv(m->face[f].sprite, &u0, &v0, &u1, &v1);
        CrRgba tint = hand_tint(m->face[f].tint);
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            float lx = (float)HAND_CUBE_FACES[f].c[c][0];
            float ly = (float)HAND_CUBE_FACES[f].c[c][1];
            float lz = (float)HAND_CUBE_FACES[f].c[c][2];
            vtx_init(&quad[c], xform_pt01(M, lx, ly, lz),
                     u0 + HAND_CUV[c][0] * (u1 - u0),
                     v0 + HAND_CUV[c][1] * (v1 - v0),
                     1.0f, tint);
        }
        float d = hand_diffuse(quad_normal(quad[0].pos, quad[1].pos, quad[2].pos));
        for (int c = 0; c < 4; ++c) hand_light_vtx(&quad[c], d, tint);
        for (int k = 0; k < 6; ++k) out[w++] = quad[HAND_TRI[k]];
    }
    return w;
}

static int held_sprite_opaque(const CrTexture *tex,
                              float u0, float v0, float u1, float v1,
                              int x, int y) {
    if (x < 0 || x >= 16 || y < 0 || y >= 16) return 0;
    float u = u0 + ((float)x + 0.5f) * (u1 - u0) / 16.0f;
    /* ItemLayerModel's geometry y grows upward; PNG/atlas rows grow down. */
    float v = v0 + (15.5f - (float)y) * (v1 - v0) / 16.0f;
    int tx = (int)(u * (float)tex->w);
    int ty = (int)(v * (float)tex->h);
    if (tx < 0) tx = 0;
    if (tx >= tex->w) tx = tex->w - 1;
    if (ty < 0) ty = 0;
    if (ty >= tex->h) ty = tex->h - 1;
    return tex->texels[ty * tex->w + tx].a != 0;
}

static int emit_held_rim_quad(CrMat4 M, const float p[4][3], float u, float v,
                              int invert_normal, CrVertex *out, int max) {
    if (max < 6) return 0;
    CrRgba white = {255, 255, 255, 255};
    CrVertex q[4];
    for (int c = 0; c < 4; ++c)
        vtx_init(&q[c], xform_pt01(M, p[c][0], p[c][1], p[c][2]),
                 u, v, 1.0f, white);
    CrVec3 n = quad_normal(q[0].pos, q[1].pos, q[2].pos);
    /* Forge ItemLayerModel.buildSideQuad stores side.getOpposite() as the
     * vertex normal. Its WEST/EAST labels match the geometric side and thus
     * invert our outward normal; its UP/DOWN scan labels are already opposite
     * the geometric side, so their resulting normal stays outward. */
    if (invert_normal) {
        n.x = -n.x;
        n.y = -n.y;
        n.z = -n.z;
    }
    float d = hand_diffuse(n);
    for (int c = 0; c < 4; ++c) hand_light_vtx(&q[c], d, white);
    for (int k = 0; k < 6; ++k) out[k] = q[HAND_TRI[k]];
    return 6;
}

/* Generated/handheld item: 16x16 sprite extruded 1/16 thick (z 7.5/16..8.5/16
 * in 0..1 model space). ItemLayerModel emits front/back plates plus a rim at
 * every opaque/transparent texel transition, not four full-sprite border
 * quads. Those interior rims are the dark silhouette visible in oblique swing
 * poses. Front = NORTH (z=7.5/16), back = SOUTH (z=8.5/16). */
static int emit_held_generated(CrMat4 M, float u0, float v0, float u1, float v1,
                               const CrTexture *tex, CrVertex *out, int max) {
    if (!tex || !tex->texels || max < 12) return 0;
    int exposed = 0;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        if (!held_sprite_opaque(tex, u0, v0, u1, v1, x, y)) continue;
        exposed += !held_sprite_opaque(tex, u0, v0, u1, v1, x - 1, y);
        exposed += !held_sprite_opaque(tex, u0, v0, u1, v1, x + 1, y);
        exposed += !held_sprite_opaque(tex, u0, v0, u1, v1, x, y - 1);
        exposed += !held_sprite_opaque(tex, u0, v0, u1, v1, x, y + 1);
    }
    if (max < 12 + exposed * 6) return 0;
    const float z0 = 7.5f / 16.0f, z1 = 8.5f / 16.0f;
    CrRgba white = {255, 255, 255, 255};
    int w = 0;
    /* NORTH (front, -Z): (0,0),(0,1),(1,1),(1,0) -> normal -Z; UV ItemLayer */
    {
        CrVertex q[4];
        float xs[4] = {0,0,1,1}, ys[4] = {0,1,1,0};
        float us[4] = {u0,u0,u1,u1}, vs[4] = {v1,v0,v0,v1};
        for (int c = 0; c < 4; ++c)
            vtx_init(&q[c], xform_pt01(M, xs[c], ys[c], z0), us[c], vs[c], 1.0f, white);
        float d = hand_diffuse(quad_normal(q[0].pos, q[1].pos, q[2].pos));
        for (int c = 0; c < 4; ++c) hand_light_vtx(&q[c], d, white);
        for (int k = 0; k < 6; ++k) out[w++] = q[HAND_TRI[k]];
    }
    /* SOUTH (back, +Z): (0,0),(1,0),(1,1),(0,1) */
    {
        CrVertex q[4];
        float xs[4] = {0,1,1,0}, ys[4] = {0,0,1,1};
        float us[4] = {u0,u1,u1,u0}, vs[4] = {v1,v1,v0,v0};
        for (int c = 0; c < 4; ++c)
            vtx_init(&q[c], xform_pt01(M, xs[c], ys[c], z1), us[c], vs[c], 1.0f, white);
        float d = hand_diffuse(quad_normal(q[0].pos, q[1].pos, q[2].pos));
        for (int c = 0; c < 4; ++c) hand_light_vtx(&q[c], d, white);
        for (int k = 0; k < 6; ++k) out[w++] = q[HAND_TRI[k]];
    }
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        if (!held_sprite_opaque(tex, u0, v0, u1, v1, x, y)) continue;
        float xa = (float)x / 16.0f, xb = (float)(x + 1) / 16.0f;
        float ya = (float)y / 16.0f, yb = (float)(y + 1) / 16.0f;
        float su = u0 + ((float)x + 0.5f) * (u1 - u0) / 16.0f;
        float sv = v0 + (15.5f - (float)y) * (v1 - v0) / 16.0f;
        if (!held_sprite_opaque(tex, u0, v0, u1, v1, x - 1, y)) {
            const float p[4][3] = {{xa,ya,z0},{xa,ya,z1},{xa,yb,z1},{xa,yb,z0}};
            w += emit_held_rim_quad(M, p, su, sv, 1, out + w, max - w);
        }
        if (!held_sprite_opaque(tex, u0, v0, u1, v1, x + 1, y)) {
            const float p[4][3] = {{xb,ya,z1},{xb,ya,z0},{xb,yb,z0},{xb,yb,z1}};
            w += emit_held_rim_quad(M, p, su, sv, 1, out + w, max - w);
        }
        if (!held_sprite_opaque(tex, u0, v0, u1, v1, x, y - 1)) {
            const float p[4][3] = {{xa,ya,z0},{xb,ya,z0},{xb,ya,z1},{xa,ya,z1}};
            w += emit_held_rim_quad(M, p, su, sv, 0, out + w, max - w);
        }
        if (!held_sprite_opaque(tex, u0, v0, u1, v1, x, y + 1)) {
            const float p[4][3] = {{xa,yb,z1},{xb,yb,z1},{xb,yb,z0},{xa,yb,z0}};
            w += emit_held_rim_quad(M, p, su, sv, 0, out + w, max - w);
        }
    }
    return w;
}

/* Classify held stack: 1 = 3D block model (terrain atlas), 0 = generated item. */
static int held_is_block(int item_id, int item_meta, const BmBlock **out_m) {
    if (out_m) *out_m = 0;
    if (item_id <= 0 || item_id > 255) return 0;
    int key = gm_state_to_model_key(gm_pack_state(item_id, item_meta));
    if (key == GM_MODEL_FALLBACK || key == 0) return 0;
    const BmBlock *m = bm_block(key);
    if (!m || m->is_air) return 0;
    /* These block models have generated-item inventory parents in 1.11.2. */
    if (m->kind == BM_KIND_CROSS || m->kind == BM_KIND_TORCH) return 0;
    if (out_m) *out_m = m;
    return 1;
}

/* verts: arm 72; held block 36; generated item varies with alpha silhouette. */
#define HAND_MAX_VERTS 6156 /* front/back + four exposed sides per 16x16 texel */
static CrVertex   g_verts[HAND_MAX_VERTS];
static CrScreenTri g_tris[HAND_MAX_VERTS * 2]; /* near-clip may split; ample */

/* Emit first-person held-item verts (test hook + draw path). Returns count;
 * 0 when the stack is empty (caller should draw the bare arm instead). */
/* ticks the bow has been drawn (getItemInUseMaxCount elapsed); <=0 = idle. */
static int g_bow_pull = 0;
/* Active use action: 0 none, 1 eat/drink, 2 block. */
static int g_use_action = 0;
static int g_use_remaining = 0;
static int g_use_max = 0;

void gm_hand_set_bow_pull(int ticks) { g_bow_pull = ticks; }

void gm_hand_set_use(int action, int remaining_ticks, int max_duration) {
    g_use_action = action;
    g_use_remaining = remaining_ticks;
    g_use_max = max_duration;
}

/* ItemRenderer.renderItemInFirstPerson isHandActive BOW branch: no swing
 * translate, transformSideFirstPerson, then the draw-back chain (j=+1). */
static CrMat4 build_bow_drawn(float equip, float f5) {
    CrMat4 M = cr_mat4_identity();
    M = mul(M, mat_translate(0.0f, 0.0f, -0.05f)); /* cancel orientCamera eye-Z */
    M = mul(M, mat_translate(0.56f, -0.52f + equip * -0.6f, -0.72f));
    M = mul(M, mat_translate(-0.2785682f, 0.18344387f, 0.15731531f));
    M = mul(M, mat_rot_x(-13.935f));
    M = mul(M, mat_rot_y(35.3f));
    M = mul(M, mat_rot_z(-9.785f));
    float f6 = f5 / 20.0f;
    f6 = (f6 * f6 + f6 * 2.0f) / 3.0f;
    if (f6 > 1.0f) f6 = 1.0f;
    if (f6 > 0.1f) {
        /* draw-tremble: MathHelper.sin((f5-0.1)*1.3) * (f6-0.1) */
        float f7 = sinf((f5 - 0.1f) * 1.3f);
        float f4 = f7 * (f6 - 0.1f);
        M = mul(M, mat_translate(0.0f, f4 * 0.004f, 0.0f));
    }
    M = mul(M, mat_translate(0.0f, 0.0f, f6 * 0.04f));
    M = mul(M, mat_scale(1.0f, 1.0f, 1.0f + f6 * 0.2f));
    M = mul(M, mat_rot_y(-45.0f)); /* rotate(j*45, 0,-1,0) */
    return M;
}

/* ItemRenderer.transformEatFirstPerson + transformSideFirstPerson (RIGHT).
 * partialTicks=1 at tick-boundary frames: f = remaining - 1 + 1 = remaining. */
static CrMat4 build_eat_drink(float equip, int remaining, int max_duration) {
    CrMat4 M = cr_mat4_identity();
    M = mul(M, mat_translate(0.0f, 0.0f, -0.05f));
    float f = (float)remaining; /* remaining - pt + 1 with pt=1 */
    float f1 = max_duration > 0 ? f / (float)max_duration : 0.0f;
    if (f1 < 0.8f) {
        float f2 = fabsf(cosf(f / 4.0f * HAND_PI) * 0.1f);
        M = mul(M, mat_translate(0.0f, f2, 0.0f));
    }
    float f3 = 1.0f - (float)pow((double)f1, 27.0);
    const int i = 1; /* RIGHT */
    M = mul(M, mat_translate(f3 * 0.6f * (float)i, f3 * -0.5f, 0.0f));
    M = mul(M, mat_rot_y((float)i * f3 * 90.0f));
    M = mul(M, mat_rot_x(f3 * 10.0f));
    M = mul(M, mat_rot_z((float)i * f3 * 30.0f));
    /* transformSideFirstPerson(RIGHT, equip) */
    M = mul(M, mat_translate(0.56f, -0.52f + equip * -0.6f, -0.72f));
    return M;
}

/* BLOCK use (ItemShield EnumAction.BLOCK only in 1.11.2): transformSideFirstPerson
 * only (no swing translate / first-person). Item camera comes from
 * shield_blocking.json (not generated firstperson). */
static CrMat4 build_block_use(float equip) {
    CrMat4 M = cr_mat4_identity();
    M = mul(M, mat_translate(0.0f, 0.0f, -0.05f));
    M = mul(M, mat_translate(0.56f, -0.52f + equip * -0.6f, -0.72f));
    return M;
}

/* models/item/shield.json + shield_blocking.json firstperson_righthand.
 * ItemCameraTransforms.applyTransformSide (makeQuaternion XYZ) then
 * RenderItem.renderItem T(-0.5) then TEISR scale(1,-1,-1).
 * JSON translation values are in 1/16-block units (*0.0625). */
static CrMat4 apply_shield_fp_camera(CrMat4 M, int blocking) {
    /* idle: rot [0,180,5] trans [-10,2,-10] scale 1.25
     * blocking: rot [0,180,-5] trans [-15,5,-11] scale 1.25 */
    float tx = blocking ? -15.0f : -10.0f;
    float ty = blocking ?   5.0f :   2.0f;
    float tz = blocking ? -11.0f : -10.0f;
    float rz = blocking ?  -5.0f :   5.0f;
    M = mul(M, mat_translate(tx * ARM_SCALE, ty * ARM_SCALE, tz * ARM_SCALE));
    M = mul(M, mat_rot_xyz(0.0f, 180.0f, rz));
    M = mul(M, mat_scale(1.25f, 1.25f, 1.25f));
    M = mul(M, mat_translate(-0.5f, -0.5f, -0.5f));
    M = mul(M, mat_scale(1.0f, -1.0f, -1.0f));
    return M;
}

/* ModelBox box-net UV face (texU/texV origin, size dx,dy,dz in texels).
 * Vertex order and (u1,v1,u2,v2) match ModelBox.java lines 75-80; TexturedQuad
 * assigns v0=(u2,v1) v1=(u1,v1) v2=(u1,v2) v3=(u2,v2). Positions are pre-scale
 * pixel units; caller multiplies by 0.0625 via xform. */
typedef struct {
    int c[4];
    int u1, v1, u2, v2;
} ModelBoxFace;

/* Emit one ModelBox: 6 faces * 6 verts. Lighting is RenderHelper diffuse only
 * (entity models do NOT use block face shades 0.5/0.6/0.8/1.0). */
static int emit_model_box(CrMat4 M, int texU, int texV,
                          float x, float y, float z,
                          int dx, int dy, int dz,
                          float au0, float av0, float au1, float av1,
                          float texW, float texH,
                          CrVertex *out, int max) {
    if (max < 36) return 0;
    float f = x + (float)dx, f1 = y + (float)dy, f2 = z + (float)dz;
    /* P0..P7 as ModelBox */
    const float P[8][3] = {
        { x,  y,  z  }, { f,  y,  z  }, { f,  f1, z  }, { x,  f1, z  },
        { x,  y,  f2 }, { f,  y,  f2 }, { f,  f1, f2 }, { x,  f1, f2 },
    };
    /* Face corner indices + net UV rects (ModelBox.quadList). */
    const ModelBoxFace faces[6] = {
        { {5,1,2,6}, texU + dz + dx,       texV + dz, texU + dz + dx + dz, texV + dz + dy }, /* +X */
        { {0,4,7,3}, texU,                 texV + dz, texU + dz,           texV + dz + dy }, /* -X */
        { {5,4,0,1}, texU + dz,            texV,      texU + dz + dx,      texV + dz     }, /* -Y */
        { {2,3,7,6}, texU + dz + dx,       texV + dz, texU + dz + dx + dx, texV          }, /* +Y V-flip */
        { {1,0,3,2}, texU + dz,            texV + dz, texU + dz + dx,      texV + dz + dy }, /* -Z */
        { {4,5,6,7}, texU + dz + dx + dz,  texV + dz, texU + dz + dx + dz + dx, texV + dz + dy }, /* +Z */
    };
    float du = (au1 - au0) / texW, dv = (av1 - av0) / texH;
    CrRgba white = {255, 255, 255, 255};
    int w = 0;
    for (int fi = 0; fi < 6; ++fi) {
        const ModelBoxFace *F = &faces[fi];
        /* TexturedQuad UV for corners 0..3 */
        float uu[4] = {
            au0 + (float)F->u2 * du, au0 + (float)F->u1 * du,
            au0 + (float)F->u1 * du, au0 + (float)F->u2 * du,
        };
        float vv[4] = {
            av0 + (float)F->v1 * dv, av0 + (float)F->v1 * dv,
            av0 + (float)F->v2 * dv, av0 + (float)F->v2 * dv,
        };
        CrVertex q[4];
        for (int c = 0; c < 4; ++c) {
            const float *pp = P[F->c[c]];
            CrVec4 p4 = { pp[0] * ARM_SCALE, pp[1] * ARM_SCALE,
                          pp[2] * ARM_SCALE, 1.0f };
            CrVec4 e = cr_mat4_mul_vec4(M, p4);
            vtx_init(&q[c], (CrVec3){ e.x, e.y, e.z }, uu[c], vv[c], 1.0f, white);
        }
        float d = hand_diffuse(quad_normal(q[0].pos, q[1].pos, q[2].pos));
        for (int c = 0; c < 4; ++c) hand_light_vtx(&q[c], d, white);
        for (int k = 0; k < 6; ++k) out[w++] = q[HAND_TRI[k]];
    }
    return w;
}

/* ModelShield: plate addBox(-6,-11,-2, 12,22,1) tex(0,0); handle
 * addBox(-1,-3,-1, 2,6,6) tex(26,0); textureWidth/Height 64; *0.0625.
 * UV net from shield_base_nopattern (native 64x64 atlas rect). */
static int emit_held_shield(CrMat4 M, CrVertex *out, int max) {
    if (max < 72) return 0;
    const float aw = (float)CR_ITEM_ATLAS_W, ah = (float)CR_ITEM_ATLAS_H;
    const CrItemSprite *s = &CR_ITEM_SPRITES[gm_item_sprite_index(442)];
    float au0 = (float)s->x0 / aw, av0 = (float)s->y0 / ah;
    float au1 = (float)s->x1 / aw, av1 = (float)s->y1 / ah;
    int w = 0;
    w += emit_model_box(M, 0, 0, -6.f, -11.f, -2.f, 12, 22, 1,
                        au0, av0, au1, av1, 64.f, 64.f, out + w, max - w);
    w += emit_model_box(M, 26, 0, -1.f, -3.f, -1.f, 2, 6, 6,
                        au0, av0, au1, av1, 64.f, 64.f, out + w, max - w);
    return w;
}

int gm_hand_emit_held(int item_id, int item_meta, float swing, float equip,
                      CrVertex *out, int max) {
    if (!out || max <= 0) return 0;
    if (item_id <= 0) return 0;
    if (swing < 0.0f) swing = 0.0f;
    if (swing > 1.0f) swing = 1.0f;
    if (equip < 0.0f) equip = 0.0f;
    if (equip > 1.0f) equip = 1.0f;

    if (item_id == 261 && g_bow_pull > 0) {
        /* drawn bow: pose from the using branch, sprite from the bow.json
         * pull overrides (pulling_0 / >=0.65 _1 / >=0.9 _2); bow first-person
         * display equals the generated-item display, so apply_fp_camera(0). */
        float f5 = (float)g_bow_pull;
        CrMat4 M = apply_fp_camera(build_bow_drawn(equip, f5), 0);
        float pull = f5 / 20.0f;
        int sid = pull >= 0.9f ? 9002 : pull >= 0.65f ? 9001 : 9000;
        const float aw2 = (float)CR_ITEM_ATLAS_W, ah2 = (float)CR_ITEM_ATLAS_H;
        const CrItemSprite *sp = &CR_ITEM_SPRITES[gm_item_sprite_index(sid)];
        CrTexture tex = gm_item_atlas();
        return emit_held_generated(M,
                                   (float)sp->x0 / aw2, (float)sp->y0 / ah2,
                                   (float)sp->x1 / aw2, (float)sp->y1 / ah2,
                                   &tex, out, max);
    }

    /* Shield (442): builtin/entity ModelShield + shield.json display.
     * Blocking uses shield_blocking.json firstperson_righthand. */
    if (item_id == 442) {
        int blocking = (g_use_action == 2);
        CrMat4 base = blocking ? build_block_use(equip)
                               : build_held_item_base(swing, equip);
        CrMat4 M = apply_shield_fp_camera(base, blocking);
        return emit_held_shield(M, out, max);
    }

    const BmBlock *bm = 0;
    int is_block = held_is_block(item_id, item_meta, &bm);
    CrMat4 base;
    if (g_use_action == 1 && g_use_max > 0) {
        base = build_eat_drink(equip, g_use_remaining, g_use_max);
    } else if (g_use_action == 2) {
        base = build_block_use(equip);
    } else {
        base = build_held_item_base(swing, equip);
    }
    CrMat4 M = apply_fp_camera(base, is_block);

    if (is_block && bm) return emit_held_block(M, bm, out, max);

    /* generated / handheld item (or cross plant via terrain sprite fallback) */
    float u0, v0, u1, v1;
    if (item_id > 0 && item_id <= 255) {
        int key = gm_state_to_model_key(gm_pack_state(item_id, item_meta));
        const BmBlock *m = (key != GM_MODEL_FALLBACK && key != 0) ? bm_block(key) : 0;
        if (m && !m->is_air &&
            (m->kind == BM_KIND_CROSS || m->kind == BM_KIND_TORCH)) {
            bm_sprite_uv(m->face[BM_SOUTH].sprite, &u0, &v0, &u1, &v1);
            CrTexture tex = bm_atlas();
            return emit_held_generated(M, u0, v0, u1, v1, &tex, out, max);
        }
    }
    const float aw = (float)CR_ITEM_ATLAS_W, ah = (float)CR_ITEM_ATLAS_H;
    const CrItemSprite *s = &CR_ITEM_SPRITES[gm_item_sprite_index(item_id)];
    u0 = (float)s->x0 / aw; v0 = (float)s->y0 / ah;
    u1 = (float)s->x1 / aw; v1 = (float)s->y1 / ah;
    CrTexture tex = gm_item_atlas();
    return emit_held_generated(M, u0, v0, u1, v1, &tex, out, max);
}

static void hand_raster(CrFramebuffer *fb, CrVertex *verts, int nv,
                        const CrTexture *tex, int cutout) {
    if (nv <= 0) return;
    CrCamera cam = {0};
    cam.pos.x = cam.pos.y = cam.pos.z = 0.0f;
    cam.yaw = 0.0f; cam.pitch = 0.0f;
    /* renderHand's gluPerspective uses getFOVModifier(pt, false): base 70,
     * still squeezed 60/70 with the eye in water (never the sprint ease). */
    cam.fov_deg = 70.0f * g_fov_scale;
    cam.aspect = (float)fb->w / (float)fb->h;
    cam.znear = 0.05f;
    cam.zfar = 50.0f;
    cam.hurt_yaw_deg = 0.0f;
    cam.hurt_roll_deg = 0.0f;

    CrRgba fog = {0,0,0,0};
    CrShadeCtx sh;
    memset(&sh, 0, sizeof sh);
    sh.atlas = tex;
    sh.lightmap = g_lm;
    sh.fog_color = fog;
    sh.alpha_test = cutout ? 1 : 0;
    sh.layer = cutout ? CR_LAYER_CUTOUT : CR_LAYER_SOLID;

    int ntris = cr_transform(verts, nv, NULL, 0, &cam, fb->w, fb->h,
                             g_tris, HAND_MAX_VERTS * 2);
    if (ntris > 0) cr_raster_cpu(fb, g_tris, ntris, &sh);
}

void gm_hand_fire_overlay_draw(CrFramebuffer *fb, const CrTexture *atlas,
                               float fov_scale) {
    if (!fb || !fb->color || !fb->depth || !atlas) return;
    float u0, v0, u1, v1;
    bm_sprite_uv(CR_SPRITE_FIRE_LAYER_1, &u0, &v0, &u1, &v1);
    int nv = 0;
    static const int tri[12] = {0,1,2, 0,2,3, 0,2,1, 0,3,2};

    for (int i = 0; i < 2; ++i) {
        int side = i * 2 - 1;
        CrMat4 M = cr_mat4_identity();
        M = mul(M, mat_translate((float)-side * 0.24f, -0.3f, 0.0f));
        M = mul(M, mat_rot_y((float)side * 10.0f));
        const float p[4][5] = {
            {-0.5f, -0.5f, -0.5f, u1, v1},
            { 0.5f, -0.5f, -0.5f, u0, v1},
            { 0.5f,  0.5f, -0.5f, u0, v0},
            {-0.5f,  0.5f, -0.5f, u1, v0},
        };
        CrVertex q[4];
        for (int k = 0; k < 4; ++k) {
            q[k].pos = xform_pt01(M, p[k][0], p[k][1], p[k][2]);
            /* cr_transform's shared world camera includes orientCamera's
             * first-person +0.05 eye-space Z nudge. renderHand loads the
             * modelview identity, so cancel that nudge for this literal
             * ItemRenderer.renderFireInFirstPerson quad. */
            q[k].pos.z -= 0.05f;
            q[k].uv = (CrVec2){p[k][3], p[k][4]};
            q[k].light = q[k].ao = 1.0f;
            q[k].blk = 0.0f;
            q[k].tint = (CrRgba){255, 255, 255, 230};
        }
        for (int k = 0; k < 12; ++k) g_verts[nv++] = q[tri[k]];
    }

    /* Vanilla uses GL_ALWAYS with depthMask(false). Clearing the scratch depth
     * makes this pass unconditional; later screen overlays and HUD ignore it. */
    for (int i = 0; i < fb->w * fb->h; ++i) fb->depth[i] = 1.0f;
    CrCamera cam = {0};
    cam.fov_deg = 70.0f * (fov_scale > 0.0f ? fov_scale : 1.0f);
    cam.aspect = (float)fb->w / (float)fb->h;
    cam.znear = 0.05f;
    cam.zfar = 50.0f;
    CrShadeCtx sh = {0};
    sh.atlas = atlas;
    sh.layer = CR_LAYER_TRANSLUCENT;
    sh.blend = 1;
    int ntris = cr_transform(g_verts, nv, NULL, 0, &cam, fb->w, fb->h,
                             g_tris, HAND_MAX_VERTS * 2);
    if (ntris > 0) cr_raster_cpu(fb, g_tris, ntris, &sh);
}

void gm_hand_draw(CrFramebuffer *fb, const GmPlayerView *pv, float bob_phase) {
    if (!fb || !fb->color || !fb->depth || !pv) return;
    gm_hand_set_bow_pull(pv->bow_pull);
    /* Live active-hand use from the player view (eat/drink/block). Tests that
     * call gm_hand_emit_held still drive g_use_* via gm_hand_set_use. */
    gm_hand_set_use(pv->use_action, pv->use_remaining, pv->use_max);

    /* Clear depth to far so the viewmodel always draws over the world. */
    int n = fb->w * fb->h;
    for (int i = 0; i < n; ++i) fb->depth[i] = 1.0f;

    /* Selected hotbar stack. Empty -> bare arm (vanilla isEmpty branch). */
    int sel = pv->hotbar_sel;
    if (sel < 0) sel = 0;
    if (sel > 8) sel = 8;
    int item_id = g_item_override ? g_item_id : pv->hotbar_ids[sel];
    int item_meta = g_item_override ? g_item_meta : 0;
    int count = g_item_override ? g_item_count : pv->hotbar_counts[sel];
    int empty   = (item_id <= 0 || count <= 0);

    if (!empty) {
        int nv = gm_hand_emit_held(item_id, item_meta, g_swing, g_equip,
                                   g_verts, HAND_MAX_VERTS);
        if (nv > 0) {
            hand_apply_hurt(g_verts, nv);
            const BmBlock *bm = 0;
            int is_block = held_is_block(item_id, item_meta, &bm);
            int use_terrain = is_block;
            if (!use_terrain && item_id > 0 && item_id <= 255) {
                int key = gm_state_to_model_key(gm_pack_state(item_id, item_meta));
                const BmBlock *m = (key != GM_MODEL_FALLBACK && key != 0) ? bm_block(key) : 0;
                if (m && !m->is_air &&
                    (m->kind == BM_KIND_CROSS || m->kind == BM_KIND_TORCH))
                    use_terrain = 1;
            }
            CrTexture tex = use_terrain ? bm_atlas() : gm_item_atlas();
            hand_raster(fb, g_verts, nv, &tex, /*cutout=*/1);
            return;
        }
        /* fall through to arm if emit failed */
    }

    /* ---- bare arm (empty hand) ---- */
    CrMat4 M = build_arm_matrix(g_swing, bob_phase);

    const float iw = 1.0f / (float)HAND_SKIN_W, ih = 1.0f / (float)HAND_SKIN_H;
    int nv = 0;
    for (int q = 0; q < 6; ++q) {
        const ArmQuad *Q = g_slim ? &ARM_QUADS_SLIM[q] : &ARM_QUADS[q];
        CrVertex corner[4];
        float qu[4], qv[4];
        qu[0] = Q->u2; qv[0] = Q->v1;
        qu[1] = Q->u1; qv[1] = Q->v1;
        qu[2] = Q->u1; qv[2] = Q->v2;
        qu[3] = Q->u2; qv[3] = Q->v2;
        for (int k = 0; k < 4; ++k) {
            const float *P = g_slim ? ARM_P_SLIM[Q->c[k]] : ARM_P[Q->c[k]];
            CrVertex vtx;
            vtx.pos = xform_pt_scaled(M, P[0], P[1], P[2], ARM_SCALE);
            vtx.uv.x = qu[k] * iw;
            vtx.uv.y = qv[k] * ih;
            vtx.light = 1.0f;
            vtx.ao = 1.0f;
            vtx.tint.r = vtx.tint.g = vtx.tint.b = vtx.tint.a = 255;
            vtx.blk = 0.0f;
            corner[k] = vtx;
        }
        /* disableCull draws both sides with the quad's OWN normal, so the
         * same diffuse applies to the mirrored winding too. */
        {
            CrRgba white = {255, 255, 255, 255};
            float d = hand_diffuse(quad_normal(corner[0].pos, corner[1].pos,
                                               corner[2].pos));
            for (int k = 0; k < 4; ++k) hand_light_vtx(&corner[k], d, white);
        }
        /* quad -> 2 tris (0,1,2)(0,2,3), each also reversed for double-siding. */
        static const int TRI[2][3] = { {0,1,2}, {0,2,3} };
        for (int t = 0; t < 2; ++t) {
            if (nv + 6 > HAND_MAX_VERTS) break;
            g_verts[nv++] = corner[TRI[t][0]];
            g_verts[nv++] = corner[TRI[t][1]];
            g_verts[nv++] = corner[TRI[t][2]];
            g_verts[nv++] = corner[TRI[t][0]];
            g_verts[nv++] = corner[TRI[t][2]];
            g_verts[nv++] = corner[TRI[t][1]];
        }
    }

    hand_apply_hurt(g_verts, nv);

    CrTexture skin;
    skin.w = HAND_SKIN_W; skin.h = HAND_SKIN_H;
    skin.texels = (const CrRgba *)(g_slim ? HAND_SKIN_RGBA_ALEX
                                          : HAND_SKIN_RGBA_STEVE);
    skin.tile = 0; skin.mip_levels = 0;
    for (int i = 0; i < 15; ++i) { skin.mip[i] = 0; skin.mipw[i] = 0; skin.miph[i] = 0; }

    hand_raster(fb, g_verts, nv, &skin, /*cutout=*/0);
}
