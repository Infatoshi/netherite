#include "game/runtime.h"
#include "game/native_save.h"

#include "container_click.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static unsigned fbits(float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static int init_flat(GmRuntime *r, GmConfig *config)
{
    char error[256];
    gm_config_defaults(config);
    config->seed = 42;
    config->world = GM_WORLD_SUPERFLAT;
    config->view_distance = 1;
    config->mobs = 0;
    config->weather = 0;
    if (!gm_runtime_init(r, config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    r->randtick_enabled = 0;
    gm_runtime_set_pose(r, 8.5, 78.0, 6.5, 180.0F, 0.0F);
    return 1;
}

static int box_at(const GmRuntime *r, int x, int y, int z,
                  GmRuntimeStaticContainer *out)
{
    int count = gm_runtime_static_container_count(r);
    for (int index = 0; index < count; ++index) {
        GmRuntimeStaticContainer found;
        if (gm_runtime_static_container_get(r, index, &found)
                && found.wx == x && found.wy == y && found.wz == z) {
            *out = found;
            return 1;
        }
    }
    return 0;
}

static void tick(GmRuntime *r, int count)
{
    GmAction action;
    memset(&action, 0, sizeof action);
    action.hotbar_sel = -1;
    while (count-- > 0) gm_runtime_tick(r, action);
}

static void close_container(GmRuntime *r)
{
    GmAction action;
    memset(&action, 0, sizeof action);
    action.hotbar_sel = -1;
    action.close_container = 1;
    gm_runtime_tick(r, action);
}

static void clean_save(const char *root)
{
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t index = 0; index < sizeof files / sizeof files[0]; ++index) {
        snprintf(path, sizeof path,
                 "%s/shulker/generation-0000000000000001/%s",
                 root, files[index]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
             "%s/shulker/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/shulker/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/shulker/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/shulker", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

int main(void)
{
    GmRuntime r;
    GmConfig config;
    GmRuntimeStaticContainer box;
    GmRuntimeSoundEvent sound;
    ICStack stack;
    const int x = 8, y = 78, z = 8;
    char save_root[256], error[256];

    snprintf(save_root, sizeof save_root,
             "../.tmp/shulker-box-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(init_flat(&r, &config), "initialize Shulker Box fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 219, 1)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 264, 5, 0),
          "place an UP-facing white Shulker Box with inventory");
    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 1, 0),
          "block the half-block lid extension");
    CHECK(gm_runtime_use_block(&r, x, y, z)
              && r.container == 0
              && box_at(&r, x, y, z, &box)
              && box.shulker_open_count == 0
              && box.shulker_animation_status == GM_SHULKER_BOX_CLOSED
              && gm_runtime_sound_event_count(&r) == 0,
          "blocked activation is consumed without opening or audio");

    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 0, 0),
          "clear the Shulker Box lid extension");
    CHECK(gm_runtime_use_block(&r, x, y, z)
              && r.container == 9
              && r.active_chest == GM_ACTIVE_SHULKER_BOX
              && box_at(&r, x, y, z, &box)
              && box.shulker_open_count == 1
              && box.shulker_animation_status == GM_SHULKER_BOX_OPENING
              && gm_runtime_sound_event_count(&r) == 1
              && gm_runtime_sound_event_get(&r, 0, &sound)
              && sound.sound == GM_SOUND_SHULKER_BOX_OPEN
              && sound.volume == 0.5F
              && sound.pitch >= 0.9F && sound.pitch < 1.0F,
          "opening selects GuiShulkerBox and exact block sound envelope");

    gm_player_cursor_set(ic_mk(219, 1, 0));
    CHECK(gm_container_click(&r, GMC_CHEST0 + 1, 0, CC_CLICK_PICKUP),
          "nested Shulker Box click is consumed");
    stack = gm_player_cursor();
    CHECK(box_at(&r, x, y, z, &box)
              && stack.item == 219 && stack.count == 1
              && isr_is_empty(&box.slots[1]),
          "SlotShulkerBox rejects every nested Shulker Box item");

    gm_player_cursor_set(ic_mk(4, 7, 0));
    CHECK(gm_container_click(&r, GMC_CHEST0 + 1, 0, CC_CLICK_PICKUP),
          "ordinary block stack click is accepted");
    stack = gm_player_cursor();
    CHECK(box_at(&r, x, y, z, &box)
              && isr_is_empty(&stack)
              && box.slots[1].item == 4 && box.slots[1].count == 7,
          "container click mutates the persistent 27-slot inventory");

    CHECK(gm_container_click(&r, GMC_CHEST0, 0, CC_CLICK_PICKUP),
          "pick up the seeded inventory stack");
    CHECK(gm_container_click(&r, GMC_CHEST0 + 2, 0, CC_CLICK_PICKUP),
          "move the seeded inventory stack");
    CHECK(box_at(&r, x, y, z, &box)
              && isr_is_empty(&box.slots[0])
              && box.slots[2].item == 264 && box.slots[2].count == 5,
          "Shulker Box inventory routing preserves item identity and count");

    tick(&r, 1);
    CHECK(box_at(&r, x, y, z, &box)
              && fbits(box.shulker_progress_old) == fbits(0.0F)
              && fbits(box.shulker_progress) == fbits(0.1F)
              && box.shulker_animation_status == GM_SHULKER_BOX_OPENING,
          "first animation tick matches TileEntityShulkerBox");
    tick(&r, 3);
    CHECK(box_at(&r, x, y, z, &box)
              && fbits(box.shulker_progress) == fbits(0.4F),
          "opening advances by exact binary32 0.1 steps");

    CHECK(gm_native_save_write(
              &r, save_root, "shulker", error, sizeof error), error);
    tick(&r, 6);
    CHECK(box_at(&r, x, y, z, &box)
              && fbits(box.shulker_progress) == fbits(1.0F)
              && box.shulker_animation_status == GM_SHULKER_BOX_OPENED,
          "open lid clamps at one after ten ticks");
    CHECK(gm_native_save_load(
              &r, &config, save_root, "shulker", error, sizeof error), error);
    CHECK(gm_runtime_snapshot_region_dim(&r, 0, 0, 0, 0)
              && box_at(&r, x, y, z, &box)
              && fbits(box.shulker_progress_old) == fbits(0.3F)
              && fbits(box.shulker_progress) == fbits(0.4F)
              && box.shulker_animation_status == GM_SHULKER_BOX_OPENING
              && box.shulker_open_count == 1
              && r.container == 9,
          "native checkpoint continues the exact mid-animation GUI state");

    tick(&r, 6);
    CHECK(box_at(&r, x, y, z, &box)
              && fbits(box.shulker_progress) == fbits(1.0F)
              && box.shulker_animation_status == GM_SHULKER_BOX_OPENED,
          "restored animation converges on the same open state");
    close_container(&r);
    CHECK(r.container == 0
              && box_at(&r, x, y, z, &box)
              && box.shulker_open_count == 0
              && box.shulker_animation_status == GM_SHULKER_BOX_CLOSING
              && fbits(box.shulker_progress_old) == fbits(1.0F)
              && fbits(box.shulker_progress) == fbits(0.9F)
              && gm_runtime_sound_event_count(&r) == 2
              && gm_runtime_sound_event_get(&r, 1, &sound)
              && sound.sound == GM_SOUND_SHULKER_BOX_CLOSE,
          "closing decrements viewers before the same tick's tile update");
    tick(&r, 9);
    CHECK(box_at(&r, x, y, z, &box)
              && fbits(box.shulker_progress) == fbits(0.0F)
              && box.shulker_animation_status == GM_SHULKER_BOX_CLOSED,
          "closed lid clamps at zero after ten ticks");

    CHECK(gm_runtime_set_block(&r, x, y, z, 0, 0)
              && r.container == 0
              && !box_at(&r, x, y, z, &box),
          "breaking removes tile inventory and closes any active view");

    gm_runtime_destroy(&r);
    clean_save(save_root);
    puts("PASS Shulker Box: blocking/container/animation/audio/checkpoint/break");
    return 0;
}
