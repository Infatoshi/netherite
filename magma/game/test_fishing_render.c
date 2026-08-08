#include "game/fishing_render.h"
#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned bits(float value) {
    union { float f; unsigned u; } raw;
    raw.f = value;
    return raw.u;
}

int main(void) {
    GmRuntime *runtime = (GmRuntime *)calloc(1, sizeof *runtime);
    GmFishingLinePoint points[GM_FISHING_LINE_POINTS];
    CrVertex vertices[GM_FISHING_LINE_MAX_VERTS];
    if (!runtime) return 2;
    mc_sin_table_init(&runtime->sin_table);
    isr_init(&runtime->player.inv);
    isr_set_stack(&runtime->player.inv, 0, ic_mk(346, 1, 0));
    runtime->player.inv.current_item = 0;
    runtime->dimension = 0;
    runtime->ox = 16;
    runtime->oz = -32;
    runtime->player.ent.posX = 8.5;
    runtime->player.ent.posY = 70.0;
    runtime->player.ent.posZ = 7.25;
    runtime->player.yaw = 37.0f;
    runtime->player.pitch = -12.0f;
    runtime->fish_hook.active = 1;
    runtime->fish_hook.dimension = 0;
    runtime->fish_hook.x = 31.25;
    runtime->fish_hook.y = 65.5;
    runtime->fish_hook.z = -18.75;
    int count = gm_fishing_line_points(
        runtime, 1.0f, 0.36f, 70.0f, points);
    printf("P %d\n", count);
    for (int i = 0; i < count; ++i)
        printf("%08x %08x %08x\n",
               bits(points[i].x), bits(points[i].y), bits(points[i].z));
    int verts = gm_fishing_line_emit(
        runtime, 1.0f, 0.36f, 70.0f,
        24.5f, 71.62f, -24.75f, 70.0f, 1080,
        vertices, GM_FISHING_LINE_MAX_VERTS);
    printf("V %d\n", verts);
    free(runtime);
    return count == GM_FISHING_LINE_POINTS
        && verts == GM_FISHING_LINE_MAX_VERTS ? 0 : 1;
}
