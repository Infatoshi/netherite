/* Magma-side oracle for test_blocklight.c. Separate TU so world/light.h
 * u8 typedefs do not collide with blaze mc.h. */
#include "world/light.h"

#include <stdio.h>

int magma_torch_scene(int *at, int *d1, int *d2, int *d13, int *d14,
                      int *after_break, int *sky_at) {
    CrLight *L = light_create(0);
    uint16_t torch = (uint16_t)(50 << 4); /* BLK_TORCH packed vanilla */
    int wx = 8, wy = 200, wz = 8;
    if (!L) {
        fprintf(stderr, "FAIL: light_create\n");
        return 0;
    }
    light_ensure(L, 0, 0, 1);
    light_set_state(L, wx, wy, wz, torch);
    light_ensure(L, 0, 0, 1);
    *at = light_blk(L, wx, wy, wz);
    *d1 = light_blk(L, wx + 1, wy, wz);
    *d2 = light_blk(L, wx + 2, wy, wz);
    *d13 = light_blk(L, wx + 13, wy, wz);
    *d14 = light_blk(L, wx + 14, wy, wz);
    *sky_at = light_sky(L, wx, wy, wz);
    light_set_state(L, wx, wy, wz, 0);
    light_ensure(L, 0, 0, 1);
    *after_break = light_blk(L, wx, wy, wz);
    light_destroy(L);
    return 1;
}
