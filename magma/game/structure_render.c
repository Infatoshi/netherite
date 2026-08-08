#include "game/overlay.h"
#include "game/runtime.h"

static int structure_in_range(
        const GmRuntimeStructureBlock *s,
        float eye_x, float eye_y, float eye_z) {
    double dx, dy, dz;
    if (!s || !s->active) return 0;
    dx = (double)s->wx + 0.5 - eye_x;
    dy = (double)s->wy + 0.5 - eye_y;
    dz = (double)s->wz + 0.5 - eye_z;
    return dx * dx + dy * dy + dz * dz < 4096.0;
}

size_t gm_overlay_structure_vertices_required(
        const GmRuntime *r, const CrCamera *camera) {
    size_t required = 0;
    int count;
    if (!r || !r->world || !r->tape_creative || !camera) return 0;
    count = gm_runtime_structure_block_count(r);
    for (int index = 0; index < count; ++index) {
        GmRuntimeStructureBlock s;
        if (!gm_runtime_structure_block_get(r, index, &s)
                || !structure_in_range(
                    &s, camera->pos.x, camera->pos.y, camera->pos.z)
                || s.size_x < 1 || s.size_y < 1 || s.size_z < 1)
            continue;
        if (s.mode == GM_STRUCTURE_MODE_SAVE
                || (s.mode == GM_STRUCTURE_MODE_LOAD
                    && s.show_bounding_box))
            required += 90;
        if (s.mode == GM_STRUCTURE_MODE_SAVE && s.show_air) {
            for (int rz = 0; rz < s.size_z; ++rz)
                for (int ry = 0; ry < s.size_y; ++ry)
                    for (int rx = 0; rx < s.size_x; ++rx) {
                        int block = gm_world_block(
                            r->world, s.wx + s.pos_x + rx,
                            s.wy + s.pos_y + ry,
                            s.wz + s.pos_z + rz);
                        if (block == 0 || block == 217)
                            required += 144;
                    }
        }
    }
    return required;
}

int gm_overlay_emit_structures(
        CrVertex *v, int max, const GmRuntime *r,
        const CrCamera *camera, int fb_w, int fb_h) {
    int n = 0, count;
    if (!v || max < 1 || !r || !r->world || !r->tape_creative
            || !camera || fb_w < 1 || fb_h < 1)
        return 0;
    count = gm_runtime_structure_block_count(r);
    for (int index = 0; index < count; ++index) {
        GmRuntimeStructureBlock s;
        if (!gm_runtime_structure_block_get(r, index, &s)
                || !structure_in_range(
                    &s, camera->pos.x, camera->pos.y, camera->pos.z)
                || s.size_x < 1 || s.size_y < 1 || s.size_z < 1)
            continue;
        n += gm_overlay_emit_structure_bounds(
            v + n, max - n, &s, camera, fb_w, fb_h);
        if (s.mode != GM_STRUCTURE_MODE_SAVE || !s.show_air) continue;
        for (int pass = 0; pass < 2; ++pass)
            for (int rz = 0; rz < s.size_z; ++rz)
                for (int ry = 0; ry < s.size_y; ++ry)
                    for (int rx = 0; rx < s.size_x; ++rx) {
                        int wx = s.wx + s.pos_x + rx;
                        int wy = s.wy + s.pos_y + ry;
                        int wz = s.wz + s.pos_z + rz;
                        int block = gm_world_block(r->world, wx, wy, wz);
                        n += gm_overlay_emit_structure_marker(
                            v + n, max - n, &s, wx, wy, wz, block, pass,
                            camera, fb_w, fb_h);
                    }
    }
    return n;
}
