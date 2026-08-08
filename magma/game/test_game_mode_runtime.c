#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static int pose_hits(
        GmRuntime *r, int tx, int ty, int tz,
        double x, double y, double z, float yaw, float pitch) {
    int hx, hy, hz, ax, ay, az;
    gm_runtime_set_pose(r, x, y, z, yaw, pitch);
    gm_world_fill_window(
        r->world, r->ccx, r->ccz, (struct Chunk *)r->window);
    return psv_raycast(
               r->window, &r->sin_table, &r->player,
               &hx, &hy, &hz, &ax, &ay, &az) >= 0
        && hx + r->ox == tx && hy == ty && hz + r->oz == tz;
}

static void run_mode(
        int game_mode, int tagged_can_destroy, int expect_break,
        int expect_disable_damage) {
    GmConfig config;
    GmRuntime *runtime = (GmRuntime *)calloc(1, sizeof *runtime);
    GmAction idle, hit;
    char error[256];
    CHECK(runtime != NULL, "allocate focused game-mode runtime");
    if (!runtime) return;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.game_mode = (GmGameMode)game_mode;
    CHECK(gm_runtime_init(runtime, &config, error, sizeof error),
          "focused game-mode runtime initializes");
    if (!runtime->world) {
        free(runtime);
        return;
    }
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    memset(&hit, 0, sizeof hit);
    hit.hotbar_sel = -1;
    hit.attack = 1;
    hit.do_break = 1;
    gm_player_dig_reset();
    gm_runtime_set_pose(runtime, 8.5, 4.0, 8.5, 0.0F, 0.0F);
    gm_runtime_tick(runtime, idle);
    if (tagged_can_destroy) {
        static const char *const values[] = {"minecraft:stone"};
        GmNbtBlob tag = {0};
        ICStack tool = ic_mk(278, 1, 0);
        CHECK(gm_nbt_blob_make_empty(&tag)
                  && gm_nbt_blob_set_string_list(
                      &tag, "CanDestroy", values, 1),
              "adventure CanDestroy tag constructs");
        tool.tag_id = gm_runtime_stack_tag_intern(
            runtime, tag.data, tag.len);
        CHECK(tool.tag_id > 0
                  && gm_runtime_set_inventory_stack(runtime, 0, tool),
              "adventure CanDestroy tool enters the selected slot");
        gm_nbt_blob_clear(&tag);
    }
    gm_runtime_set_block(runtime, 8, 5, 11, 1, 0);
    CHECK(pose_hits(runtime, 8, 5, 11, 8.5, 4.0, 8.5, 0.0F, 0.0F),
          "game-mode fixture ray selects the target block");
    for (int tick = 0; tick < 20; ++tick) {
        gm_runtime_tick(runtime, hit);
        hit.do_break = 0;
    }
    if ((gm_world_block(runtime->world, 8, 5, 11) == 0) != expect_break) {
        fprintf(stderr,
                "FAIL: game mode %d tagged=%d expected break=%d got block=%d\n",
                game_mode, tagged_can_destroy, expect_break,
                gm_world_block(runtime->world, 8, 5, 11));
        fail = 1;
    }
    CHECK(runtime->mobs.player_disable_damage == expect_disable_damage
              && runtime->mobs.player_creative == expect_disable_damage,
          "game mode exposes the expected damage and targeting capabilities");
    gm_runtime_destroy(runtime);
    free(runtime);
}

static void run_place(
        int game_mode, int tagged_can_place, int expect_place) {
    GmConfig config;
    GmRuntime *runtime = (GmRuntime *)calloc(1, sizeof *runtime);
    GmAction idle, place;
    char error[256];
    CHECK(runtime != NULL, "allocate Adventure placement runtime");
    if (!runtime) return;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.game_mode = (GmGameMode)game_mode;
    CHECK(gm_runtime_init(runtime, &config, error, sizeof error),
          "Adventure placement runtime initializes");
    if (!runtime->world) {
        free(runtime);
        return;
    }
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    memset(&place, 0, sizeof place);
    place.hotbar_sel = -1;
    place.use = place.do_place = 1;
    ICStack dirt = ic_mk(3, 1, 0);
    if (tagged_can_place) {
        static const char *const values[] = {"minecraft:stone"};
        GmNbtBlob tag = {0};
        CHECK(gm_nbt_blob_make_empty(&tag)
                  && gm_nbt_blob_set_string_list(
                      &tag, "CanPlaceOn", values, 1),
              "Adventure CanPlaceOn tag constructs");
        dirt.tag_id = gm_runtime_stack_tag_intern(
            runtime, tag.data, tag.len);
        gm_nbt_blob_clear(&tag);
    }
    CHECK(gm_runtime_set_inventory_stack(runtime, 0, dirt),
          "Adventure placement stack enters selected slot");
    gm_player_dig_reset();
    gm_runtime_set_pose(runtime, 8.5, 4.0, 8.5, 0.0F, 0.0F);
    gm_runtime_tick(runtime, idle);
    gm_runtime_set_block(runtime, 8, 5, 11, 1, 0);
    CHECK(pose_hits(runtime, 8, 5, 11, 8.5, 4.0, 8.5, 0.0F, 0.0F),
          "Adventure placement ray selects stone support");
    gm_runtime_tick(runtime, place);
    if ((gm_world_block(runtime->world, 8, 5, 10) == 3) != expect_place) {
        fprintf(stderr,
                "FAIL: mode=%d CanPlaceOn tagged=%d expected place=%d "
                "got block=%d\n",
                game_mode, tagged_can_place, expect_place,
                gm_world_block(runtime->world, 8, 5, 10));
        fail = 1;
    }
    gm_runtime_destroy(runtime);
    free(runtime);
}

static void run_spectator_flight(void) {
    GmConfig config;
    GmRuntime *runtime = (GmRuntime *)calloc(1, sizeof *runtime);
    GmAction fly;
    char error[256];
    CHECK(runtime != NULL, "allocate spectator-flight runtime");
    if (!runtime) return;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.game_mode = GM_MODE_SPECTATOR;
    CHECK(gm_runtime_init(runtime, &config, error, sizeof error),
          "spectator-flight runtime initializes");
    if (!runtime->world) {
        free(runtime);
        return;
    }
    memset(&fly, 0, sizeof fly);
    fly.hotbar_sel = -1;
    fly.forward = 1.0F;
    fly.jump = 1;
    gm_runtime_set_pose(runtime, 8.5, 10.0, 8.5, 0.0F, 0.0F);
    runtime->player.ent.motionX = 0.0;
    runtime->player.ent.motionY = 0.0;
    runtime->player.ent.motionZ = 0.0;
    gm_runtime_tick(runtime, fly);
    if (!(fabs(runtime->player.ent.posX - 8.5) < 1.0e-12
              && fabs(runtime->player.ent.posY - 10.15) < 1.0e-7
              && fabs(runtime->player.ent.posZ - 8.55) < 1.0e-7))
        fprintf(stderr, "spectator pose %.17g %.17g %.17g\n",
                runtime->player.ent.posX, runtime->player.ent.posY,
                runtime->player.ent.posZ);
    CHECK(fabs(runtime->player.ent.posX - 8.5) < 1.0e-12
              && fabs(runtime->player.ent.posY - 10.15) < 1.0e-7
              && fabs(runtime->player.ent.posZ - 8.55) < 1.0e-7,
          "spectator free-flight applies exact no-clip input displacement");
    CHECK(fabs(runtime->player.ent.motionY - 0.09) < 1.0e-7
              && fabs(runtime->player.ent.motionZ - 0.0455) < 1.0e-7
              && !runtime->player.ent.onGround,
          "spectator free-flight applies Java horizontal and vertical drag");
    if (!(fabs(runtime->player.ent.motionY - 0.09) < 1.0e-7
              && fabs(runtime->player.ent.motionZ - 0.0455) < 1.0e-7))
        fprintf(stderr, "spectator motion %.17g %.17g %.17g ground=%d\n",
                runtime->player.ent.motionX, runtime->player.ent.motionY,
                runtime->player.ent.motionZ, runtime->player.ent.onGround);
    gm_runtime_destroy(runtime);
    free(runtime);
}

int main(void) {
    run_mode(GM_MODE_SPECTATOR, 0, 0, 1);
    run_mode(GM_MODE_CREATIVE, 0, 1, 1);
    run_mode(GM_MODE_ADVENTURE, 0, 0, 0);
    run_mode(GM_MODE_ADVENTURE, 1, 1, 0);
    run_place(GM_MODE_ADVENTURE, 0, 0);
    run_place(GM_MODE_ADVENTURE, 1, 1);
    run_place(GM_MODE_CREATIVE, 0, 1);
    run_spectator_flight();
    if (fail) return 1;
    puts("PASS game-mode runtime: spectator capabilities and Adventure tags");
    return 0;
}
