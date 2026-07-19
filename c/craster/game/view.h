/* game/view.h - the ONE MC-degrees -> craster-camera-radians conversion.
 *
 * MC convention (player physics, psv yaw/pitch): yaw 0 faces +Z, +yaw turns
 * RIGHT (west at 90), +pitch looks DOWN. Forward = (-sin yaw, cos yaw).
 * craster camera (core/math.c cr_look_yaw_pitch): yaw 0 faces -Z, +pitch UP.
 * Forward = (-sin yaw, -cos yaw).
 *
 * The pixel-verified mapping (raster/verify/mc_capture: capture.sh /
 * game_candidate.c, gated against real MC frames at non-180 yaws) is
 *   craster_yaw = 180 - mc_yaw,  craster_pitch = -mc_pitch
 * which makes the two forward vectors IDENTICAL for every yaw:
 *   (-sin(180-m), -cos(180-m)) == (-sin m, cos m).
 * The sign matters: (mc_yaw - 180) agrees at the spawn yaw 180 but X-mirrors
 * the view everywhere else, so walking forward diverges from the look
 * direction as soon as the player turns (found by feel, 2026-07-10).
 */
#ifndef CRASTER_GAME_VIEW_H
#define CRASTER_GAME_VIEW_H

#define GM_VIEW_DEG2RAD 0.01745329251994329577f

static inline float gm_view_cam_yaw_rad(float mc_yaw_deg)
{
    return (180.0f - mc_yaw_deg) * GM_VIEW_DEG2RAD;
}

static inline float gm_view_cam_pitch_rad(float mc_pitch_deg)
{
    return -mc_pitch_deg * GM_VIEW_DEG2RAD;
}

#endif /* CRASTER_GAME_VIEW_H */
