/* test_assets.c - self-test for the ASSETS module (atlas + block model table). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"
#include "assets/water_frames.h"

/* CB_* ids under test (mirror blockmodels.c). */
enum { T_STONE = 1, T_WATER = 2, T_GRASS = 3, T_ICE = 10, T_AIR = 0 };

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS  %s\n", msg); } \
    else { printf("FAIL  %s\n", msg); g_fail = 1; } \
} while (0)

static int tile_equals(CrTexture atlas, int sprite, const unsigned char *rgba)
{
    CrAtlasSprite s = CR_ATLAS_SPRITES[sprite];
    int w = s.x1 - s.x0, h = s.y1 - s.y0;
    for (int y = 0; y < h; ++y) {
        const CrRgba *got = atlas.texels + (s.y0 + y) * atlas.w + s.x0;
        if (memcmp(got, rgba + y * w * 4, w * sizeof(CrRgba)) != 0)
            return 0;
    }
    return 1;
}

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
    CHECK(CR_ATLAS_SPRITES[CR_SPRITE_WATER_FLOW].x1 -
          CR_ATLAS_SPRITES[CR_SPRITE_WATER_FLOW].x0 == 32,
          "water_flow keeps native 32px width");
    CHECK(CR_ATLAS_SPRITES[CR_SPRITE_LAVA_FLOW].y1 -
          CR_ATLAS_SPRITES[CR_SPRITE_LAVA_FLOW].y0 == 32,
          "lava_flow keeps native 32px height");
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

    /* TextureAtlasSprite.updateAnimation metadata clocks. */
    bm_atlas_set_animation_tick(0);
    atlas = bm_atlas();
    CHECK(tile_equals(atlas, CR_SPRITE_WATER_STILL,
                      CR_WATER_STILL_RGBA[0]),
          "water_still starts at physical frame 0");
    CHECK(tile_equals(atlas, CR_SPRITE_FIRE_LAYER_0,
                      CR_FIRE_LAYER_0_RGBA[16]),
          "fire_layer_0 honors custom frame sequence");
    bm_atlas_set_animation_physical_zero();
    atlas = bm_atlas();
    CHECK(tile_equals(atlas, CR_SPRITE_FIRE_LAYER_0,
                      CR_FIRE_LAYER_0_RGBA[0]),
          "pinned fire_layer_0 restores physical frame zero");
    CHECK(tile_equals(atlas, CR_SPRITE_FIRE_LAYER_1,
                      CR_FIRE_LAYER_1_RGBA[0]),
          "pinned fire_layer_1 restores physical frame zero");
    bm_atlas_set_animation_tick(1);
    atlas = bm_atlas();
    CHECK(tile_equals(atlas, CR_SPRITE_WATER_STILL,
                      CR_WATER_STILL_RGBA[0]),
          "water_still frametime 2 holds tick 1");
    CHECK(tile_equals(atlas, CR_SPRITE_WATER_FLOW,
                      CR_WATER_FLOW_RGBA[1]),
          "water_flow advances every tick");
    bm_atlas_set_animation_tick(2);
    atlas = bm_atlas();
    CHECK(tile_equals(atlas, CR_SPRITE_WATER_STILL,
                      CR_WATER_STILL_RGBA[1]),
          "water_still advances on tick 2");
    CHECK(tile_equals(atlas, CR_SPRITE_LAVA_STILL,
                      CR_LAVA_STILL_RGBA[1]),
          "lava_still frametime 2 advances");
    bm_atlas_set_animation_tick(3);
    atlas = bm_atlas();
    CHECK(tile_equals(atlas, CR_SPRITE_LAVA_FLOW,
                      CR_LAVA_FLOW_RGBA[1]),
          "lava_flow frametime 3 advances");

    if (g_fail) { printf("\nRESULT: FAIL\n"); return 1; }
    printf("\nRESULT: PASS\n");
    return 0;
}
