/* Magma-side oracle for test_skylight_water.c. Separate TU so world/light.h
 * u8 typedefs do not collide with blaze mc.h. */
#include "world/light.h"

#include <stdio.h>

int magma_water_cube_skies(int *top, int *mid, int *bot, int *edge) {
    CrLight *L = light_create(0);
    int x, y, z;
    uint16_t water = (uint16_t)(9 << 4);
    if (!L) {
        fprintf(stderr, "FAIL: light_create\n");
        return 0;
    }
    light_ensure(L, 0, 0, 1);
    for (y = 199; y <= 201; ++y)
        for (z = 7; z <= 9; ++z)
            for (x = 7; x <= 9; ++x)
                light_set_state(L, x, y, z, water);
    light_ensure(L, 0, 0, 1);
    *top = light_sky(L, 8, 201, 8);
    *mid = light_sky(L, 8, 200, 8);
    *bot = light_sky(L, 8, 199, 8);
    *edge = light_sky(L, 7, 200, 7);
    light_destroy(L);
    return 1;
}
