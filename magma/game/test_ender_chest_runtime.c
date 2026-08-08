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

static void clean_save(const char *root)
{
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t index = 0; index < sizeof files / sizeof files[0]; ++index) {
        snprintf(path, sizeof path,
                 "%s/ender/generation-0000000000000001/%s",
                 root, files[index]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
             "%s/ender/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/ender/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/ender/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/ender", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static void tick(GmRuntime *r, int count)
{
    GmAction action;
    memset(&action, 0, sizeof action);
    while (count-- > 0) gm_runtime_tick(r, action);
}

static void close_container(GmRuntime *r)
{
    GmAction action;
    memset(&action, 0, sizeof action);
    action.close_container = 1;
    gm_runtime_tick(r, action);
}

int main(void)
{
    GmRuntime r;
    GmConfig config;
    GmRuntimeEnderChest tile;
    ICStack stack;
    const int x = 8, y = 78, z = 8;
    char save_root[256];
    char error[256];

    snprintf(save_root, sizeof save_root,
             "../.tmp/ender-chest-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(init_flat(&r, &config), "initialize Ender Chest fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 130, 2),
          "place Overworld Ender Chest");
    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 1, 0),
          "place blocking normal cube");
    CHECK(gm_runtime_use_block(&r, x, y, z),
          "blocked activation is still consumed");
    CHECK(r.container == 0
              && gm_runtime_ender_chest_tile_get(
                  &r, 0, x, y, z, &tile)
              && tile.num_players_using == 0,
          "normal cube above prevents the inventory from opening");

    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 0, 0),
          "clear blocking cube");
    CHECK(gm_runtime_ender_chest_set_slot(
              &r, 0, ic_mk(264, 5, 0)),
          "seed player Ender inventory");
    CHECK(gm_runtime_use_block(&r, x, y, z)
              && r.container == 3
              && r.active_chest == GM_ACTIVE_ENDER_CHEST
              && gm_runtime_ender_chest_tile_get(
                  &r, 0, x, y, z, &tile)
              && tile.num_players_using == 1,
          "open the player-owned inventory through the tile");

    gm_player_cursor_set(ic_empty());
    CHECK(gm_container_click(&r, GMC_CHEST0, 0, CC_CLICK_PICKUP),
          "pick up Ender inventory slot");
    stack = gm_player_cursor();
    {
        ICStack emptied = gm_runtime_ender_chest_get_slot(&r, 0);
        CHECK(stack.item == 264 && stack.count == 5
                  && isr_is_empty(&emptied),
          "container routes the slot into InventoryEnderChest");
    }
    CHECK(gm_container_click(&r, GMC_CHEST0 + 7, 0, CC_CLICK_PICKUP),
          "put stack in another Ender slot");
    stack = gm_runtime_ender_chest_get_slot(&r, 7);
    {
        ICStack cursor = gm_player_cursor();
        CHECK(stack.item == 264 && stack.count == 5
                  && isr_is_empty(&cursor),
          "Ender slot mutation is persistent player state");
    }

    tick(&r, 1);
    CHECK(gm_runtime_ender_chest_tile_get(&r, 0, x, y, z, &tile)
              && tile.ticks_since_sync == 1
              && fbits(tile.prev_lid_angle) == fbits(0.0F)
              && fbits(tile.lid_angle) == fbits(0.1F),
          "first open animation tick matches TileEntityEnderChest");
    tick(&r, 9);
    CHECK(gm_runtime_ender_chest_tile_get(&r, 0, x, y, z, &tile)
              && tile.ticks_since_sync == 10
              && fbits(tile.lid_angle) == fbits(1.0F),
          "open lid clamps at one after ten ticks");

    close_container(&r);
    CHECK(r.container == 0
              && gm_runtime_ender_chest_tile_get(
                  &r, 0, x, y, z, &tile)
              && tile.num_players_using == 0
              && tile.ticks_since_sync == 11
              && fbits(tile.prev_lid_angle) == fbits(1.0F)
              && fbits(tile.lid_angle) == fbits(0.9F),
          "closing decrements viewers before the next tile tick");

    CHECK(gm_runtime_set_dimension(&r, -1), "switch to Nether");
    CHECK(gm_runtime_set_block(&r, x, y, z, 130, 5),
          "place Nether Ender Chest at same coordinates");
    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 0, 0),
          "clear Nether lid space");
    CHECK(gm_runtime_use_block(&r, x, y, z), "open Nether Ender Chest");
    stack = gm_runtime_ender_chest_get_slot(&r, 7);
    CHECK(stack.item == 264 && stack.count == 5
              && gm_runtime_ender_chest_tile_get(
                  &r, -1, x, y, z, &tile)
              && tile.num_players_using == 1,
          "all dimensions expose the same player inventory");
    close_container(&r);

    CHECK(gm_native_save_write(
              &r, save_root, "ender", error, sizeof error),
          error);
    CHECK(gm_runtime_ender_chest_set_slot(&r, 7, ic_empty()),
          "mutate state after checkpoint");
    CHECK(gm_runtime_set_block(&r, x, y, z, 0, 0),
          "remove Nether Ender Chest after checkpoint");
    CHECK(gm_native_save_load(
              &r, &config, save_root, "ender", error, sizeof error),
          error);
    CHECK(gm_runtime_snapshot_region_dim(&r, -1, 0, 0, 0),
          "materialize saved Nether column");
    stack = gm_runtime_ender_chest_get_slot(&r, 7);
    CHECK(r.dimension == -1 && stack.item == 264 && stack.count == 5
              && gm_world_block(r.world, x, y, z) == 130
              && gm_runtime_ender_chest_tile_get(
                  &r, -1, x, y, z, &tile)
              && tile.active && tile.num_players_using == 0,
          "checkpoint restores player inventory and dimension-keyed tile");

    CHECK(gm_runtime_set_block(&r, x, y, z, 0, 0),
          "break reloaded Nether Ender Chest");
    stack = gm_runtime_ender_chest_get_slot(&r, 7);
    CHECK(stack.item == 264 && stack.count == 5
              && !gm_runtime_ender_chest_tile_get(
                  &r, -1, x, y, z, &tile),
          "breaking a tile never drops or clears the player inventory");

    {
        int item = 0, count = 0, meta = 0;
        CHECK(gm_runtime_harvest_drop_result(
                  &r, 130, 5, 270, 0, 3, &item, &count, &meta)
                  && item == 49 && count == 8 && meta == 0,
              "ordinary pickaxe harvest drops eight obsidian");
        CHECK(gm_runtime_harvest_drop_result(
                  &r, 130, 5, 270, 1, 3, &item, &count, &meta)
                  && item == 130 && count == 1 && meta == 0,
              "Silk Touch pickaxe harvest drops one Ender Chest");
        CHECK(gm_runtime_harvest_drop_result(
                  &r, 130, 5, 0, 0, 3, &item, &count, &meta)
                  && item == 0 && count == 0 && meta == 0,
              "hand harvest drops nothing");
        CHECK(gm_runtime_harvest_drop_result(
                  &r, 130, 5, 277, 1, 3, &item, &count, &meta)
                  && item == 0 && count == 0 && meta == 0,
              "Silk Touch shovel harvest drops nothing");
    }

    gm_runtime_destroy(&r);
    clean_save(save_root);

    CHECK(init_flat(&r, &config), "initialize ordinary Chest audio fixture");
    {
        GmRuntimeChest chest;
        GmRuntimeSoundEvent event;
        const int cx = 10, cy = 78, cz = 10;
        CHECK(gm_runtime_set_block(&r, cx, cy, cz, 54, 2)
                  && gm_runtime_chest_set_slot(
                      &r, 0, cx, cy, cz, 0, 264, 1, 0)
                  && gm_runtime_chest_set_transient(
                      &r, 0, cx, cy, cz, 1,
                      fbits(0.0F), fbits(0.0F), 17)
                  && gm_runtime_set_world_random_seed48(&r, UINT64_C(0)),
              "restore an opening ordinary Chest");
        tick(&r, 1);
        CHECK(gm_runtime_chest_count(&r) == 1
                  && gm_runtime_chest_get(&r, 0, &chest)
                  && chest.state.te.num_players_using == 1
                  && chest.state.te.ticks_since_sync == 18
                  && fbits(chest.state.te.prev_lid_angle) == fbits(0.0F)
                  && fbits(chest.state.te.lid_angle) == fbits(0.1F)
                  && gm_runtime_sound_event_count(&r) == 1
                  && gm_runtime_sound_event_get(&r, 0, &event)
                  && event.sound == GM_SOUND_CHEST_OPEN
                  && event.category == GM_SOUND_CATEGORY_BLOCKS
                  && event.x == 10.5 && event.y == 78.5 && event.z == 10.5
                  && fbits(event.volume) == fbits(0.5F)
                  && fbits(event.pitch) == fbits(0.9F)
                  && r.world_random_seed48 == UINT64_C(11),
              "ordinary Chest opening consumes one exact sound draw");
        CHECK(gm_runtime_chest_set_transient(
                  &r, 0, cx, cy, cz, 0,
                  fbits(0.6F), fbits(0.7F), 31)
                  && gm_runtime_set_world_random_seed48(&r, UINT64_C(0)),
              "restore a closing ordinary Chest");
        tick(&r, 1);
        CHECK(gm_runtime_sound_event_count(&r) == 1
                  && r.world_random_seed48 == UINT64_C(0),
              "lid reaching one half does not close-sound early");
        tick(&r, 1);
        CHECK(gm_runtime_chest_get(&r, 0, &chest)
                  && chest.state.te.ticks_since_sync == 33
                  && fbits(chest.state.te.prev_lid_angle) == fbits(0.5F)
                  && fbits(chest.state.te.lid_angle) == fbits(0.4F)
                  && gm_runtime_sound_event_count(&r) == 2
                  && gm_runtime_sound_event_get(&r, 1, &event)
                  && event.sound == GM_SOUND_CHEST_CLOSE
                  && fbits(event.pitch) == fbits(0.9F)
                  && r.world_random_seed48 == UINT64_C(11),
              "ordinary Chest close threshold consumes one exact sound draw");
    }
    gm_runtime_destroy(&r);
    puts("PASS Ender/ordinary Chest: container/lid/save/harvest/audio/RNG");
    return 0;
}
