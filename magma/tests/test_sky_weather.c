/* World.getSkyColorBody / updateFogColor weather mix and
 * EntityRenderer.updateLightmap lastLightningBolt branch. */
#include "game/sky.h"
#include "world/lightmap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails;

static uint32_t fbits(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    return u;
}

static float bitsf(uint32_t u)
{
    float x;
    memcpy(&x, &u, 4);
    return x;
}

static void expect_bits(const char *tag, float got, uint32_t want)
{
    uint32_t g = fbits(got);
    if (g != want) {
        printf("FAIL: %s got 0x%08x want 0x%08x\n", tag, g, want);
        fails++;
    }
}

int main(void)
{
    CrVec3 c;
    c.x = bitsf(0x3ef0f0f1u); /* 120/255 */
    c.y = bitsf(0x3f27a7a8u); /* 167/255 */
    c.z = bitsf(0x3f800000u); /* 1.0 */
    CrVec3 id = gm_sky_color_weather_mix(c, 0.0f, 0.0f);
    if (fbits(id.x) != fbits(c.x) || fbits(id.y) != fbits(c.y) ||
        fbits(id.z) != fbits(c.z)) {
        printf("FAIL: rain=thunder=0 must be identity\n");
        fails++;
    }
    CrVec3 fog0 = gm_fog_color_weather_mix(c, 0.0f, 0.0f);
    if (fbits(fog0.x) != fbits(c.x) || fbits(fog0.y) != fbits(c.y) ||
        fbits(fog0.z) != fbits(c.z)) {
        printf("FAIL: fog rain=thunder=0 must be identity\n");
        fails++;
    }

    /* World.java:1609-1629 with rain=1 thunder=1 on plains noon RGB.
     * Bits are the gcc -ffp-contract=off evaluation of the Java float ops. */
    CrVec3 sky = gm_sky_color_weather_mix(c, 1.0f, 1.0f);
    expect_bits("sky.r", sky.x, 0x3e2c1df0u);
    expect_bits("sky.g", sky.y, 0x3e37e9bcu);
    expect_bits("sky.b", sky.z, 0x3e4dffd2u);

    CrVec3 fog_in;
    fog_in.x = 0.7529412f;
    fog_in.y = 0.84705883f;
    fog_in.z = 1.0f;
    CrVec3 fog = gm_fog_color_weather_mix(fog_in, 1.0f, 1.0f);
    expect_bits("fog.r", fog.x, 0x3e40c0c1u);
    expect_bits("fog.g", fog.y, 0x3e58d8d9u);
    expect_bits("fog.b", fog.z, 0x3e99999au);

    /* EntityRenderer.java:900-903: lastLightningBolt>0 unscales sky * f1. */
    CrLightmapRgb a = cr_lightmap_rgb(0, 15, 0, 0.578125f, 0.0f, 0.0f);
    CrLightmapRgb b = cr_lightmap_rgb_lightning(0, 15, 0, 0.578125f,
                                                0.0f, 0.0f, 2);
    CrLightmapRgb a0 = cr_lightmap_rgb_lightning(0, 15, 0, 0.578125f,
                                                 0.0f, 0.0f, 0);
    if (fbits(a.r) != fbits(a0.r) || fbits(a.g) != fbits(a0.g) ||
        fbits(a.b) != fbits(a0.b)) {
        printf("FAIL: lightning=0 must match cr_lightmap_rgb\n");
        fails++;
    }
    if (!(b.r > a.r && b.g > a.g && b.b > a.b)) {
        printf("FAIL: lightning must boost sky-15 texel (%f vs %f)\n",
               b.r, a.r);
        fails++;
    }
    CrLightmapRgb z0 = cr_lightmap_rgb(0, 0, 0, 0.578125f, 0.0f, 0.0f);
    CrLightmapRgb z1 = cr_lightmap_rgb_lightning(0, 0, 0, 0.578125f,
                                                 0.0f, 0.0f, 2);
    if (fbits(z0.r) != fbits(z1.r) || fbits(z0.g) != fbits(z1.g) ||
        fbits(z0.b) != fbits(z1.b)) {
        printf("FAIL: lightning must not change sky-0 texel\n");
        fails++;
    }

    printf(fails ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", fails);
    return fails ? 1 : 0;
}
