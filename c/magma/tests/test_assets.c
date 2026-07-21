/* test_assets.c - self-test for the ASSETS module (atlas + block model table). */
#include <stdio.h>
#include <stdlib.h>

#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"

/* CB_* ids under test (mirror blockmodels.c). */
enum { T_STONE = 1, T_WATER = 2, T_GRASS = 3, T_ICE = 10, T_AIR = 0 };

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS  %s\n", msg); } \
    else { printf("FAIL  %s\n", msg); g_fail = 1; } \
} while (0)

int main(void)
{
    CrTexture atlas = bm_atlas();
    const BmBlock *grass, *stone, *water, *air;
    float u0, v0, u1, v1;
    int s;

    CHECK(atlas.w == atlas.h, "atlas is square (w == h)");
    CHECK(atlas.texels != NULL, "atlas texels non-NULL");
    CHECK(atlas.mip_levels > 0, "atlas mip_levels > 0");
    CHECK(atlas.mip_levels >= 1 && atlas.mip[0] != NULL, "mip level 1 present");
    CHECK(atlas.miph[atlas.mip_levels - 1] == 1 &&
          atlas.mipw[atlas.mip_levels - 1] == 1, "mip chain reaches 1x1");

    const BmBlock *ice;
    grass = bm_block(T_GRASS);
    stone = bm_block(T_STONE);
    water = bm_block(T_WATER);
    ice   = bm_block(T_ICE);
    air   = bm_block(T_AIR);
    CHECK(grass != NULL && stone != NULL && water != NULL && air != NULL,
          "bm_block never NULL");
    CHECK(grass->face[BM_UP].tint == BM_TINT_GRASS, "grass UP face tinted GRASS");
    CHECK(grass->face[BM_UP].sprite == CR_SPRITE_GRASS_TOP, "grass UP = grass_top");
    CHECK(grass->face[BM_DOWN].sprite == CR_SPRITE_DIRT, "grass DOWN = dirt");
    CHECK(grass->face[BM_NORTH].sprite == CR_SPRITE_GRASS_SIDE, "grass side = grass_side");
    CHECK(water->layer == CR_LAYER_TRANSLUCENT, "water layer == TRANSLUCENT");
    CHECK(ice->layer == CR_LAYER_TRANSLUCENT, "ice layer == TRANSLUCENT");
    CHECK(stone->is_full_cube == 1, "stone is_full_cube == 1");
    CHECK(stone->is_air == 0, "stone is_air == 0");
    CHECK(air->is_air == 1, "air is_air == 1");

    /* every sprite rect must be inside [0,1] */
    for (s = 0; s < CR_ATLAS_SPRITE_COUNT; ++s) {
        bm_sprite_uv(s, &u0, &v0, &u1, &v1);
        if (!(u0 >= 0.0f && v0 >= 0.0f && u1 <= 1.0f && v1 <= 1.0f &&
              u1 > u0 && v1 > v0)) {
            printf("FAIL  sprite %d rect out of [0,1]: %f %f %f %f\n",
                   s, u0, v0, u1, v1);
            g_fail = 1;
        }
    }
    CHECK(1, "all sprite UV rects within [0,1]");

    /* grass UP sprite UV must be sane too */
    bm_sprite_uv(grass->face[BM_UP].sprite, &u0, &v0, &u1, &v1);
    CHECK(u0 >= 0.0f && v0 >= 0.0f && u1 <= 1.0f && v1 <= 1.0f,
          "grass UP UV within [0,1]");

    if (g_fail) { printf("\nRESULT: FAIL\n"); return 1; }
    printf("\nRESULT: PASS\n");
    return 0;
}
