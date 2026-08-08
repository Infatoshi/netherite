#include "game/runtime.h"
#include "container_click.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static unsigned fbits(float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static int init_flat(GmRuntime *r)
{
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(r, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    r->randtick_enabled = 0;
    gm_runtime_set_pose(r, 8.5, 80.0, 6.5, 180.0F, 0.0F);
    return 1;
}

static int effect(const GmRuntime *r, int id, int *amplifier, int *duration)
{
    for (int index = 0; index < r->potion_count; ++index) {
        if (r->potions[index].id != id) continue;
        if (amplifier) *amplifier = r->potions[index].amplifier;
        if (duration) *duration = r->potions[index].duration;
        return 1;
    }
    return 0;
}

static int build_pyramid(GmRuntime *r, int x, int y, int z, int levels)
{
    for (int level = 1; level <= levels; ++level)
        for (int bx = x - level; bx <= x + level; ++bx)
            for (int bz = z - level; bz <= z + level; ++bz)
                if (!gm_runtime_set_block(r, bx, y - level, bz,
                                          level & 1 ? 42 : 57, 0))
                    return 0;
    return 1;
}

int main(void)
{
    GmRuntime r;
    GmRuntimeStaticContainer beacon;
    const int x = 8, y = 80, z = 8;
    int amplifier, duration;

    CHECK(init_flat(&r), "initialize Beacon fixture");
    CHECK(gm_runtime_beacon_valid_effect(1)
              && gm_runtime_beacon_valid_effect(3)
              && gm_runtime_beacon_valid_effect(5)
              && gm_runtime_beacon_valid_effect(8)
              && gm_runtime_beacon_valid_effect(10)
              && gm_runtime_beacon_valid_effect(11)
              && !gm_runtime_beacon_valid_effect(2),
          "valid effect registry is the six-entry vanilla set");
    CHECK(gm_runtime_beacon_payment_item(388)
              && gm_runtime_beacon_payment_item(264)
              && gm_runtime_beacon_payment_item(266)
              && gm_runtime_beacon_payment_item(265)
              && !gm_runtime_beacon_payment_item(263),
          "payment registry is emerald, diamond, gold, and iron");
    CHECK(gm_runtime_set_block(&r, x, y, z, 138, 0)
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && beacon.beacon_levels == -1
              && !beacon.beacon_complete,
          "placing a Beacon creates the Java-default tile state");
    CHECK(build_pyramid(&r, x, y, z, 4),
          "build mixed valid four-level pyramid");
    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 95, 1)
              && gm_runtime_set_block(&r, x, y + 2, z, 160, 11),
          "place orange glass then blue pane in the beam");
    CHECK(gm_runtime_beacon_set_state(&r, 0, x, y, z, -1, 1, 1, 0)
              && gm_runtime_beacon_update(&r, 0, x, y, z)
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon),
          "scan complete four-level Beacon");
    CHECK(beacon.beacon_complete && beacon.beacon_levels == 4
              && beacon.beacon_segment_count == 3,
          "pyramid and beam segment counts match TileEntityBeacon");
    CHECK(beacon.beacon_segments[0].height == 1
              && fbits(beacon.beacon_segments[0].red) == fbits(1.0F)
              && beacon.beacon_segments[1].height == 1
              && fbits(beacon.beacon_segments[1].red) == fbits(0.85F)
              && fbits(beacon.beacon_segments[1].green) == fbits(0.5F)
              && fbits(beacon.beacon_segments[1].blue) == fbits(0.2F)
              && beacon.beacon_segments[2].height == 174
              && fbits(beacon.beacon_segments[2].red)
                    == fbits((0.85F + 0.2F) / 2.0F)
              && fbits(beacon.beacon_segments[2].green)
                    == fbits((0.5F + 0.3F) / 2.0F)
              && fbits(beacon.beacon_segments[2].blue)
                    == fbits((0.2F + 0.7F) / 2.0F),
          "glass colors preserve Java binary32 mixing and exact heights");
    CHECK(effect(&r, 1, &amplifier, &duration)
              && amplifier == 1 && duration == 340,
          "level-four same-effect Beacon applies Speed II for 340 ticks");

    gm_runtime_potions_clear(&r);
    CHECK(gm_runtime_set_block(&r, x + 4, y - 4, z + 4, 1, 0)
              && gm_runtime_beacon_set_state(&r, 0, x, y, z, 4, 5, 10, 1)
              && gm_runtime_beacon_update(&r, 0, x, y, z)
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && beacon.beacon_complete && beacon.beacon_levels == 3,
          "first invalid layer stops the pyramid at the previous level");
    CHECK(effect(&r, 5, &amplifier, &duration)
              && amplifier == 0 && duration == 300
              && !effect(&r, 10, NULL, NULL),
          "level-three Beacon applies primary only with exact duration");

    gm_runtime_potions_clear(&r);
    CHECK(gm_runtime_set_block(&r, x, y + 3, z, 1, 0)
              && gm_runtime_beacon_update(&r, 0, x, y, z)
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && !beacon.beacon_complete && beacon.beacon_levels == 0
              && beacon.beacon_segment_count == 0
              && r.potion_count == 0,
          "opaque non-bedrock obstruction clears beam and effects");
    CHECK(gm_runtime_set_block(&r, x, y + 3, z, 7, 0)
              && gm_runtime_beacon_update(&r, 0, x, y, z)
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && beacon.beacon_complete && beacon.beacon_levels == 3,
          "bedrock is the exact opaque beam exception");

    CHECK(gm_runtime_set_block(&r, x + 4, y - 4, z + 4, 42, 0)
              && gm_runtime_set_block(&r, x, y + 3, z, 0, 0)
              && gm_runtime_beacon_set_state(&r, 0, x, y, z, 0, 3, 0, 0),
          "reset Beacon before periodic update boundary");
    gm_runtime_set_total_time(&r, 78);
    {
        GmAction action;
        memset(&action, 0, sizeof action);
        action.hotbar_sel = -1;
        gm_runtime_tick(&r, action);
        CHECK(gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
                  && beacon.beacon_levels == 0,
              "total time 79 does not scan Beacon");
        gm_runtime_tick(&r, action);
    }
    CHECK(gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && beacon.beacon_complete && beacon.beacon_levels == 4,
          "total time 80 performs the periodic scan");
    beacon.beacon_render_counter = 0;
    beacon.beacon_render_scale = 0.0F;
    CHECK(fbits(gm_runtime_beacon_should_render(&beacon, 80))
                  == fbits(0.025F)
              && fbits(gm_runtime_beacon_should_render(&beacon, 80))
                  == fbits(0.05F)
              && fbits(gm_runtime_beacon_should_render(&beacon, 81))
                  == fbits(0.075F)
              && fbits(gm_runtime_beacon_should_render(&beacon, 84))
                  == fbits(0.025F),
          "Beacon render scale follows shouldBeamRender's exact ramp and gap decay");
    beacon.beacon_complete = 0;
    CHECK(gm_runtime_beacon_should_render(&beacon, 90) == 0.0F
              && beacon.beacon_render_counter == 84
              && fbits(beacon.beacon_render_scale) == fbits(0.025F),
          "incomplete Beacon skips render counter and scale mutation");

    CHECK(gm_runtime_use_block(&r, x, y, z)
              && r.container == 11
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 388, 1, 0)
              && gm_runtime_beacon_confirm(&r, 11, 10)
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && isr_is_empty(&beacon.slots[0])
              && beacon.beacon_primary == 11
              && beacon.beacon_secondary == 10,
          "server payload consumes one payment and stores both effects");

    CHECK(gm_runtime_beacon_set_state(&r, 0, x, y, z, 4, 1, 0, 1),
          "prepare four-level Beacon GUI state");
    isr_set_stack(&r.player.inv, 9, ic_mk(264, 4, 0));
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_QUICK_MOVE)
              && isr_get_stack(&r.player.inv, 9).count == 3
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && beacon.slots[0].item == 264
              && beacon.slots[0].count == 1,
          "Beacon shift-click moves exactly one payment");
    CHECK(gm_container_click(
              &r, GMC_BEACON_POWER0 + 4, 0, CC_CLICK_PICKUP)
              && gm_container_click(
                  &r, GMC_BEACON_POWER0 + 6, 0, CC_CLICK_PICKUP)
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && beacon.beacon_primary == 5
              && beacon.beacon_secondary == 5,
          "Beacon primary and level-II controls update client fields");
    CHECK(gm_container_click(
              &r, GMC_BEACON_CONFIRM, 0, CC_CLICK_PICKUP)
              && r.container == 0
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && isr_is_empty(&beacon.slots[0])
              && beacon.beacon_primary == 5
              && beacon.beacon_secondary == 5,
          "Beacon Done consumes payment and closes the container");

    CHECK(gm_runtime_use_block(&r, x, y, z), "reopen Beacon for Cancel");
    isr_set_stack(&r.player.inv, 10, ic_mk(388, 1, 0));
    CHECK(gm_container_click(&r, 10, 0, CC_CLICK_QUICK_MOVE)
              && gm_container_click(
                  &r, GMC_BEACON_CANCEL, 0, CC_CLICK_PICKUP)
              && r.container == 0
              && gm_runtime_beacon_get(&r, 0, x, y, z, &beacon)
              && isr_is_empty(&beacon.slots[0]),
          "Beacon Cancel closes and drops its transient payment");

    CHECK(gm_runtime_use_block(&r, x, y, z) && r.container == 11
              && gm_runtime_set_block(&r, x, y, z, 0, 0)
              && r.container == 0,
          "breaking an open Beacon closes container 11");

    gm_runtime_destroy(&r);
    puts("PASS Beacon runtime: pyramid, beam colors, effects, rendering, payment");
    return 0;
}
