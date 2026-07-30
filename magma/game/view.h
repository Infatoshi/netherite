/* game/view.h - the ONE MC-degrees -> magma-camera-radians conversion.
 *
 * MC convention (player physics, psv yaw/pitch): yaw 0 faces +Z, +yaw turns
 * RIGHT (west at 90), +pitch looks DOWN. Forward = (-sin yaw, cos yaw).
 * magma camera (core/math.c cr_look_yaw_pitch): yaw 0 faces -Z, +pitch UP.
 * Forward = (-sin yaw, -cos yaw).
 *
 * The pixel-verified mapping (verify/mc_capture: capture.sh /
 * game_candidate.c, gated against real MC frames at non-180 yaws) is
 *   magma_yaw = 180 - mc_yaw,  magma_pitch = -mc_pitch
 * which makes the two forward vectors IDENTICAL for every yaw:
 *   (-sin(180-m), -cos(180-m)) == (-sin m, cos m).
 * The sign matters: (mc_yaw - 180) agrees at the spawn yaw 180 but X-mirrors
 * the view everywhere else, so walking forward diverges from the look
 * direction as soon as the player turns (found by feel, 2026-07-10).
 */
#ifndef MAGMA_GAME_VIEW_H
#define MAGMA_GAME_VIEW_H

#include <math.h>

#define GM_VIEW_DEG2RAD 0.01745329251994329577f

static inline float gm_view_cam_yaw_rad(float mc_yaw_deg)
{
    return (180.0f - mc_yaw_deg) * GM_VIEW_DEG2RAD;
}

static inline float gm_view_cam_pitch_rad(float mc_pitch_deg)
{
    return -mc_pitch_deg * GM_VIEW_DEG2RAD;
}

/* EntityRenderer.hurtCameraEffect at the tape/render tick boundary
 * (partialTicks=1). The attackedAtYaw conjugation is stored separately on
 * CrCamera; this returns vanilla's eased Z rotation in degrees. */
static inline float gm_view_hurt_roll_deg(int hurt_time, int max_hurt_time)
{
    float f = (float)hurt_time - 1.0f;
    if (f < 0.0f || max_hurt_time <= 0) return 0.0f;
    f /= (float)max_hurt_time;
    f = sinf(f * f * f * f * 3.14159265358979323846f);
    return -f * 14.0f;
}

#endif /* MAGMA_GAME_VIEW_H */
