/* Held torch item-model regression.
 *
 * Build/run from magma:
 * cc -DGM_HAND_TEST -ffp-contract=off -Wall -Wextra -O2 -I. -Icore \
 *   tests/test_hand_torch.c game/hand.c game/item_render.c \
 *   assets/blockmodels.c transform.c core/math.c core/shade.c \
 *   cpu/raster_cpu.c -lm -o tests/test_hand_torch && ./tests/test_hand_torch
 */
#include "assets/blockmodels.h"
#include "assets/item_atlas.h"
#include "game/block_registry.h"
#include "game/hand.h"
#include "game/item_render.h"
#include <stdio.h>

static int g_fail;
#define TEST_MAX_VERTS 6156
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_fail = 1; } \
} while (0)

/* ItemLayerModel scans the sprite's 16x16 alpha mask and emits one side quad
 * for every opaque/transparent texel transition (including the outer edge). */
static int opaque_at(CrTexture tex, int x0, int y0, int x, int y) {
    if (x < 0 || x >= 16 || y < 0 || y >= 16) return 0;
    return tex.texels[(y0 + 15 - y) * tex.w + x0 + x].a != 0;
}

static int rim_edges(CrTexture tex, int x0, int y0) {
    int edges = 0;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        if (!opaque_at(tex, x0, y0, x, y)) continue;
        edges += !opaque_at(tex, x0, y0, x - 1, y);
        edges += !opaque_at(tex, x0, y0, x + 1, y);
        edges += !opaque_at(tex, x0, y0, x, y - 1);
        edges += !opaque_at(tex, x0, y0, x, y + 1);
    }
    return edges;
}

int main(void) {
    static CrVertex torch[TEST_MAX_VERTS], stick[TEST_MAX_VERTS], stone[36];
    int nt = gm_hand_emit_held(50, 0, 0.0f, 0.0f,
                               torch, TEST_MAX_VERTS);
    int ni = gm_hand_emit_held(280, 0, 0.0f, 0.0f,
                               stick, TEST_MAX_VERTS);
    int nb = gm_hand_emit_held(1, 0, 0.0f, 0.0f, stone, 36);

    int torch_key = gm_state_to_model_key(gm_pack_state(50, 0));
    const BmBlock *torch_model = bm_block(torch_key);
    CHECK(torch_model != NULL && torch_model->kind == BM_KIND_TORCH,
          "torch state resolves to the torch model");
    if (!torch_model) return 1;
    float tu0, tv0;
    bm_sprite_uv(torch_model->face[BM_SOUTH].sprite, &tu0, &tv0, NULL, NULL);
    CrTexture terrain = bm_atlas();
    int torch_rims = rim_edges(terrain,
                               (int)(tu0 * terrain.w + 0.5f),
                               (int)(tv0 * terrain.h + 0.5f));

    int stick_index = gm_item_sprite_index(280);
    const CrItemSprite *stick_sprite = &CR_ITEM_SPRITES[stick_index];
    CrTexture items = gm_item_atlas();
    int stick_rims = rim_edges(items, stick_sprite->x0, stick_sprite->y0);

    CHECK(torch_rims == 24, "torch sprite has 24 alpha-boundary rim edges");
    CHECK(stick_rims == 52, "stick sprite has 52 alpha-boundary rim edges");
    CHECK(nt == 12 + torch_rims * 6,
          "held torch emits two plates plus every per-texel rim quad");
    CHECK(ni == 12 + stick_rims * 6,
          "stick emits two plates plus every per-texel rim quad");
    CHECK(nb == 36, "stone reference emits a block cube");

    /* The first two quads are ItemLayerModel's full front/back plates. Their
     * positions depend only on the item/generated camera transform, so torch
     * and stick must match despite their different alpha-derived rims. */
    int same_as_item = 1, same_as_block = 1;
    for (int i = 0; i < 12; ++i) {
        const CrVec3 tp = torch[i].pos;
        const CrVec3 ip = stick[i].pos;
        const CrVec3 bp = stone[i].pos;
        if (tp.x != ip.x || tp.y != ip.y || tp.z != ip.z) same_as_item = 0;
        if (tp.x != bp.x || tp.y != bp.y || tp.z != bp.z) same_as_block = 0;
    }
    CHECK(same_as_item, "torch uses item/generated first-person geometry");
    CHECK(!same_as_block, "torch does not use block/block first-person geometry");

    if (g_fail) return 1;
    printf("test_hand_torch: PASS (torch item/generated: 2 plates + %d rim quads; stick: 2 + %d)\n",
           torch_rims, stick_rims);
    return 0;
}
