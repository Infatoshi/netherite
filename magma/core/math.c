/* core/math.c - mat4/vec math + camera matrices for magma.
 *
 * Conventions (must match the rasterizer's backface winding during integration):
 *   - Column-major 4x4 storage: element at (row, col) lives at m[col*4 + row].
 *     This matches the GL convention and CrMat4 in core/types.h.
 *   - Right-handed clip/eye space: the camera looks down -Z in eye space, +X right,
 *     +Y up. cr_perspective is the standard GL perspective (maps eye -Z into
 *     clip, NDC z in [-1,1]).
 *   - MC camera: yaw about world +Y, pitch about world +X, applied as the camera
 *     orientation R_cam = Ry(yaw) * Rx(pitch). With yaw=0,pitch=0 the camera forward
 *     (world) is (0,0,-1). Positive pitch tilts the view UP (forward.y = +sin pitch),
 *     positive yaw turns the forward toward -X (forward.x = -sin(yaw)cos(pitch)).
 *       forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)).
 *     The view matrix is the inverse of the camera world transform, plus the
 *     EntityRenderer.orientCamera first-person eye-space Z nudge:
 *       V = T_eyeZ(0.05) * Rx(-pitch) * Ry(-yaw) * T(-pos).
 */
#include "core/types.h"
#include <math.h>

CR_HD CrMat4 cr_mat4_identity(void)
{
    CrMat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    r.m[0] = 1.0f;
    r.m[5] = 1.0f;
    r.m[10] = 1.0f;
    r.m[15] = 1.0f;
    return r;
}

/* returns a * b (column-major). result(row,col) = sum_k a(row,k) * b(k,col). */
CR_HD CrMat4 cr_mat4_mul(CrMat4 a, CrMat4 b)
{
    CrMat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = s;
        }
    }
    return r;
}

/* returns m * v. result[row] = sum_col m(row,col) * v[col]. */
CR_HD CrVec4 cr_mat4_mul_vec4(CrMat4 m, CrVec4 v)
{
    CrVec4 r;
    r.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8]  * v.z + m.m[12] * v.w;
    r.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9]  * v.z + m.m[13] * v.w;
    r.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    r.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return r;
}

/* Standard GL right-handed perspective. Vertical fov in degrees. NDC z in [-1,1]. */
CR_HD CrMat4 cr_perspective(float fov_deg, float aspect, float znear, float zfar)
{
    CrMat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    const float pi = 3.14159265358979323846f;
    float f = 1.0f / tanf((fov_deg * (pi / 180.0f)) * 0.5f);
    r.m[0]  = f / aspect;              /* (0,0) */
    r.m[5]  = f;                       /* (1,1) */
    r.m[10] = (zfar + znear) / (znear - zfar);      /* (2,2) */
    r.m[11] = -1.0f;                                /* (3,2) */
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar); /* (2,3) */
    return r;
}

/* View matrix V = Rx(-pitch) * Ry(-yaw) * T(-pos). See file header for handedness. */
CR_HD CrMat4 cr_look_yaw_pitch(CrVec3 pos, float yaw, float pitch)
{
    float cy = cosf(yaw),   sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);

    /* Ry(-yaw): cos(-yaw)=cy, sin(-yaw)=-sy */
    CrMat4 ry;
    for (int i = 0; i < 16; ++i) ry.m[i] = 0.0f;
    ry.m[0]  =  cy; ry.m[8]  = -sy;   /* row0: [cy, 0, -sy, 0] */
    ry.m[5]  =  1.0f;
    ry.m[2]  =  sy; ry.m[10] =  cy;   /* row2: [sy, 0,  cy, 0] */
    ry.m[15] =  1.0f;

    /* Rx(-pitch): cos(-pitch)=cp, sin(-pitch)=-sp */
    CrMat4 rx;
    for (int i = 0; i < 16; ++i) rx.m[i] = 0.0f;
    rx.m[0]  =  1.0f;
    rx.m[5]  =  cp; rx.m[9]  =  sp;   /* row1: [0, cp,  sp, 0] */
    rx.m[6]  = -sp; rx.m[10] =  cp;   /* row2: [0,-sp,  cp, 0] */
    rx.m[15] =  1.0f;

    /* T(-pos) */
    CrMat4 t = cr_mat4_identity();
    t.m[12] = -pos.x;
    t.m[13] = -pos.y;
    t.m[14] = -pos.z;

    /* EntityRenderer.orientCamera first-person branch (not third-person / bed):
     *   GlStateManager.translate(0.0F, 0.0F, 0.05F);
     * applied AFTER the yaw/pitch rotates in the GL stack product, i.e. on the left
     * of R*T: V = T_eyeZ(0.05) * Rx(-pitch) * Ry(-yaw) * T(-pos).
     * Without this the hard-scene leaf holes systematically phase-misalign
     * (Gcorr~0.66); with it they lock to the MC golden. */
    CrMat4 view = cr_mat4_mul(cr_mat4_mul(rx, ry), t);
    CrMat4 zoff = cr_mat4_identity();
    zoff.m[14] = 0.05f;   /* translate eye-space +Z by 0.05 */
    return cr_mat4_mul(zoff, view);
}

static CR_HD CrMat4 rotation_y_deg(float deg)
{
    const float rad = deg * 0.01745329251994329577f;
    const float c = cosf(rad), s = sinf(rad);
    CrMat4 r = cr_mat4_identity();
    r.m[0] = c; r.m[8] = s;
    r.m[2] = -s; r.m[10] = c;
    return r;
}

static CR_HD CrMat4 rotation_z_deg(float deg)
{
    const float rad = deg * 0.01745329251994329577f;
    const float c = cosf(rad), s = sinf(rad);
    CrMat4 r = cr_mat4_identity();
    r.m[0] = c; r.m[4] = -s;
    r.m[1] = s; r.m[5] = c;
    return r;
}

/* glRotate(deg, 0, 1, 1): axis is normalized to (0, 1/sqrt(2), 1/sqrt(2)).
 * EntityRenderer.java:759,761. Column-major Rodrigues. */
static CR_HD CrMat4 rotation_axis_0_1_1_deg(float deg)
{
    const float rad = deg * 0.01745329251994329577f;
    const float c = cosf(rad), s = sinf(rad);
    const float k = 0.7071067811865475244f;
    const float oc = 1.0f - c;
    const float half_oc = 0.5f * oc;
    CrMat4 r = cr_mat4_identity();
    r.m[0] = c;
    r.m[1] = k * s;
    r.m[2] = -k * s;
    r.m[4] = -k * s;
    r.m[5] = c + half_oc;
    r.m[6] = half_oc;
    r.m[8] = k * s;
    r.m[9] = half_oc;
    r.m[10] = c + half_oc;
    return r;
}

CR_HD CrMat4 cr_camera_view(const CrCamera *cam)
{
    CrMat4 view = cr_look_yaw_pitch(cam->pos, cam->yaw, cam->pitch);
    /* setupCameraTransform GL order (java:739-764): hurt, bob, portal RSR,
     * then orientCamera. C's look is orient; portal and hurt multiply on
     * the left. renderHand (java:791-804) builds a fresh matrix without
     * this RSR — callers must leave portal_time=0 on the hand camera.
     * 1.11.2 is only this RSR: f2=5/(t^2+5)-t*0.04 then squared, rotate
     * (count+pt)*20 about (0,1,1), scale(1/f2,1,1), rotate back
     * (EntityRenderer.java:746-761). It is not the later nausea form
     * (t*2, rotate t*5 about (1,0,0)/(0,1,0), or scale 1/(1+t*0.2)). */
    if (cam->portal_time > 0.0f) {
        float f = cam->portal_time;
        float f2 = 5.0f / (f * f + 5.0f) - f * 0.04f; /* java:757 */
        f2 = f2 * f2;                                 /* java:758 */
        CrMat4 rp = rotation_axis_0_1_1_deg(cam->portal_spin_deg);
        CrMat4 rm = rotation_axis_0_1_1_deg(-cam->portal_spin_deg);
        CrMat4 sc = cr_mat4_identity();
        sc.m[0] = 1.0f / f2; /* java:760 scale(1/f2, 1, 1) */
        /* R(+spin) * S * R(-spin) * Orient */
        view = cr_mat4_mul(rp, cr_mat4_mul(sc, cr_mat4_mul(rm, view)));
    }
    if (cam->hurt_roll_deg == 0.0f) return view;

    /* EntityRenderer.hurtCameraEffect(partialTicks), in GL call order:
     * rotate(-attackedAtYaw,Y), rotate(-f*14,Z), rotate(attackedAtYaw,Y).
     * hurt_roll_deg is the already-eased middle rotation. */
    CrMat4 hurt = rotation_y_deg(-cam->hurt_yaw_deg);
    hurt = cr_mat4_mul(hurt, rotation_z_deg(cam->hurt_roll_deg));
    hurt = cr_mat4_mul(hurt, rotation_y_deg(cam->hurt_yaw_deg));
    return cr_mat4_mul(hurt, view);
}
