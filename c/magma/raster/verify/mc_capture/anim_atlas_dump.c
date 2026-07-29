/* Dump the compiled runtime atlas animation tiles for anim_verify.py.
 * Layout: tick-major, then six 16x16 RGBA tiles in the order below. */
#include "assets/atlas_gen.h"
#include "assets/blockmodels.h"

#include <stdio.h>
#include <stdlib.h>

static long long texture_tick(long long total_time, int portal_frame)
{
    long long rem = total_time % 32;
    int delta;
    if (rem < 0) rem += 32;
    portal_frame &= 31;
    delta = ((int)rem - portal_frame + 32) & 31;
    return total_time - delta;
}

int main(int argc, char **argv)
{
    static const int sprites[] = {
        CR_SPRITE_WATER_STILL, CR_SPRITE_WATER_FLOW,
        CR_SPRITE_LAVA_STILL, CR_SPRITE_LAVA_FLOW,
        CR_SPRITE_FIRE_LAYER_0, CR_SPRITE_FIRE_LAYER_1,
    };
    long long start;
    int count;
    FILE *out;
    if (argc != 5) {
        fprintf(stderr, "usage: %s TOTAL_TIME PORTAL_FRAME COUNT OUT\n", argv[0]);
        return 2;
    }
    start = texture_tick(strtoll(argv[1], NULL, 10), atoi(argv[2]));
    count = atoi(argv[3]);
    out = fopen(argv[4], "wb");
    if (count < 1 || !out) return 2;
    for (int tick = 0; tick < count; ++tick) {
        CrTexture atlas;
        bm_atlas_set_animation_tick(start + tick);
        atlas = bm_atlas();
        for (size_t i = 0; i < sizeof sprites / sizeof sprites[0]; ++i) {
            CrAtlasSprite s = CR_ATLAS_SPRITES[sprites[i]];
            for (int row = 0; row < CR_ATLAS_TILE; ++row) {
                const CrRgba *src = atlas.texels +
                    (s.y0 + row) * atlas.w + s.x0;
                if (fwrite(src, sizeof *src, CR_ATLAS_TILE, out) != CR_ATLAS_TILE) {
                    fclose(out);
                    return 2;
                }
            }
        }
    }
    if (fclose(out) != 0) return 2;
    printf("atlas animation: start_client_tick=%lld ticks=%d\n", start, count);
    return 0;
}
