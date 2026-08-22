/* World.checkLightFor decrease through water (Block.java:2412-2413 opacity 3).
 *
 * Magma's skylight spread only RAISES. Placing a 3x3x3 still-water cube used
 * to leave every cell at sky=12 (the first air/glass neighbour wins). Java
 * Chunk.generateSkylightMap (Chunk.java:238) then checkLightFor decrease
 * (World.java:3046) settles the cube centre at 9. Overlay brightness
 * (Entity.getBrightness -> World.getLightBrightness) reads that cell.
 */
#include <stdio.h>
#include "world/light.h"

static int failures;

static void check(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    } else {
        printf("ok  : %s\n", msg);
    }
}

int main(void) {
    CrLight *L = light_create(0);
    int x, y, z;
    uint16_t water = (uint16_t)(9 << 4);
    if (!L) {
        printf("FAIL: light_create\n");
        return 1;
    }
    light_ensure(L, 0, 0, 1);
    for (y = 199; y <= 201; ++y)
        for (z = 7; z <= 9; ++z)
            for (x = 7; x <= 9; ++x)
                light_set_state(L, x, y, z, water);
    light_ensure(L, 0, 0, 1);
    check(light_sky(L, 8, 201, 8) == 12, "top centre sky == 12");
    check(light_sky(L, 8, 200, 8) == 9, "mid centre sky == 9");
    check(light_sky(L, 8, 199, 8) == 10, "bot centre sky == 10");
    check(light_sky(L, 7, 200, 7) == 12, "edge sky == 12");
    for (y = 199; y <= 201; ++y)
        for (z = 7; z <= 9; ++z)
            for (x = 7; x <= 9; ++x)
                light_set_state(L, x, y, z, 0);
    light_ensure(L, 0, 0, 1);
    check(light_sky(L, 8, 200, 8) == 15, "air restored to 15");
    light_destroy(L);
    if (failures) {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
