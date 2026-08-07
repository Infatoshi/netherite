#include "game/fishing_render.h"

#include "game/runtime.h"

#include <math.h>

static void fish_rotate_pitch(
        const McSinTable *table, double *x, double *y, double *z,
        float angle) {
    float c = mc_cos(table, angle);
    float s = mc_sin(table, angle);
    double old_y = *y;
    double old_z = *z;
    (void)x;
    *y = old_y * (double)c + old_z * (double)s;
    *z = old_z * (double)c - old_y * (double)s;
}

static void fish_rotate_yaw(
        const McSinTable *table, double *x, double *y, double *z,
        float angle) {
    float c = mc_cos(table, angle);
    float s = mc_sin(table, angle);
    double old_x = *x;
    double old_z = *z;
    (void)y;
    *x = old_x * (double)c + old_z * (double)s;
    *z = old_z * (double)c - old_x * (double)s;
}

int gm_fishing_line_points(
        const GmRuntime *runtime, float partial_ticks,
        float swing_progress, float fov_setting,
        GmFishingLinePoint out[GM_FISHING_LINE_POINTS]) {
    const GmRuntimeFishHook *hook;
    const PsvPlayer *player;
    double player_x, player_y, player_z;
    double hook_x, hook_y, hook_z;
    double vx, vy, vz;
    double hand_x, hand_y, hand_z;
    double dx, dy, dz;
    float root, swing;
    int hand;
    ICStack held;
    if (!runtime || !out || partial_ticks < 0.0f || partial_ticks > 1.0f
            || fov_setting <= 0.0f)
        return 0;
    hook = &runtime->fish_hook;
    player = &runtime->player;
    if (!hook->active || hook->dimension != runtime->dimension)
        return 0;

    /* The represented state has the current and tick-entry player positions.
     * At partial=1 this is exactly the branch used by all product captures. */
    player_x = partial_ticks < 1.0f && runtime->te_valid
        ? runtime->te_x + (player->ent.posX + runtime->ox - runtime->te_x)
            * (double)partial_ticks
        : player->ent.posX + runtime->ox;
    player_y = partial_ticks < 1.0f && runtime->te_valid
        ? runtime->te_y + (player->ent.posY - runtime->te_y)
            * (double)partial_ticks
        : player->ent.posY;
    player_z = partial_ticks < 1.0f && runtime->te_valid
        ? runtime->te_z + (player->ent.posZ + runtime->oz - runtime->te_z)
            * (double)partial_ticks
        : player->ent.posZ + runtime->oz;
    hook_x = hook->x;
    hook_y = hook->y;
    hook_z = hook->z;

    hand = 1; /* represented player primary hand is right */
    held = isr_get_stack(&player->inv, player->inv.current_item);
    if (held.item != 346 || held.count <= 0) hand = -hand;
    root = (float)sqrt((double)swing_progress);
    swing = mc_sin(&runtime->sin_table, root * (float)MC_PI);

    vx = (double)hand * -0.36 * (double)(fov_setting / 100.0f);
    vy = -0.045 * (double)(fov_setting / 100.0f);
    vz = 0.4;
    fish_rotate_pitch(&runtime->sin_table, &vx, &vy, &vz,
                      -player->pitch * 0.017453292f);
    fish_rotate_yaw(&runtime->sin_table, &vx, &vy, &vz,
                    -player->yaw * 0.017453292f);
    fish_rotate_yaw(&runtime->sin_table, &vx, &vy, &vz, swing * 0.5f);
    fish_rotate_pitch(&runtime->sin_table, &vx, &vy, &vz, -swing * 0.7f);
    hand_x = player_x + vx;
    hand_y = player_y + vy;
    hand_z = player_z + vz;

    dx = (double)(float)(hand_x - hook_x);
    dy = (double)(float)(hand_y - (hook_y + 0.25))
        + psv_player_eye_height(player);
    dz = (double)(float)(hand_z - hook_z);
    for (int i = 0; i < GM_FISHING_LINE_POINTS; ++i) {
        float t = (float)i / 16.0f;
        out[i].x = (float)(hook_x + dx * (double)t);
        out[i].y = (float)(hook_y + dy
            * (double)(t * t + t) * 0.5 + 0.25);
        out[i].z = (float)(hook_z + dz * (double)t);
    }
    return GM_FISHING_LINE_POINTS;
}

static CrVertex fish_vertex(float x, float y, float z) {
    CrVertex vertex = {0};
    vertex.pos = (CrVec3){x, y, z};
    vertex.tint = (CrRgba){0, 0, 0, 255};
    vertex.light = 15.0f;
    vertex.blk = 15.0f;
    vertex.ao = 1.0f;
    return vertex;
}

int gm_fishing_line_emit(
        const GmRuntime *runtime, float partial_ticks,
        float swing_progress, float fov_setting,
        float camera_x, float camera_y, float camera_z,
        float vertical_fov_deg, int viewport_height,
        CrVertex *out, int cap) {
    GmFishingLinePoint points[GM_FISHING_LINE_POINTS];
    int count = gm_fishing_line_points(
        runtime, partial_ticks, swing_progress, fov_setting, points);
    int n = 0;
    if (!out || count != GM_FISHING_LINE_POINTS || viewport_height <= 0
            || vertical_fov_deg <= 0.0f || cap < 12)
        return 0;
    for (int i = 0; i + 1 < count && n + 12 <= cap; ++i) {
        GmFishingLinePoint a = points[i];
        GmFishingLinePoint b = points[i + 1];
        double sx = (double)b.x - a.x;
        double sy = (double)b.y - a.y;
        double sz = (double)b.z - a.z;
        double mx = ((double)a.x + b.x) * 0.5 - camera_x;
        double my = ((double)a.y + b.y) * 0.5 - camera_y;
        double mz = ((double)a.z + b.z) * 0.5 - camera_z;
        double nx = sy * mz - sz * my;
        double ny = sz * mx - sx * mz;
        double nz = sx * my - sy * mx;
        double normal_len = sqrt(nx * nx + ny * ny + nz * nz);
        double distance = sqrt(mx * mx + my * my + mz * mz);
        double half_width = distance
            * tan((double)vertical_fov_deg * MC_PI / 360.0)
            / (double)viewport_height;
        if (normal_len <= 1.0e-12) continue;
        nx *= half_width / normal_len;
        ny *= half_width / normal_len;
        nz *= half_width / normal_len;
        CrVertex q[4] = {
            fish_vertex((float)(a.x - nx), (float)(a.y - ny), (float)(a.z - nz)),
            fish_vertex((float)(a.x + nx), (float)(a.y + ny), (float)(a.z + nz)),
            fish_vertex((float)(b.x + nx), (float)(b.y + ny), (float)(b.z + nz)),
            fish_vertex((float)(b.x - nx), (float)(b.y - ny), (float)(b.z - nz))
        };
        int order[12] = {0,1,2, 0,2,3, 2,1,0, 3,2,0};
        for (int j = 0; j < 12; ++j) out[n++] = q[order[j]];
    }
    return n;
}
