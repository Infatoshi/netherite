/* test_screen: container-screen layout + hit-test invariants (pure, no SDL).
 * The slot rects are the vanilla 1.11.2 GUI coordinates; the hit test must
 * round-trip every rect center, report the panel background as -1 and anything
 * beyond the panel as GMC_OUTSIDE, and never emit duplicate slot ids. */
#include "game/screen.h"
#include "game/container_live.h"
#include "game/player_preview.h"
#include "game/runtime.h"

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
      int ph = container == 10 ? 222
             : container == 3 ? 168
             : container == 9 ? 167
             : container == 14 ? 133
             : container == 11 ? 219 : 166;
      int pw = container == 11 ? 230 : 176;
      int gw = (fb_w + s - 1) / s, gh = (fb_h + s - 1) / s;
      int px = (gw - pw) / 2 * s, py = (gh - ph) / 2 * s;
      CHECK(gm_screen_slot_at(container, fb_w, fb_h, px, py) == -1,
            "panel background is a no-op (-1)");
      if (fb_w == 854 && container != 11)
          CHECK(px == 250, "vanilla 854-wide origin floors to 250"); }
}

static void check_native_menus(int fb_w, int fb_h)
{
    int s = fb_h / 240 > 1 ? fb_h / 240 : 1;
    int gw = (fb_w + s - 1) / s;
    int x = (gw - 200) / 2 * s;
    CHECK(gm_screen_pause_button_at(
              fb_w, fb_h, x + 100 * s, 92 * s) == 0,
          "pause resume button center");
    CHECK(gm_screen_pause_button_at(
              fb_w, fb_h, x + 100 * s, 116 * s) == 1,
          "pause save-and-title button center");
    CHECK(gm_screen_pause_button_at(fb_w, fb_h, 0, 0) == -1,
          "pause background is not a button");

    int row_x = (gw - 300) / 2 * s;
    for (int index = 0; index < 5; ++index) {
        CHECK(gm_screen_world_row_at(
                  fb_w, fb_h, row_x + 150 * s,
                  (30 + index * 32 + 15) * s, 5) == index,
              "world row center maps to exact row");
    }
    CHECK(gm_screen_world_row_at(
              fb_w, fb_h, row_x + 150 * s, (30 + 4 * 32 + 15) * s, 4) == -1,
          "world rows beyond count are disabled");
    CHECK(gm_screen_world_button_at(
              fb_w, fb_h, x + 49 * s, 217 * s) == 0,
          "play selected world button center");
    CHECK(gm_screen_world_button_at(
              fb_w, fb_h, x + 102 * s + 49 * s, 217 * s) == 1,
          "world list back button center");
}

static void check_structure_screen(void)
{
    enum { W = 854, H = 480 };
    static CrRgba pixels[W * H];
    GmRuntime runtime;
    memset(&runtime, 0, sizeof runtime);
    runtime.container = 12;
    runtime.structure_gui.active = 1;
    runtime.structure_gui.focus = GM_STRUCTURE_GUI_NAME;
    runtime.structure_gui.value.active = 1;
    runtime.structure_gui.value.mode = GM_STRUCTURE_MODE_SAVE;
    runtime.structure_gui.value.ignore_entities = 1;
    runtime.structure_gui.value.show_air = 1;
    runtime.structure_gui.value.show_bounding_box = 1;
    snprintf(runtime.structure_gui.value.name,
             sizeof runtime.structure_gui.value.name, "screen_fixture");
    snprintf(runtime.structure_gui.pos_x,
             sizeof runtime.structure_gui.pos_x, "1");
    snprintf(runtime.structure_gui.pos_y,
             sizeof runtime.structure_gui.pos_y, "2");
    snprintf(runtime.structure_gui.pos_z,
             sizeof runtime.structure_gui.pos_z, "3");
    snprintf(runtime.structure_gui.size_x,
             sizeof runtime.structure_gui.size_x, "4");
    snprintf(runtime.structure_gui.size_y,
             sizeof runtime.structure_gui.size_y, "5");
    snprintf(runtime.structure_gui.size_z,
             sizeof runtime.structure_gui.size_z, "6");
    snprintf(runtime.structure_gui.integrity,
             sizeof runtime.structure_gui.integrity, "1.0");
    snprintf(runtime.structure_gui.seed,
             sizeof runtime.structure_gui.seed, "0");

    CHECK(gm_screen_layout(12, W, H, NULL, 0) == 0,
          "Structure editor exposes no inventory slots");
    CHECK(gm_screen_slot_at(12, W, H, 0, 0) == -1,
          "Structure editor never maps background clicks to cursor drops");
    CHECK(gm_screen_structure_button_at(
              &runtime, W, H, 168, 390) == 18,
          "Structure mode button has exact Java rectangle");
    CHECK(gm_screen_structure_button_at(
              &runtime, W, H, 268, 440) == 0,
          "Structure Done button has exact Java rectangle");
    CHECK(gm_screen_structure_button_at(
              &runtime, W, H, 684, 180) == 22,
          "SAVE show-air button has exact Java rectangle");
    CHECK(gm_screen_structure_field_at(
              &runtime, W, H, 422, 100) == GM_STRUCTURE_GUI_NAME,
          "Structure name field has exact Java rectangle");

    runtime.structure_gui.value.mode = GM_STRUCTURE_MODE_LOAD;
    runtime.structure_gui.value.rotation = GM_STRUCTURE_ROTATION_CW90;
    CHECK(gm_screen_structure_button_at(
              &runtime, W, H, 344, 390) == -1,
          "selected 90-degree rotation button is disabled");
    CHECK(gm_screen_structure_button_at(
              &runtime, W, H, 262, 390) == 11,
          "unselected rotation control remains clickable");
    CHECK(gm_screen_structure_field_at(
              &runtime, W, H, 202, 260) == GM_STRUCTURE_GUI_INTEGRITY,
          "LOAD integrity field has exact Java rectangle");

    memset(pixels, 0, sizeof pixels);
    CrFramebuffer fb = {W, H, pixels, NULL};
    gm_screen_draw(&fb, &runtime, 0, 0);
    int colored = 0;
    for (int index = 0; index < W * H; ++index)
        colored += pixels[index].r != 0 || pixels[index].g != 0
            || pixels[index].b != 0;
    CHECK(colored > W * H / 2,
          "Structure form renders a full nonempty GuiScreen composition");
}

static void check_horse_preview(void)
{
    enum { W = 104, H = 116 };
    CrRgba pixels[W * H];
    memset(pixels, 0, sizeof pixels);
    CrFramebuffer fb = {W, H, pixels, NULL};
    gm_horse_preview_draw(&fb, 0, 0, W, H, 68, 6 | (4 << 8), 3,
                          16384, 0.0f, 0.0f);
    int visible = 0;
    unsigned long long center_hash = 1469598103934665603ULL;
    for (int i = 0; i < W * H; ++i) {
        if (pixels[i].a) ++visible;
        center_hash ^= pixels[i].r;
        center_hash *= 1099511628211ULL;
        center_hash ^= pixels[i].g;
        center_hash *= 1099511628211ULL;
        center_hash ^= pixels[i].b;
        center_hash *= 1099511628211ULL;
    }
    CHECK(visible > 300,
          "horse inventory preview emits a substantial skinned model");

    memset(pixels, 0, sizeof pixels);
    gm_horse_preview_draw(&fb, 0, 0, W, H, 69, 0, 0,
                          8192, 20.0f, -12.0f);
    int donkey_visible = 0;
    unsigned long long donkey_hash = 1469598103934665603ULL;
    for (int i = 0; i < W * H; ++i) {
        if (pixels[i].a) ++donkey_visible;
        donkey_hash ^= pixels[i].r;
        donkey_hash *= 1099511628211ULL;
        donkey_hash ^= pixels[i].g;
        donkey_hash *= 1099511628211ULL;
        donkey_hash ^= pixels[i].b;
        donkey_hash *= 1099511628211ULL;
    }
    CHECK(donkey_visible > 300, "chested donkey inventory preview renders");
    CHECK(donkey_hash != center_hash,
          "horse preview responds to species, skin and mouse pose");
}

int main(void)
{
    /* 854x480 (product default, scale 2) and a scale-1 window */
    static const int sizes[][2] = {{854, 480}, {320, 200}};
    for (int z = 0; z < 2; ++z) {
        check_container(0, sizes[z][0], sizes[z][1], 46); /* 36 + armor/offhand + 2x2 + result */
        check_container(1, sizes[z][0], sizes[z][1], 46); /* 36 + 3x3 grid + result */
        check_container(2, sizes[z][0], sizes[z][1], 39); /* 36 + furnace 3 */
        check_container(3, sizes[z][0], sizes[z][1], 63); /* 36 + chest 27 */
        check_container(4, sizes[z][0], sizes[z][1], 41); /* 36 + brewing 5 */
        check_container(5, sizes[z][0], sizes[z][1], 41); /* 36 + enchant 2 + 3 buttons */
        check_container(6, sizes[z][0], sizes[z][1], 39); /* 36 + anvil 3 */
        check_container(7, sizes[z][0], sizes[z][1], 41); /* 36 + merchant 3 + 2 buttons */
        check_container(8, sizes[z][0], sizes[z][1], 53); /* 36 + horse 17 */
        check_container(9, sizes[z][0], sizes[z][1], 63); /* 36 + shulker 27 */
        if (z == 0)
            check_container(10, sizes[z][0], sizes[z][1], 90); /* 36 + large chest 54 */
        if (z == 0)
            check_container(11, sizes[z][0], sizes[z][1], 46); /* 36 + payment + 9 buttons */
        check_container(13, sizes[z][0], sizes[z][1], 45); /* 36 + dispenser 9 */
        check_container(14, sizes[z][0], sizes[z][1], 41); /* 36 + hopper 5 */
        check_native_menus(sizes[z][0], sizes[z][1]);
    }

    /* tape "gui" class name -> container kind (OPEN_DIVERGENCES #9) */
    CHECK(gm_screen_kind_for_gui("GuiInventory") == 0, "GuiInventory -> player");
    CHECK(gm_screen_kind_for_gui("GuiCrafting") == 1, "GuiCrafting -> workbench");
    CHECK(gm_screen_kind_for_gui("GuiFurnace") == 2, "GuiFurnace -> furnace");
    CHECK(gm_screen_kind_for_gui("GuiChest") == 3, "GuiChest -> chest");
    CHECK(gm_screen_kind_for_gui("GuiLargeChest") == 10,
          "synthetic replay GuiLargeChest -> large chest");
    CHECK(gm_screen_kind_for_gui("GuiBrewingStand") == 4,
          "GuiBrewingStand -> brewing stand");
    CHECK(gm_screen_kind_for_gui("GuiMerchant") == 7,
          "GuiMerchant -> merchant");
    CHECK(gm_screen_kind_for_gui("GuiScreenHorseInventory") == 8,
          "GuiScreenHorseInventory -> horse inventory");
    CHECK(gm_screen_kind_for_gui("GuiBeacon") == 11,
          "GuiBeacon -> beacon");
    CHECK(gm_screen_kind_for_gui("GuiDispenser") == 13,
          "GuiDispenser -> dispenser/dropper");
    CHECK(gm_screen_kind_for_gui("GuiHopper") == 14,
          "GuiHopper -> hopper");
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

    check_horse_preview();
    check_structure_screen();

    if (fail) { fprintf(stderr, "screen: FAIL\n"); return 1; }
    fprintf(stderr, "screen: PASS\n");
    return 0;
}
