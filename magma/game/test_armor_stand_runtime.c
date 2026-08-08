#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition, message) do {                                      \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL: %s\n", (message));                         \
        return 0;                                                           \
    }                                                                       \
} while (0)

static int init_runtime(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    return 1;
}

static int same_double(double left, double right) {
    uint64_t left_bits, right_bits;
    memcpy(&left_bits, &left, sizeof left_bits);
    memcpy(&right_bits, &right, sizeof right_bits);
    return left_bits == right_bits;
}

static int exact_state_and_view(void) {
    static const unsigned char leather_color[] = {
        10,0,0,
        10,0,7,'d','i','s','p','l','a','y',
        3,0,5,'c','o','l','o','r',0,0x10,0x20,0x30,
        0,0,
    };
    GmRuntime runtime;
    GmRuntimeArmorStand stand;
    GmEntityView view;
    const unsigned char *text;
    size_t text_length;
    CHECK(init_runtime(&runtime), "initialize exact armor-stand fixture");
    CHECK(runtime.armor_stands == NULL && runtime.armor_stands_cap == 0,
          "empty worlds do not allocate armor-stand storage");
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6101, 8.5, 100.0, 12.5,
              0.125, -0.25, 0.375, 67.5F, -11.25F, 19.5F,
              1, 1, 1,
              GM_ARMOR_STAND_SMALL | GM_ARMOR_STAND_SHOW_ARMS
                  | GM_ARMOR_STAND_NO_BASE_PLATE,
              0x10204, 37, 81, 1234567)
              && runtime.armor_stands != NULL
              && runtime.armor_stands_cap == GM_RUNTIME_ARMOR_STANDS_INITIAL
              && runtime.armor_stand_count == 1
              && !gm_runtime_spawn_armor_stand_fixture(
                  &runtime, 6101, 0.0, 70.0, 0.0,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
                  0, 0, 0, 0, 0, 0, 0, 0),
          "first stand cold-allocates storage and duplicate eids fail closed");
    CHECK(gm_runtime_armor_stand_set_uuid(
              &runtime, 6101, INT64_C(0x4675d88ca2a73c16),
              (int64_t)UINT64_C(0xbf9c70b5a7108798))
              && gm_runtime_armor_stand_set_random_state(
                  &runtime, 6101, UINT64_C(0x123456789abc), 1, -0.375)
              && gm_runtime_armor_stand_set_generic_state(
                  &runtime, 6101, 2.5F, 24.0F, 24.0F, 31, 7,
                  1, 1, 1, 1, 0, 0, -1)
              && gm_runtime_armor_stand_set_custom_name(
                  &runtime, 6101, "Sentinel")
              && gm_runtime_armor_stand_add_tag(
                  &runtime, 6101, "guard")
              && gm_runtime_armor_stand_add_tag(
                  &runtime, 6101, "west")
              && gm_runtime_armor_stand_add_tag(
                  &runtime, 6101, "guard")
              && gm_runtime_armor_stand_add_effect(
                  &runtime, 6101, 10, 1, 50, 0, 1),
          "restore exact armor-stand UUID and Java Random cursor");
    int dye_tag = gm_runtime_stack_tag_intern(
        &runtime, leather_color, sizeof leather_color);
    CHECK(dye_tag > 0, "intern complete leather armor NBT");
    for (int slot = 0; slot < GM_ARMOR_STAND_SLOTS; ++slot) {
        ICStack stack = ic_mk(
            (int[]){276, 442, 301, 300, 307, 310}[slot],
            slot + 1, slot * 3);
        if (slot == GM_ARMOR_STAND_FEET) stack.tag_id = dye_tag;
        CHECK(gm_runtime_armor_stand_set_equipment(
                  &runtime, 6101, slot, stack),
              "restore all six armor-stand equipment slots");
    }
    for (int part = 0; part < GM_ARMOR_STAND_POSE_PARTS; ++part)
        CHECK(gm_runtime_armor_stand_set_pose(
                  &runtime, 6101, part,
                  (float)(part * 10 + 1), (float)(part * -7 - 2),
                  (float)(part * 3 + 0.5)),
              "restore all six armor-stand pose rotations");
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.eid == 6101 && stand.dimension == 0
              && stand.uuid_present
              && stand.uuid_most == INT64_C(0x4675d88ca2a73c16)
              && (uint64_t)stand.uuid_least
                  == UINT64_C(0xbf9c70b5a7108798)
              && stand.random_seed48 == UINT64_C(0x123456789abc)
              && stand.random_have_gaussian
              && same_double(stand.random_gaussian, -0.375)
              && stand.absorption == 2.5F && stand.max_health == 24.0F
              && stand.revenge_timer == 31 && stand.portal_cooldown == 7
              && stand.custom_name_visible && stand.silent && stand.glowing
              && stand.invulnerable && !stand.update_blocked
              && !stand.fall_flying && stand.vehicle_eid == -1
              && stand.tag_count == 2 && stand.effect_count == 1
              && stand.effects[0].id == 10
              && stand.effects[0].duration == 50
              && gm_runtime_armor_stand_string(
                  &runtime, stand.custom_name_tag_id, &text, &text_length)
              && text_length == 8 && !memcmp(text, "Sentinel", 8)
              && gm_runtime_armor_stand_string(
                  &runtime, stand.tag_ids[1], &text, &text_length)
              && text_length == 4 && !memcmp(text, "west", 4)
              && stand.equipment[GM_ARMOR_STAND_MAINHAND].item == 276
              && stand.equipment[GM_ARMOR_STAND_HEAD].item == 310
              && stand.pose[GM_ARMOR_STAND_RIGHT_LEG_POSE].x == 51.0F
              && stand.pose[GM_ARMOR_STAND_RIGHT_LEG_POSE].y == -37.0F
              && stand.pose[GM_ARMOR_STAND_RIGHT_LEG_POSE].z == 15.5F,
          "all saved armor-stand fields round-trip exactly");
    CHECK(gm_runtime_armor_stand_views(&runtime, &view, 1) == 1
              && view.type == 34 && view.ent_id == 6101
              && view.armor_feet == 301 && view.armor_legs == 300
              && view.armor_chest == 307 && view.armor_head == 310
              && view.armor_feet_meta == 6 && view.armor_legs_meta == 9
              && view.armor_chest_meta == 12 && view.armor_head_meta == 15
              && view.armor_color_valid == 3
              && view.armor_color[0] == 0x102030
              && view.armor_color[1] == 0xa06540
              && view.stand_mainhand == 276 && view.stand_offhand == 442
              && view.stand_mainhand_meta == 0
              && view.stand_offhand_meta == 3
              && view.stand_flags == 7
              && view.flags == (4 | GM_ENTITY_FLAG_GLOWING)
              && view.stand_pose_valid
              && view.stand_pose[GM_ARMOR_STAND_RIGHT_LEG_POSE][0] == 51.0F
              && view.stand_pose[GM_ARMOR_STAND_RIGHT_LEG_POSE][1] == -37.0F
              && view.stand_pose[GM_ARMOR_STAND_RIGHT_LEG_POSE][2] == 15.5F
              && view.stand_punch_time_valid
              && view.stand_punch_time == -1234566.0F,
          "live stand exposes visibility, pose, equipment, and hit phase");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.ticks_existed == 38 && stand.fire_ticks == 80
              && stand.health == 20.5F && stand.portal_cooldown == 6
              && stand.effect_count == 1
              && stand.effects[0].duration == 49
              && same_double(stand.x, 8.5)
              && same_double(stand.y, 100.0)
              && same_double(stand.z, 12.5)
              && same_double(stand.vx, 0.125 * 0.98)
              && same_double(stand.vy, -0.25 * 0.98)
              && same_double(stand.vz, 0.375 * 0.98),
          "NoGravity tick stays fixed while client-world motion damps by .98");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int gravity_and_collision(void) {
    GmRuntime runtime;
    GmRuntimeArmorStand marker, grounded;
    CHECK(init_runtime(&runtime), "initialize armor-stand physics fixture");
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6201, 8.5, 100.0, 8.5,
              0.125, 0.25, -0.5, 0.0F, 0.0F, 20.0F,
              0, 0, 0, GM_ARMOR_STAND_MARKER, 0, 0, 0, 0)
              && gm_runtime_spawn_armor_stand_fixture(
                  &runtime, 6202, 20.5, 100.0, 20.5,
                  0.1, -0.1, 0.0, 0.0F, 0.0F, 20.0F,
                  1, 0, 0, 0, 0, 0, 0, 0),
          "stage marker and grounded armor stands");
    gm_world_set_block(runtime.world, 20, 99, 20, 1);
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &marker)
              && same_double(marker.x, 8.625)
              && same_double(marker.y, 100.25)
              && same_double(marker.z, 8.0)
              && same_double(marker.vx, 0.125 * (double)0.91F)
              && same_double(marker.vy,
                  (0.25 - 0.08) * 0.9800000190734863)
              && same_double(marker.vz, -0.5 * (double)0.91F)
              && !marker.on_ground,
          "marker stands retain their zero box but still travel and gravitate");
    CHECK(gm_runtime_armor_stand_get(&runtime, 1, &grounded)
              && same_double(grounded.x, 20.6)
              && same_double(grounded.y, 100.0)
              && same_double(grounded.z, 20.5)
              && grounded.on_ground
              && same_double(grounded.vx,
                  0.1 * (double)(0.6F * 0.91F))
              && same_double(grounded.vy,
                  -0.08 * 0.9800000190734863)
              && same_double(grounded.vz, 0.0),
          "ground collision zeroes downward motion and uses block slipperiness");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int generic_state_and_passenger(void) {
    GmRuntime runtime;
    GmRuntimeArmorStand stand;
    GmRuntimeMinecart cart;
    CHECK(init_runtime(&runtime), "initialize generic Armor Stand fixture");
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6251, 8.5, 100.0, 8.5,
              0.125, -0.25, 0.375, 0.0F, 0.0F, 12.0F,
              0, 0, 0, 0, 0, 17, -1, 0)
              && gm_runtime_armor_stand_set_generic_state(
                  &runtime, 6251, 2.0F, 24.0F, 24.0F, 11, 9,
                  1, 1, 1, 1, 1, 0, -1)
              && gm_runtime_armor_stand_add_effect(
                  &runtime, 6251, 10, 0, 50, 0, 1),
          "stage update-blocked generic state");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.ticks_existed == 18
              && stand.portal_cooldown == 9
              && stand.effects[0].duration == 50
              && stand.health == 12.0F
              && same_double(stand.x, 8.5)
              && same_double(stand.vx, 0.125),
          "UpdateBlocked increments World entity age but skips onUpdate state");
    CHECK(gm_runtime_armor_stand_damage_fixture(
              &runtime, 6251, GM_ARMOR_STAND_DAMAGE_PLAYER,
              1.0F, 1, 0)
              && runtime.armor_stand_count == 1
              && gm_runtime_sound_event_count(&runtime) == 0,
          "invulnerable silent stand ignores survival damage and sound");
    CHECK(gm_runtime_armor_stand_damage_fixture(
              &runtime, 6251, GM_ARMOR_STAND_DAMAGE_PLAYER,
              1.0F, 1, 1)
              && runtime.armor_stand_count == 0
              && gm_runtime_sound_event_count(&runtime) == 0,
          "creative damage bypasses invulnerability but Silent hides break audio");

    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6254, 10.5, 100.0, 10.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              0, 1, 0, 0, 0, 0, -1, 0)
              && gm_runtime_armor_stand_set_generic_state(
                  &runtime, 6254, 0.0F, 28.0F, 24.0F, 0, 0,
                  0, 0, 0, 0, 0, 0, -1)
              && gm_runtime_armor_stand_add_effect(
                  &runtime, 6254, 21, 0, 1, 0, 1),
          "stage one-tick Health Boost attribute modifier");
    for (int index = 0; index < runtime.armor_stands_cap; ++index)
        if (runtime.armor_stands[index].active
                && runtime.armor_stands[index].eid == 6254)
            runtime.armor_stands[index].health = 28.0F;
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.eid == 6254
              && stand.effect_count == 0
              && stand.max_health_base == 24.0F
              && stand.max_health == 24.0F
              && stand.health == 24.0F,
          "expiring Health Boost removes its modifier and clamps health");
    CHECK(gm_runtime_armor_stand_damage_fixture(
              &runtime, 6254, GM_ARMOR_STAND_DAMAGE_OUT_OF_WORLD,
              1.0F, 0, 0),
          "retire Health Boost fixture before passenger case");

    CHECK(gm_runtime_spawn_minecart_fixture(
              &runtime, GM_MINECART_RIDEABLE, 6252,
              20.0, 100.0, 20.0, 0.2, 0.0, 0.0, 0.0F)
              && gm_runtime_spawn_armor_stand_fixture(
                  &runtime, 6253, 20.0, 100.1, 20.0,
                  3.0, 4.0, 5.0, 0.0F, 0.0F, 20.0F,
                  0, 0, 0, 0, 0, 0, -1, 0)
              && gm_runtime_armor_stand_set_generic_state(
                  &runtime, 6253, 0.0F, 20.0F, 20.0F, 0, 0,
                  0, 0, 0, 0, 0, 0, 6252),
          "stage Armor Stand passenger on a moving minecart");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.ticks_existed == 0,
          "ordinary stand loop defers mounted entities to their vehicle");
    gm_runtime_tick_minecarts(&runtime);
    CHECK(gm_runtime_minecart_get(&runtime, 0, &cart)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.ticks_existed == 1
              && same_double(stand.x, cart.x)
              && same_double(stand.y,
                  cart.y + 0.10000000149011612)
              && same_double(stand.z, cart.z)
              && same_double(stand.vx, 0.0)
              && same_double(stand.vy,
                  -0.08 * 0.9800000190734863)
              && same_double(stand.vz, 0.0),
          "minecart recursively ticks, then repositions its Armor Stand passenger");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int placement_and_interaction(void) {
    GmRuntime runtime;
    GmRuntimeArmorStand stand;
    GmRuntimeSoundEvent sound;
    ICStack held;
    CHECK(init_runtime(&runtime), "initialize armor-stand placement fixture");
    gm_runtime_set_pose(&runtime, 2.5, 5.0, 2.5, 0.0F, 0.0F);
    gm_runtime_set_world_random_seed48(&runtime, UINT64_C(0x123456789abc));
    isr_set_stack(&runtime.player.inv, 0, ic_mk(416, 2, 0));
    runtime.player.inv.current_item = 0;
    CHECK(gm_runtime_place_armor_stand(
              &runtime, 8, 5, 8, 202.6F, 0, 0)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.x == 8.5 && stand.y == 5.0 && stand.z == 8.5
              && stand.yaw == 45.0F && stand.health == 20.0F
              && stand.uuid_present && stand.random_seed48 != 0
              && stand.pose[GM_ARMOR_STAND_HEAD_POSE].x > 0.0F
              && stand.pose[GM_ARMOR_STAND_HEAD_POSE].x < 5.0F
              && stand.pose[GM_ARMOR_STAND_HEAD_POSE].y >= -10.0F
              && stand.pose[GM_ARMOR_STAND_HEAD_POSE].y < 10.0F
              && stand.pose[GM_ARMOR_STAND_BODY_POSE].y >= -5.0F
              && stand.pose[GM_ARMOR_STAND_BODY_POSE].y < 5.0F
              && isr_get_stack(&runtime.player.inv, 0).count == 1
              && gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &sound)
              && sound.sound == GM_SOUND_ARMOR_STAND_PLACE
              && sound.category == GM_SOUND_CATEGORY_BLOCKS
              && sound.volume == 0.75F && sound.pitch == 0.8F,
          "placement snaps yaw, consumes three world floats, seeds identity, "
          "shrinks the item, and publishes exact sound");
    CHECK(!gm_runtime_place_armor_stand(
              &runtime, 8, 5, 8, 0.0F, 0, 0),
          "placement rejects an intersecting entity");
    isr_set_stack(&runtime.player.inv, 0, ic_mk(301, 3, 7));
    CHECK(gm_runtime_armor_stand_interact(
              &runtime, stand.eid, 0.2, 0, 0)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.equipment[GM_ARMOR_STAND_FEET].item == 301
              && stand.equipment[GM_ARMOR_STAND_FEET].count == 1
              && stand.equipment[GM_ARMOR_STAND_FEET].meta == 7
              && isr_get_stack(&runtime.player.inv, 0).count == 2
              && gm_runtime_sound_event_count(&runtime) == 2
              && gm_runtime_sound_event_get(&runtime, 1, &sound)
              && sound.sound == GM_SOUND_ITEM_ARMOR_EQUIP_LEATHER,
          "multi-count armor interaction inserts exactly one item and "
          "uses its material equip sound");
    isr_set_stack(&runtime.player.inv, 0, ic_empty());
    CHECK(gm_runtime_armor_stand_interact(
              &runtime, stand.eid, 0.2, 0, 0)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && isr_is_empty(&stand.equipment[GM_ARMOR_STAND_FEET])
              && (held = isr_get_stack(&runtime.player.inv, 0)).item == 301
              && held.count == 1 && held.meta == 7
              && gm_runtime_sound_event_count(&runtime) == 3
              && gm_runtime_sound_event_get(&runtime, 2, &sound)
              && sound.sound == GM_SOUND_ITEM_ARMOR_EQUIP_LEATHER
              && sound.category == GM_SOUND_CATEGORY_PLAYERS,
          "empty-hand removal equips the player and publishes its material sound");
    isr_set_stack(&runtime.player.inv, 0, ic_mk(276, 1, 0));
    CHECK(!gm_runtime_armor_stand_interact(
              &runtime, stand.eid, 1.0, 0, 0),
          "hidden arms reject hand equipment");
    isr_set_stack(&runtime.player.inv, 0, ic_mk(421, 1, 0));
    CHECK(!gm_runtime_armor_stand_interact(
              &runtime, stand.eid, 1.0, 0, 0),
          "name tags pass through to the generic name-tag route");
    isr_set_stack(&runtime.player.inv, 0, ic_mk(310, 2, 7));
    CHECK(gm_runtime_armor_stand_interact(
              &runtime, stand.eid, 1.8, 0, 0)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.equipment[GM_ARMOR_STAND_HEAD].item == 310
              && stand.equipment[GM_ARMOR_STAND_HEAD].count == 1
              && stand.equipment[GM_ARMOR_STAND_HEAD].meta == 7
              && isr_get_stack(&runtime.player.inv, 0).count == 1
              && gm_runtime_sound_event_count(&runtime) == 4
              && gm_runtime_sound_event_get(&runtime, 3, &sound)
              && sound.sound == GM_SOUND_ITEM_ARMOR_EQUIP_DIAMOND
              && sound.category == GM_SOUND_CATEGORY_NEUTRAL,
          "two-helmet Java row equips one and retains one with diamond audio");
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6402, 12.5, 5.0, 12.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              1, 0, 0, GM_ARMOR_STAND_SHOW_ARMS,
              1 << 4, 0, 0, 0)
              && !gm_runtime_armor_stand_interact(
                  &runtime, 6402, 1.8, 0, 0)
              && gm_runtime_armor_stand_get(&runtime, 1, &stand)
              && isr_is_empty(&stand.equipment[GM_ARMOR_STAND_HEAD])
              && isr_get_stack(&runtime.player.inv, 0).count == 1,
          "disabled head bit rejects the directly measured Java interaction");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int placement_rejects_at(GmRuntime *runtime, int x, int z) {
    ICStack held;
    isr_set_stack(&runtime->player.inv, 0, ic_mk(416, 64, 0));
    runtime->player.inv.current_item = 0;
    if (gm_runtime_place_armor_stand(
            runtime, x, 5, z, 0.0F, 0, 0))
        return 0;
    held = isr_get_stack(&runtime->player.inv, 0);
    return held.item == 416 && held.count == 64
        && runtime->armor_stand_count == 0;
}

static int placement_collision_matrix(void) {
    GmRuntime runtime;
    CHECK(init_runtime(&runtime), "initialize all-entity placement matrix");
    gm_runtime_set_pose(&runtime, 2.5, 5.0, 2.5, 0.0F, 0.0F);

    CHECK(gm_runtime_spawn_item_fixture(
              &runtime, 6801, 8.5, 5.0, 8.5,
              0.0, 0.0, 0.0, 1, 1, 0, 0, 10, 1)
              && placement_rejects_at(&runtime, 8, 8),
          "EntityItem blocks armor-stand placement");
    CHECK(gm_runtime_spawn_xp_fixture(
              &runtime, 12.5, 5.0, 8.5,
              0.0, 0.0, 0.0, 1, 6802, 0, 0, 0, 0)
              && placement_rejects_at(&runtime, 12, 8),
          "EntityXPOrb blocks armor-stand placement");
    CHECK(gm_runtime_spawn_arrow_fixture(
              &runtime, 6803, 16.5, 5.0, 8.5,
              0.0, 0.0, 0.0, 1, 0)
              && placement_rejects_at(&runtime, 16, 8),
          "projectiles block armor-stand placement");
    CHECK(gm_runtime_spawn_falling_fixture(
              &runtime, 6804, 12, 0, 1,
              20.5, 5.0, 8.5, 0.0, 0.0, 0.0, 1, 1)
              && placement_rejects_at(&runtime, 20, 8),
          "EntityFallingBlock blocks armor-stand placement");
    CHECK(gm_runtime_spawn_primed_tnt_fixture(
              &runtime, 6805, 24.5, 5.0, 8.5,
              0.0, 0.0, 0.0, 80)
              && placement_rejects_at(&runtime, 24, 8),
          "EntityTNTPrimed blocks armor-stand placement");
    CHECK(gm_runtime_spawn_end_crystal_fixture(
              &runtime, 6806, 28.5, 5.0, 8.5,
              0, 1, 0, 0, 0, 0)
              && placement_rejects_at(&runtime, 28, 8),
          "EntityEnderCrystal blocks armor-stand placement");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &runtime, GM_MINECART_RIDEABLE, 6807,
              8.5, 5.0, 12.5, 0.0, 0.0, 0.0, 0.0F)
              && placement_rejects_at(&runtime, 8, 12),
          "EntityMinecart blocks armor-stand placement");
    CHECK(gm_runtime_spawn_wither_fixture(
              &runtime, 6808, 12.5, 5.0, 12.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
              300.0F, 0, 0, 0, 0, 0, 0,
              UINT64_C(0x123456789abc), 0, 0.0)
              && placement_rejects_at(&runtime, 12, 12),
          "EntityWither blocks armor-stand placement");
    CHECK(gm_runtime_spawn_area_effect_cloud_fixture(
              &runtime, 6809, 0, 16.5, 5.0, 12.5, /* TB_PT_EMPTY */
              0, 600, 10, 20, 3.0F, -0.5F, -0.005F, 0)
              && placement_rejects_at(&runtime, 16, 12),
          "EntityAreaEffectCloud blocks armor-stand placement");
    CHECK(gm_runtime_spawn_firework_payload(
              &runtime, 20.5, 5.0, 12.5, 1, 1, 0, 0, 0) > 0
              && placement_rejects_at(&runtime, 20, 12),
          "EntityFireworkRocket blocks armor-stand placement");
    CHECK(gm_runtime_spawn_fish_hook_fixture(
              &runtime, 6811, 24.5, 5.0, 12.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F,
              0, 0, 0, 0, 0, 0, 0, 0.0F, 0, 0, 0,
              UINT64_C(0x123456789abc), 0, 0.0)
              && placement_rejects_at(&runtime, 24, 12),
          "EntityFishHook blocks armor-stand placement");
    CHECK(gm_runtime_spawn_shulker_fixture(
              &runtime, 6812, 28, 5, 12, 0,
              UINT64_C(0x123456789abc))
              && placement_rejects_at(&runtime, 28, 12),
          "EntityShulker blocks armor-stand placement");
    CHECK(gm_runtime_spawn_shulker_bullet_state_fixture(
              &runtime, 6813, 6812, 1, 20, 0,
              8.5, 5.0, 16.5, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0.0F, 0.0F,
              UINT64_C(0x123456789abc))
              && placement_rejects_at(&runtime, 8, 16),
          "EntityShulkerBullet blocks armor-stand placement");
    gm_world_set_block(runtime.world, 11, 5, 16, 1);
    CHECK(gm_runtime_item_frame_set(
              &runtime, 0, 6814, 12.03125, 5.5, 16.5,
              12, 5, 16, 5, 0, 0, 0, 0)
              && placement_rejects_at(&runtime, 12, 16),
          "EntityItemFrame blocks armor-stand placement");
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_COW, 6815, 16.5, 5.0, 16.5,
              0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0)
              && placement_rejects_at(&runtime, 16, 16),
          "living entities block armor-stand placement");
    CHECK(gm_runtime_spawn_boat_fixture(
              &runtime, 6816, 20.5, 5.0, 16.5, 0.0F)
              && placement_rejects_at(&runtime, 20, 16),
          "EntityBoat blocks armor-stand placement");
    runtime.mobs.evoker_fangs = calloc(1, sizeof *runtime.mobs.evoker_fangs);
    CHECK(runtime.mobs.evoker_fangs,
          "allocate the evoker-fang placement fixture");
    runtime.mobs.evoker_fangs_cap = 1;
    runtime.mobs.evoker_fang_count = 1;
    runtime.mobs.evoker_fangs[0] = (GmEvokerFang){
        .active = 1, .eid = 6817, .dimension = 0,
        .x = 24.5, .y = 5.0, .z = 16.5
    };
    CHECK(placement_rejects_at(&runtime, 24, 16),
          "EntityEvokerFangs blocks armor-stand placement");

    gm_runtime_destroy(&runtime);
    return 1;
}

static int entity_tag_placement(void) {
    static const unsigned char item_tag[] = {
        10,0,0,
        10,0,9,'E','n','t','i','t','y','T','a','g',
          1,0,5,'S','m','a','l','l',1,
          1,0,8,'S','h','o','w','A','r','m','s',1,
          1,0,11,'N','o','B','a','s','e','P','l','a','t','e',1,
          1,0,9,'N','o','G','r','a','v','i','t','y',1,
          1,0,9,'I','n','v','i','s','i','b','l','e',1,
          3,0,13,'D','i','s','a','b','l','e','d','S','l','o','t','s',
            0,1,0,2,
          9,0,6,'M','o','t','i','o','n',6,0,0,0,3,
            0x3f,0xd0,0,0,0,0,0,0,
            0xbf,0xe0,0,0,0,0,0,0,
            0x3f,0xc0,0,0,0,0,0,0,
          9,0,8,'R','o','t','a','t','i','o','n',5,0,0,0,2,
            0x42,0xb4,0,0,0x41,0x20,0,0,
          5,0,6,'H','e','a','l','t','h',0x40,0xf0,0,0,
          2,0,3,'A','i','r',0,123,
          2,0,4,'F','i','r','e',0,40,
          9,0,10,'A','r','m','o','r','I','t','e','m','s',10,0,0,0,4,
            8,0,2,'i','d',0,23,
              'm','i','n','e','c','r','a','f','t',':',
              'd','i','a','m','o','n','d','_','b','o','o','t','s',
            1,0,5,'C','o','u','n','t',2,
            2,0,6,'D','a','m','a','g','e',0,7,
            10,0,3,'t','a','g',
              3,0,10,'R','e','p','a','i','r','C','o','s','t',0,0,0,9,
              10,0,7,'d','i','s','p','l','a','y',
                8,0,4,'N','a','m','e',0,6,'T','a','g','g','e','d',0,
              9,0,4,'e','n','c','h',10,0,0,0,1,
                2,0,2,'i','d',0,0,
                2,0,3,'l','v','l',0,3,
                0,
              0,
            0,
            0,
            0,
            0,
          10,0,4,'P','o','s','e',
            9,0,8,'R','i','g','h','t','A','r','m',5,0,0,0,3,
              0x41,0x48,0,0,0xc0,0,0,0,0x40,0x50,0,0,
            0,
          0,
        0,
    };
    GmRuntime runtime;
    GmRuntimeArmorStand stand;
    ICStack held = ic_mk(416, 2, 0);
    int tag_id;
    CHECK(init_runtime(&runtime), "initialize item EntityTag placement fixture");
    gm_runtime_set_pose(&runtime, 2.5, 5.0, 2.5, 0.0F, 0.0F);
    tag_id = gm_runtime_stack_tag_intern(
        &runtime, item_tag, sizeof item_tag);
    held.tag_id = tag_id;
    isr_set_stack(&runtime.player.inv, 0, held);
    runtime.player.inv.current_item = 0;
    CHECK(tag_id > 0
              && gm_runtime_place_armor_stand(
                  &runtime, 8, 5, 8, 0.0F, 0, 0)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.status == (GM_ARMOR_STAND_SMALL
                  | GM_ARMOR_STAND_SHOW_ARMS
                  | GM_ARMOR_STAND_NO_BASE_PLATE)
              && stand.no_gravity && stand.invisible
              && stand.disabled_slots == 0x10002
              && stand.vx == 0.25 && stand.vy == -0.5
              && stand.vz == 0.125
              && stand.yaw == 90.0F && stand.pitch == 10.0F
              && stand.health == 7.5F && stand.air == 123
              && stand.fire_ticks == 40
              && stand.pose[GM_ARMOR_STAND_RIGHT_ARM_POSE].x == 12.5F
              && stand.pose[GM_ARMOR_STAND_RIGHT_ARM_POSE].y == -2.0F
              && stand.pose[GM_ARMOR_STAND_RIGHT_ARM_POSE].z == 3.25F
              && stand.equipment[GM_ARMOR_STAND_FEET].item == 313
              && stand.equipment[GM_ARMOR_STAND_FEET].count == 2
              && stand.equipment[GM_ARMOR_STAND_FEET].meta == 7
              && stand.equipment[GM_ARMOR_STAND_FEET].repair_cost == 9
              && stand.equipment[GM_ARMOR_STAND_FEET].custom_name > 0
              && stand.equipment[GM_ARMOR_STAND_FEET].tag_id > 0
              && stand.equipment[GM_ARMOR_STAND_FEET].n_enchants == 1
              && stand.equipment[GM_ARMOR_STAND_FEET].enchants[0].id == 0
              && stand.equipment[GM_ARMOR_STAND_FEET].enchants[0].level == 3
              && isr_get_stack(&runtime.player.inv, 0).count == 1
              && isr_get_stack(&runtime.player.inv, 0).tag_id == tag_id,
          "placement merges represented EntityTag base/living/pose/equipment "
          "fields while preserving arbitrary nested stack NBT");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int live_tick_routes(void) {
    GmRuntime runtime;
    GmRuntimeArmorStand stand;
    GmRuntimeSoundEvent sound;
    GmAction idle = {0}, use = {0}, attack = {0};
    idle.hotbar_sel = use.hotbar_sel = attack.hotbar_sel = -1;
    use.use = use.do_place = 1;
    attack.attack = attack.do_break = 1;

    CHECK(init_runtime(&runtime), "initialize live interaction route");
    gm_runtime_set_total_time(&runtime, 100);
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 6.5, 0.0F, 35.0F);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6601, 8.5, 5.0, 8.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              1, 0, 0, GM_ARMOR_STAND_SHOW_ARMS,
              0, 0, 0, 0),
          "stage a stand on the client entity ray");
    isr_set_stack(&runtime.player.inv, 0, ic_mk(276, 1, 19));
    runtime.player.inv.current_item = 0;
    gm_runtime_tick(&runtime, use);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.equipment[GM_ARMOR_STAND_MAINHAND].item == 276
              && stand.equipment[GM_ARMOR_STAND_MAINHAND].meta == 19
              && isr_get_stack(&runtime.player.inv, 0).count <= 0,
          "public right-click ray equips the live stand");
    gm_runtime_tick(&runtime, attack);
    CHECK(runtime.server_attack_pending,
          "public attack edge queues the integrated-server entity packet");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.punch_cooldown == runtime.clock.total_time - 1
              && gm_runtime_sound_event_count(&runtime) >= 2
              && gm_runtime_sound_event_get(
                  &runtime, gm_runtime_sound_event_count(&runtime) - 2,
                  &sound)
              && sound.sound == GM_SOUND_ARMOR_STAND_HIT
              && gm_runtime_sound_event_get(
                  &runtime, gm_runtime_sound_event_count(&runtime) - 1,
                  &sound)
              && sound.sound == GM_SOUND_PLAYER_ATTACK_NODAMAGE,
          "delayed CPacketUseEntity takes the first-punch and no-damage path");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize live arrow route");
    gm_runtime_set_pose(&runtime, 30.5, 5.0, 30.5, 0.0F, 0.0F);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6602, 8.5, 5.0, 8.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              1, 0, 0, 0, 0, 0, 0, 0)
              && gm_runtime_spawn_player_arrow_state_fixture(
                  &runtime, 6603, 8.5, 6.0, 6.5,
                  0.0, 0.0, 2.0, 0.0F, 0.0F,
                  0, -1, 2.0, 0, 0, 1, 0, 0, 0,
                  -1, -1, -1, 0, 0, UINT64_C(0x123456789abc),
                  0, 0.0),
          "stage a flying arrow across the stand collision box");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.armor_stand_count == 0
              && !runtime.projectiles[0].active
              && runtime.entities.n_active == 1
              && runtime.entities.ents[0].item == 416,
          "ordinary player-arrow tick kills both arrow and stand and drops it");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize live explosion route");
    gm_runtime_set_pose(&runtime, 30.5, 5.0, 30.5, 0.0F, 0.0F);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6604, 3.5, 5.0, 0.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              1, 0, 0, 0, 0, 0, 0, 0)
              && gm_runtime_armor_stand_set_equipment(
                  &runtime, 6604, GM_ARMOR_STAND_HEAD,
                  ic_mk(310, 1, 27))
              && gm_runtime_spawn_primed_tnt_fixture(
                  &runtime, 6605, 0.5, 5.0, 0.5,
                  0.0, 0.0, 0.0, 1),
          "stage a one-tick TNT explosion near an equipped stand");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.armor_stand_count == 0
              && runtime.entities.n_active == 1
              && runtime.entities.ents[0].item == 310
              && runtime.entities.ents[0].meta == 27,
          "live explosion drops equipment but not the armor-stand item");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int nearest_attack_ordering(void) {
    GmRuntime runtime;
    GmRuntimeArmorStand stand;
    GmRuntimeItemFrame frame;
    GmRuntimeMinecart cart;
    GmAction idle = {0}, attack = {0};
    const EwStore *store;
    int slot;
    idle.hotbar_sel = attack.hotbar_sel = -1;
    attack.attack = attack.do_break = 1;

    CHECK(init_runtime(&runtime), "initialize frame-versus-stand attack order");
    gm_runtime_set_total_time(&runtime, 100);
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 5.5, 0.0F, 20.0F);
    gm_world_set_block(runtime.world, 8, 6, 10, 1);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6901, 8.5, 5.0, 7.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              1, 0, 0, 0, 0, 0, 0, 0)
              && gm_runtime_item_frame_set(
                  &runtime, 0, 6902, 8.5, 6.5, 9.96875,
                  8, 6, 9, 2, 0, 0, 0, 0),
          "stage a near stand in front of a farther item frame");
    gm_runtime_tick(&runtime, attack);
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.punch_cooldown > 0
              && gm_runtime_item_frame_get(&runtime, 0, &frame)
              && frame.eid == 6902,
          "nearest stand receives attack instead of earlier-store frame");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize mob-versus-stand attack order");
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 5.5, 0.0F, 25.0F);
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, EW_TYPE_COW, 6903, 8.5, 5.0, 7.5,
              0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0)
              && gm_runtime_spawn_armor_stand_fixture(
                  &runtime, 6904, 8.5, 5.0, 8.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
                  1, 0, 0, 0, 0, 0, 0, 0),
          "stage a near Cow in front of a farther stand");
    gm_runtime_tick(&runtime, attack);
    gm_runtime_tick(&runtime, idle);
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 6903);
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(slot > 0 && store->health[slot] < 10.0F
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.punch_cooldown == 0,
          "nearest ordinary mob receives attack instead of earlier-store stand");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize stand-versus-mob attack order");
    gm_runtime_set_total_time(&runtime, 100);
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 5.5, 0.0F, 20.0F);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6905, 8.5, 5.0, 7.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              1, 0, 0, 0, 0, 0, 0, 0)
              && gm_runtime_spawn_mob_fixture(
                  &runtime, EW_TYPE_COW, 6906, 8.5, 5.0, 8.5,
                  0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0),
          "stage a near stand in front of a farther Cow");
    gm_runtime_tick(&runtime, attack);
    gm_runtime_tick(&runtime, idle);
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 6906);
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(slot > 0 && store->health[slot] == 10.0F
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.punch_cooldown > 0,
          "nearest stand receives attack instead of later-store mob");
    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize minecart-versus-stand attack order");
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 5.5, 0.0F, 25.0F);
    CHECK(gm_runtime_spawn_minecart_fixture(
              &runtime, GM_MINECART_RIDEABLE, 6907,
              8.5, 5.0, 7.5, 0.0, 0.0, 0.0, 0.0F)
              && gm_runtime_spawn_armor_stand_fixture(
                  &runtime, 6908, 8.5, 5.0, 8.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
                  1, 0, 0, 0, 0, 0, 0, 0),
          "stage a near minecart in front of a farther stand");
    gm_runtime_tick(&runtime, attack);
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_minecart_get(&runtime, 0, &cart)
              && cart.rolling_amplitude > 0
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.punch_cooldown == 0,
          "nearest minecart receives attack instead of earlier-store stand");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int environment_tick(void) {
    GmRuntime runtime;
    GmRuntimeArmorStand stand;
    GmRuntimeParticleEvent particle;
    GmRuntimeSoundEvent sound;
    GmRuntimeMinecart cart;
    uint64_t random_before;
    CHECK(init_runtime(&runtime), "initialize armor-stand environment fixture");

    gm_world_set_block(runtime.world, 8, 5, 8, 51);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6701, 8.5, 5.0, 8.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              1, 0, 0, 0, 0, 0, -1, 0),
          "stage stand inside fire");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.fire_ticks == 101 && stand.health == 20.0F,
          "first fire contact sets five seconds then applies move-tail increment");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.fire_ticks == 101
              && stand.health > 19.84F && stand.health < 19.86F,
          "continuous fire contact applies repeated 0.15 armor-stand damage");

    gm_world_set_block(runtime.world, 12, 5, 12, 9);
    gm_world_set_block(runtime.world, 12, 6, 12, 9);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6702, 12.5, 5.0, 12.5,
              0.25, -0.5, 0.125, 0.0F, 0.0F, 20.0F,
              0, 1, 0, 0, 0, 0, 100, 0)
              && gm_runtime_armor_stand_set_living_state(
                  &runtime, 6702, -19, 0, 7.0F, 0, 0, 0, 0.0F)
              && gm_runtime_armor_stand_get(&runtime, 1, &stand),
          "stage submerged NoGravity stand at drowning boundary");
    random_before = stand.random_seed48;
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 1, &stand)
              && stand.in_water && stand.fire_ticks == 0
              && stand.air == 0 && stand.fall_distance == 0.0F
              && same_double(stand.vx, 0.25 * 0.98)
              && same_double(stand.vy, -0.5 * 0.98)
              && same_double(stand.vz, 0.125 * 0.98)
              && stand.random_seed48 != random_before
              && gm_runtime_particle_event_count(&runtime) == 8,
          "water extinguishes, clears fall distance, advances drowning RNG, "
          "and emits eight bubbles without harming the stand");

    gm_world_set_block(runtime.world, 16, 5, 16, 11);
    gm_world_set_block(runtime.world, 16, 6, 16, 11);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6703, 16.5, 5.0, 16.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              0, 1, 0, 0, 0, 0, -1, 0)
              && gm_runtime_armor_stand_set_living_state(
                  &runtime, 6703, 300, 0, 8.0F, 0, 0, 0, 0.0F),
          "stage NoGravity stand in lava");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 2, &stand)
              && stand.fire_ticks == 300 && stand.fall_distance == 4.0F
              && stand.health == 20.0F,
          "lava sets fifteen seconds of fire and halves fall distance, "
          "while its unsupported damage source is ignored");

    gm_world_set_block(runtime.world, 20, 4, 20, 1);
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6704, 20.5, 5.5, 20.5,
              0.0, -1.0, 0.0, 0.0F, 0.0F, 20.0F,
              0, 0, 0, 0, 0, 0, -1, 0)
              && gm_runtime_armor_stand_set_living_state(
                  &runtime, 6704, 300, 0, 7.0F, 0, 0, 0, 0.0F),
          "stage damaging-distance landing");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_armor_stand_get(&runtime, 3, &stand)
              && stand.on_ground && stand.fall_distance == 0.0F
              && stand.health == 20.0F
              && gm_runtime_particle_event_count(&runtime) == 9
              && gm_runtime_particle_event_get(&runtime, 8, &particle)
              && particle.kind == GM_PARTICLE_BLOCK_DUST
              && particle.count == 70
              && particle.parameter_count == 1
              && particle.parameters[0] == 1
              && gm_runtime_sound_event_count(&runtime) >= 2
              && gm_runtime_sound_event_get(
                  &runtime, gm_runtime_sound_event_count(&runtime) - 2,
                  &sound)
              && sound.sound == GM_SOUND_ARMOR_STAND_FALL
              && gm_runtime_sound_event_get(
                  &runtime, gm_runtime_sound_event_count(&runtime) - 1,
                  &sound)
              && sound.sound == GM_SOUND_BLOCK_STONE_FALL,
          "landing emits exact stand/block audio and block-dust count but "
          "fall damage remains an ignored source");

    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6705, 24.5, -65.0, 24.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              0, 0, 0, 0, 0, 0, -1, 0),
          "stage out-of-world stand");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(runtime.armor_stand_count == 4,
          "base tick retires a stand below y=-64 without drops");

    CHECK(gm_runtime_spawn_minecart_fixture(
              &runtime, GM_MINECART_RIDEABLE, 6706,
              28.2, 5.0, 28.5, 0.0, 0.0, 0.0, 0.0F)
              && gm_runtime_spawn_armor_stand_fixture(
                  &runtime, 6707, 28.5, 5.0, 28.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
                  1, 0, 0, 0, 0, 0, -1, 0),
          "stage rideable-minecart collision inside stand box");
    gm_runtime_tick_armor_stands(&runtime);
    CHECK(gm_runtime_minecart_get(&runtime, 0, &cart)
              && cart.vx < -0.049999 && cart.vx > -0.050001
              && gm_runtime_armor_stand_get(&runtime, 4, &stand)
              && stand.vx > 0.012499 && stand.vx < 0.012501,
          "stand nearby-entity pass pushes only the rideable minecart and "
          "receives one-quarter reaction velocity");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int damage_drop_and_events(void) {
    static const unsigned char named_tag[] = {
        10,0,0,10,0,7,'d','i','s','p','l','a','y',
        8,0,4,'N','a','m','e',0,5,'B','o','o','t','s',0,0,
    };
    GmRuntime runtime;
    GmRuntimeArmorStand stand;
    GmRuntimeSoundEvent sound;
    GmRuntimeParticleEvent particle;
    ICStack held;
    int tag_id, stand_eid = 6501;
    CHECK(init_runtime(&runtime), "initialize armor-stand damage fixture");
    gm_runtime_set_total_time(&runtime, 100);
    tag_id = gm_runtime_stack_tag_intern(
        &runtime, named_tag, sizeof named_tag);
    CHECK(tag_id > 0
              && gm_runtime_spawn_armor_stand_fixture(
                  &runtime, stand_eid, 8.5, 5.0, 8.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
                  1, 0, 0, GM_ARMOR_STAND_SHOW_ARMS,
                  0, 0, 0, 0),
          "stage damageable equipped armor stand");
    held = ic_mk(301, 2, 17);
    held.tag_id = tag_id;
    CHECK(gm_runtime_armor_stand_set_equipment(
              &runtime, stand_eid, GM_ARMOR_STAND_FEET, held),
          "equip complete tagged stack for destruction");
    CHECK(gm_runtime_armor_stand_damage_fixture(
              &runtime, stand_eid, GM_ARMOR_STAND_DAMAGE_PLAYER,
              1.0F, 1, 0)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.punch_cooldown == 100
              && runtime.entities.n_active == 0
              && gm_runtime_sound_event_count(&runtime) == 1
              && gm_runtime_sound_event_get(&runtime, 0, &sound)
              && sound.sound == GM_SOUND_ARMOR_STAND_HIT
              && sound.volume == 0.3F,
          "first survival punch only publishes status-32 hit feedback");
    gm_runtime_set_total_time(&runtime, 103);
    CHECK(gm_runtime_armor_stand_damage_fixture(
              &runtime, stand_eid, GM_ARMOR_STAND_DAMAGE_PLAYER,
              1.0F, 1, 0)
              && runtime.armor_stand_count == 0
              && runtime.entities.n_active == 2
              && runtime.entities.ents[0].item == 416
              && runtime.entities.ents[0].count == 1
              && runtime.entities.ents[1].item == 301
              && runtime.entities.ents[1].count == 2
              && runtime.entities.ents[1].meta == 17
              && runtime.entities.ents[1].tag_id == tag_id
              && gm_runtime_particle_event_count(&runtime) == 1
              && gm_runtime_particle_event_get(&runtime, 0, &particle)
              && particle.kind == GM_PARTICLE_BLOCK_DUST
              && particle.count == 10
              && particle.offset_x == (double)0.125F
              && particle.offset_y == (double)(1.975F / 4.0F)
              && particle.offset_z == (double)0.125F
              && particle.speed == 0.05
              && particle.parameter_count == 1
              && particle.parameters[0] == 5
              && gm_runtime_sound_event_count(&runtime) == 2
              && gm_runtime_sound_event_get(&runtime, 1, &sound)
              && sound.sound == GM_SOUND_ARMOR_STAND_BREAK,
          "second punch drops stand and byte-complete equipment, then emits "
          "break sound and oak-plank dust");
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6502, 12.5, 5.0, 12.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 4.4F,
              1, 0, 0, 0, 0, 0, 0, 0)
              && gm_runtime_armor_stand_damage_fixture(
                  &runtime, 6502, GM_ARMOR_STAND_DAMAGE_IN_FIRE,
                  1.0F, 1, 0)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.fire_ticks == 100 && stand.health == 4.4F
              && gm_runtime_armor_stand_damage_fixture(
                  &runtime, 6502, GM_ARMOR_STAND_DAMAGE_IN_FIRE,
                  1.0F, 1, 0)
              && gm_runtime_armor_stand_get(&runtime, 0, &stand)
              && stand.health > 4.24F && stand.health < 4.26F
              && gm_runtime_armor_stand_damage_fixture(
                  &runtime, 6502, GM_ARMOR_STAND_DAMAGE_ON_FIRE,
                  4.0F, 1, 0)
              && runtime.armor_stand_count == 0,
          "in-fire ignition, repeated 0.15 damage, and on-fire lethal damage match");
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6503, 16.5, 5.0, 16.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F, 20.0F,
              1, 0, 0, GM_ARMOR_STAND_MARKER, 0, 0, 0, 0)
              && gm_runtime_armor_stand_damage_fixture(
                  &runtime, 6503, GM_ARMOR_STAND_DAMAGE_EXPLOSION,
                  100.0F, 1, 0)
              && runtime.armor_stand_count == 1
              && gm_runtime_armor_stand_damage_fixture(
                  &runtime, 6503, GM_ARMOR_STAND_DAMAGE_OUT_OF_WORLD,
                  100.0F, 1, 0)
              && runtime.armor_stand_count == 0,
          "Marker ignores normal damage but out-of-world retires without drops");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int checkpoint_continuation(void) {
    GmRuntime runtime;
    GmRuntimeArmorStand expected[2], actual[2];
    GmAction idle;
    char path[160];
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    (void)mkdir(".tmp", 0700);
    snprintf(path, sizeof path,
             ".tmp/test_armor_stand_runtime_checkpoint.%ld.bin",
             (long)getpid());
    CHECK(init_runtime(&runtime), "initialize armor-stand checkpoint fixture");
    CHECK(gm_runtime_spawn_armor_stand_fixture(
              &runtime, 6301, 10.5, 104.0, 10.5,
              0.0625, -0.125, 0.03125, 45.0F, 2.0F, 20.0F,
              0, 0, 0, GM_ARMOR_STAND_SHOW_ARMS,
              0x500, 12, 0, 45)
              && gm_runtime_spawn_armor_stand_fixture(
                  &runtime, 6302, 12.5, 106.0, 12.5,
                  -0.25, 0.5, 0.125, -90.0F, 0.0F, 7.5F,
                  0, 1, 1, GM_ARMOR_STAND_SMALL,
                  0x20000, 99, 17, 81)
              && gm_runtime_armor_stand_set_equipment(
                  &runtime, 6301, GM_ARMOR_STAND_CHEST, ic_mk(311, 1, 73))
              && gm_runtime_armor_stand_set_pose(
                  &runtime, 6302, GM_ARMOR_STAND_BODY_POSE,
                  1.25F, -2.5F, 3.75F)
              && gm_runtime_armor_stand_set_random_state(
                  &runtime, 6301, UINT64_C(0x456789abcdef), 1, 0.625)
              && gm_runtime_armor_stand_set_generic_state(
                  &runtime, 6301, 3.0F, 24.0F, 24.0F, 17, 11,
                  1, 1, 1, 1, 0, 0, -1)
              && gm_runtime_armor_stand_set_custom_name(
                  &runtime, 6301, "Checkpoint")
              && gm_runtime_armor_stand_add_tag(
                  &runtime, 6301, "persisted")
              && gm_runtime_armor_stand_add_effect(
                  &runtime, 6301, 10, 0, 80, 1, 0)
              && gm_runtime_write_checkpoint(&runtime, path),
          "write complete armor-stand lifecycle checkpoint");
    for (int tick = 0; tick < 24; ++tick) gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &expected[0])
              && gm_runtime_armor_stand_get(&runtime, 1, &expected[1]),
          "capture uninterrupted armor-stand continuation");
    CHECK(gm_runtime_load_checkpoint(&runtime, path),
          "reload armor-stand lifecycle checkpoint");
    for (int tick = 0; tick < 24; ++tick) gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_armor_stand_get(&runtime, 0, &actual[0])
              && gm_runtime_armor_stand_get(&runtime, 1, &actual[1])
              && memcmp(expected, actual, sizeof expected) == 0
              && runtime.loaded_entity_order_count == 2
              && runtime.loaded_entity_order[0] == 6301
              && runtime.loaded_entity_order[1] == 6302,
          "checkpoint resumes every stand byte and causal order exactly");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
    return 1;
}

int main(void) {
    if (!exact_state_and_view()
            || !gravity_and_collision()
            || !generic_state_and_passenger()
            || !placement_and_interaction()
            || !placement_collision_matrix()
            || !entity_tag_placement()
            || !damage_drop_and_events()
            || !live_tick_routes()
            || !nearest_attack_ordering()
            || !environment_tick()
            || !checkpoint_continuation())
        return 1;
    puts("PASS armor stand runtime: cold/generic state, flags, equipment, "
         "pose, passenger, NoGravity/Marker physics, placement, "
         "interaction, damage/drop, "
         "all-entity collision, globally ordered attack/arrow/explosion routes, "
         "environment, events, "
         "checkpoint continuation");
    return 0;
}
