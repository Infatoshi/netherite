/* Held torch item-model regression.
 *
 * Build/run from c/magma:
 * cc -DGM_HAND_TEST -ffp-contract=off -Wall -Wextra -O2 -I. -Icore \
 *   tests/test_hand_torch.c game/hand.c game/item_render.c \
 *   assets/blockmodels.c transform.c core/math.c core/shade.c \
 *   cpu/raster_cpu.c -lm -o tests/test_hand_torch && ./tests/test_hand_torch
 */
#include "game/hand.h"
#include <stdio.h>

static int g_fail;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_fail = 1; } \
} while (0)

int main(void) {
    CrVertex torch[36], stick[36], stone[36];
    int nt = gm_hand_emit_held(50, 0, 0.0f, 0.0f, torch, 36);
    int ni = gm_hand_emit_held(280, 0, 0.0f, 0.0f, stick, 36);
    int nb = gm_hand_emit_held(1, 0, 0.0f, 0.0f, stone, 36);
    CHECK(nt == 36, "held torch emits a generated-item plate");
    CHECK(ni == 36, "stick reference emits a generated-item plate");
    CHECK(nb == 36, "stone reference emits a block cube");

    int same_as_item = 1, same_as_block = 1;
    for (int i = 0; i < 36; ++i) {
        const CrVec3 tp = torch[i].pos;
        const CrVec3 ip = stick[i].pos;
        const CrVec3 bp = stone[i].pos;
        if (tp.x != ip.x || tp.y != ip.y || tp.z != ip.z) same_as_item = 0;
        if (tp.x != bp.x || tp.y != bp.y || tp.z != bp.z) same_as_block = 0;
    }
    CHECK(same_as_item, "torch uses item/generated first-person geometry");
    CHECK(!same_as_block, "torch does not use block/block first-person geometry");

    if (g_fail) return 1;
    printf("test_hand_torch: PASS (torch follows item/generated, not block/block)\n");
    return 0;
}
