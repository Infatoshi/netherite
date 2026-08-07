/* test_screen: container-screen layout + hit-test invariants (pure, no SDL).
 * The slot rects are the vanilla 1.11.2 GUI coordinates; the hit test must
 * round-trip every rect center, report the panel background as -1 and anything
 * beyond the panel as GMC_OUTSIDE, and never emit duplicate slot ids. */
#include "game/screen.h"
#include "game/container_live.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static void check_container(int container, int fb_w, int fb_h, int want_slots)
{
    GmScreenSlot slots[GMC_SLOT_COUNT];
    int n = gm_screen_layout(container, fb_w, fb_h, slots, GMC_SLOT_COUNT);
    char msg[128];
    snprintf(msg, sizeof msg, "container %d @%dx%d has %d slots", container, fb_w, fb_h, want_slots);
    CHECK(n == want_slots, msg);

    int seen[GMC_SLOT_COUNT];
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < n; ++i) {
        CHECK(slots[i].slot_id >= 0 && slots[i].slot_id < GMC_SLOT_COUNT, "slot id in range");
        CHECK(!seen[slots[i].slot_id], "no duplicate slot ids");
        seen[slots[i].slot_id] = 1;
        int cx = slots[i].x + slots[i].w / 2, cy = slots[i].y + slots[i].h / 2;
        CHECK(gm_screen_slot_at(container, fb_w, fb_h, cx, cy) == slots[i].slot_id,
              "rect center hit-tests back to its slot id");
        CHECK(slots[i].x >= 0 && slots[i].y >= 0 &&
              slots[i].x + slots[i].w <= fb_w && slots[i].y + slots[i].h <= fb_h,
              "slot rect inside the framebuffer");
    }
    /* every GUI shares the full 36-slot player inventory */
    for (int s = 0; s < GMC_INV_SLOTS; ++s) CHECK(seen[s], "player slot present");

    CHECK(gm_screen_slot_at(container, fb_w, fb_h, 0, 0) == GMC_OUTSIDE,
          "far corner is OUTSIDE (cursor drop)");
    /* panel top-left corner: inside the panel, not on a slot (vanilla origin:
     * integer division in GUI units, so 854x480 lands at fb x 250, not 251) */
    { int s = fb_h / 240 > 1 ? fb_h / 240 : 1;
      int ph = container == 3 ? 168 : 166; /* GuiChest ySize; tex is 167 */
      int gw = (fb_w + s - 1) / s, gh = (fb_h + s - 1) / s;
      int px = (gw - 176) / 2 * s, py = (gh - ph) / 2 * s;
      CHECK(gm_screen_slot_at(container, fb_w, fb_h, px, py) == -1,
            "panel background is a no-op (-1)");
      if (fb_w == 854) CHECK(px == 250, "vanilla 854-wide origin floors to 250"); }
}

int main(void)
{
    /* 854x480 (product default, scale 2) and a scale-1 window */
    static const int sizes[][2] = {{854, 480}, {320, 200}};
    for (int z = 0; z < 2; ++z) {
        check_container(0, sizes[z][0], sizes[z][1], 45); /* 36 + 4 armor + 2x2 + result */
        check_container(1, sizes[z][0], sizes[z][1], 46); /* 36 + 3x3 grid + result */
        check_container(2, sizes[z][0], sizes[z][1], 39); /* 36 + furnace 3 */
        check_container(3, sizes[z][0], sizes[z][1], 63); /* 36 + chest 27 */
        check_container(4, sizes[z][0], sizes[z][1], 41); /* 36 + brewing 5 */
    }

    /* tape "gui" class name -> container kind (OPEN_DIVERGENCES #9) */
    CHECK(gm_screen_kind_for_gui("GuiInventory") == 0, "GuiInventory -> player");
    CHECK(gm_screen_kind_for_gui("GuiCrafting") == 1, "GuiCrafting -> workbench");
    CHECK(gm_screen_kind_for_gui("GuiFurnace") == 2, "GuiFurnace -> furnace");
    CHECK(gm_screen_kind_for_gui("GuiChest") == 3, "GuiChest -> chest");
    CHECK(gm_screen_kind_for_gui("GuiBrewingStand") == 4,
          "GuiBrewingStand -> brewing stand");
    CHECK(gm_screen_kind_for_gui("GuiIngameMenu") == -1, "GuiIngameMenu skipped");
    CHECK(gm_screen_kind_for_gui("GuiChat") == -1, "GuiChat skipped");
    CHECK(gm_screen_kind_for_gui("GuiUnknown") == -1, "unknown skipped");
    CHECK(gm_screen_kind_for_gui(NULL) == -1, "NULL skipped");
    CHECK(gm_screen_kind_for_gui("") == -1, "empty skipped");

    /* ScaledResolution mouse -> framebuffer: at 854x480 scale is 2 */
    CHECK(gm_screen_gui_scale(480) == 2, "scale 2 at 480h");
    CHECK(gm_screen_gui_scale(240) == 1, "scale 1 at 240h");
    { int mx, my;
      gm_screen_mouse_to_fb(854, 480, 213, 120, &mx, &my);
      CHECK(mx == 426 && my == 240, "center gui (213,120) -> fb (426,240)");
      gm_screen_mouse_to_fb(854, 480, 0, 0, &mx, &my);
      CHECK(mx == 0 && my == 0, "origin stays 0");
    }

    if (fail) { fprintf(stderr, "screen: FAIL\n"); return 1; }
    fprintf(stderr, "screen: PASS\n");
    return 0;
}
