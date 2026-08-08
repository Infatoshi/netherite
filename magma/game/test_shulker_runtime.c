#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 0; } } while (0)

static int init_runtime(GmRuntime *r) {
    GmConfig cfg;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    if (!gm_runtime_init(r, &cfg, err, sizeof err)) {
        fprintf(stderr, "FAIL: %s\n", err);
        return 0;
    }
    return 1;
}

static int has_item(const GmRuntime *r, int item) {
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (r->entities.ents[i].active
                && r->entities.ents[i].item == item)
            return 1;
    return 0;
}

static int test_damage_and_drop(void) {
    GmRuntime r;
    GmRuntimeShulker shulker;
    GmRuntimeSoundEvent sound;
    CHECK(init_runtime(&r), "initialize damage runtime");
    gm_world_set_block(r.world, 0, 63, 0, 201);
    CHECK(gm_runtime_spawn_shulker_fixture(&r, 101, 0, 64, 0, 0, 1),
          "spawn damage shulker");
    CHECK(!gm_runtime_shulker_damage_fixture(&r, 101, 4.0F, 1),
          "closed shell rejects arrows");
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && shulker.health == 30.0F,
          "rejected arrow does not change health");
    CHECK(gm_runtime_shulker_damage_fixture(&r, 101, 4.0F, 0),
          "closed shell accepts melee damage");
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && fabsf(shulker.health - 28.88F) < 0.0001F,
          "closed shell applies vanilla +20 armor reduction");
    r.shulkers[0].hurt_resistant_time = 0;
    r.shulkers[0].peek_tick = 100;
    CHECK(gm_runtime_shulker_damage_fixture(&r, 101, 4.0F, 0),
          "open shell accepts melee damage");
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && fabsf(shulker.health - 24.88F) < 0.0001F,
          "open shell has no covered armor bonus");
    r.shulkers[0].hurt_resistant_time = 0;
    r.shulkers[0].random_seed48 = 0;
    CHECK(gm_runtime_shulker_damage_fixture(&r, 101, 100.0F, 0),
          "lethal player damage is accepted");
    for (int tick = 0; tick < 20; ++tick)
        gm_runtime_tick_shulkers_fixture(&r);
    CHECK(gm_runtime_shulker_count(&r) == 0,
          "dead shulker is removed after twenty death ticks");
    CHECK(has_item(&r, 450),
          "deterministic loot roll creates a collectible shulker shell");
    CHECK(gm_runtime_sound_event_count(&r) == 3
              && gm_runtime_sound_event_get(&r, 0, &sound)
              && sound.sound == GM_SOUND_SHULKER_HURT_CLOSED
              && gm_runtime_sound_event_get(&r, 1, &sound)
              && sound.sound == GM_SOUND_SHULKER_HURT
              && gm_runtime_sound_event_get(&r, 2, &sound)
              && sound.sound == GM_SOUND_SHULKER_DEATH,
          "closed hurt, open hurt, and death sounds preserve order");
    gm_runtime_destroy(&r);
    return 1;
}

static int test_guided_hit(void) {
    GmRuntime r;
    GmRuntimeParticleEvent particle;
    int hit_tick = 0;
    CHECK(init_runtime(&r), "initialize bullet runtime");
    gm_runtime_set_pose(&r, 6.5, 66.0, 4.5, 0.0F, 0.0F);
    CHECK(gm_runtime_spawn_shulker_fixture(&r, 201, 0, 64, 0, 0, 1),
          "spawn bullet owner");
    CHECK(gm_runtime_spawn_shulker_bullet_fixture(
              &r, 202, 201, UINT64_C(0x102030405060)),
          "spawn guided bullet");
    r.shulkers[0].active = 0;
    r.shulker_count = 0;
    for (int tick = 1; tick <= 160; ++tick) {
        gm_runtime_tick_shulkers_fixture(&r);
        if (gm_runtime_shulker_bullet_count(&r) == 0) {
            hit_tick = tick;
            break;
        }
    }
    CHECK(hit_tick > 0 && hit_tick < 160,
          "guided bullet reaches and removes itself on the player");
    CHECK(fabsf(r.vitals.health - 16.0F) < 0.0001F,
          "shulker bullet deals four points of indirect damage");
    CHECK(r.potion_count == 1 && r.potions[0].id == 25
              && r.potions[0].amplifier == 0
              && r.potions[0].duration == 200,
          "accepted bullet hit applies Levitation I for 200 ticks");
    CHECK(gm_runtime_particle_event_count(&r) > 0
              && gm_runtime_particle_event_get(&r, 0, &particle)
              && particle.kind == GM_PARTICLE_END_ROD,
          "guided bullet emits its client end-rod trail");
    gm_runtime_destroy(&r);
    return 1;
}

static int test_block_impact(void) {
    GmRuntime r;
    GmRuntimeSoundEvent sound;
    GmRuntimeParticleEvent particle;
    CHECK(init_runtime(&r), "initialize block-impact runtime");
    gm_runtime_set_pose(&r, 6.5, 66.0, 4.5, 0.0F, 0.0F);
    gm_world_set_block(r.world, 1, 64, 0, 201);
    CHECK(gm_runtime_spawn_shulker_fixture(&r, 211, 0, 64, 0, 0, 1)
              && gm_runtime_spawn_shulker_bullet_fixture(
                  &r, 212, 211, UINT64_C(0x102030405060)),
          "spawn block-impact bullet");
    r.shulker_bullets[0].direction = -1;
    r.shulker_bullets[0].target_dx = 0.15;
    r.shulker_bullets[0].target_dy = 0.0;
    r.shulker_bullets[0].target_dz = 0.0;
    r.shulkers[0].active = 0;
    r.shulker_count = 0;
    for (int tick = 0; tick < 80
            && gm_runtime_shulker_bullet_count(&r) > 0; ++tick)
        gm_runtime_tick_shulkers_fixture(&r);
    CHECK(gm_runtime_shulker_bullet_count(&r) == 0,
          "guided bullet is removed by a solid block");
    CHECK(gm_runtime_sound_event_count(&r) == 1
              && gm_runtime_sound_event_get(&r, 0, &sound)
              && sound.sound == GM_SOUND_SHULKER_BULLET_HIT
              && sound.volume == 1.0F && sound.pitch == 1.0F,
          "solid impact emits exact shulker-bullet hit sound");
    CHECK(gm_runtime_particle_event_count(&r) > 0
              && gm_runtime_particle_event_get(
                  &r, gm_runtime_particle_event_count(&r) - 1, &particle)
              && particle.kind == GM_PARTICLE_EXPLOSION_LARGE
              && particle.count == 2,
          "solid impact emits the two large-explosion particles");
    gm_runtime_destroy(&r);
    return 1;
}

static int test_server_attack(void) {
    GmRuntime r;
    GmRuntimeShulker shulker;
    GmAction idle, attack;
    CHECK(init_runtime(&r), "initialize player attack runtime");
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    attack = idle;
    attack.attack = 1;
    attack.do_break = 1;
    gm_world_set_block(r.world, 0, 63, -2, 201);
    gm_runtime_set_pose_state(
        &r, 0.5, 64.0, 0.5, 180.0F, 20.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    CHECK(gm_runtime_spawn_shulker_fixture(&r, 301, 0, 64, -2, 0, 1),
          "spawn ray-selected shulker");
    gm_runtime_tick(&r, attack);
    gm_runtime_tick(&r, idle);
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && shulker.health < 30.0F,
          "integrated CPacketUseEntity path damages the nearest shulker");
    gm_runtime_destroy(&r);
    return 1;
}

static int test_live_arrow_collision(void) {
    GmRuntime r;
    GmRuntimeShulker shulker;
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&r), "initialize arrow collision runtime");
    gm_runtime_set_pose(&r, 100.5, 64.0, 100.5, 0.0F, 0.0F);
    gm_world_set_block(r.world, 2, 63, 0, 201);
    CHECK(gm_runtime_spawn_shulker_fixture(
              &r, 401, 2, 64, 0, 0, UINT64_C(0x123456789ab)),
          "spawn arrow target shulker");
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &r, 402, 0.5, 64.5, 0.5,
              1.0, 0.0, 0.0, 90.0F, 0.0F,
              5, -1, 2.0, 0, 0, 1, 0, 0, 0,
              -1, -1, -1, 0, 0, 1, 0, 0.0),
          "spawn flying player arrow");
    for (int tick = 0; tick < 4 && r.projectiles[0].vx > 0.0; ++tick)
        gm_runtime_tick(&r, idle);
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && shulker.health == 30.0F,
          "live arrow collision cannot damage a closed shulker");
    CHECK(r.projectiles[0].active && r.projectiles[0].vx < 0.0
              && r.projectiles[0].age == 0,
          "closed shell reflects the rejected live arrow and resets its age");
    gm_runtime_destroy(&r);

    CHECK(init_runtime(&r), "initialize open-arrow runtime");
    gm_runtime_set_pose(&r, 100.5, 64.0, 100.5, 0.0F, 0.0F);
    gm_world_set_block(r.world, 2, 63, 0, 201);
    CHECK(gm_runtime_spawn_shulker_fixture(
              &r, 411, 2, 64, 0, 0, UINT64_C(0x123456789ab)),
          "spawn open arrow target");
    r.shulkers[0].peek_tick = 100;
    r.shulkers[0].peek_amount = 1.0F;
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &r, 412, 0.5, 64.5, 0.5,
              1.0, 0.0, 0.0, 90.0F, 0.0F,
              5, -1, 2.0, 0, 0, 1, 0, 0, 0,
              -1, -1, -1, 0, 0, 1, 0, 0.0),
          "spawn open-shell arrow");
    for (int tick = 0; tick < 4 && r.projectiles[0].active; ++tick)
        gm_runtime_tick(&r, idle);
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && shulker.health == 28.0F,
          "live arrow applies unarmored damage to an open shulker");
    CHECK(!r.projectiles[0].active,
          "accepted open-shell hit removes the arrow");
    gm_runtime_destroy(&r);
    return 1;
}

static int test_explosion_collision(void) {
    GmRuntime r;
    GmRuntimeShulker shulker;
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_runtime(&r), "initialize explosion collision runtime");
    gm_runtime_set_pose(&r, 100.5, 64.0, 100.5, 0.0F, 0.0F);
    gm_world_set_block(r.world, 3, 63, 0, 201);
    CHECK(gm_runtime_spawn_shulker_fixture(
              &r, 501, 3, 64, 0, 0, UINT64_C(0x123456789ab)),
          "spawn explosion target shulker");
    CHECK(gm_runtime_spawn_shulker_bullet_fixture(&r, 502, 501, 1),
          "spawn explosion target bullet");
    CHECK(gm_runtime_spawn_primed_tnt_fixture(
              &r, 503, 0.5, 64.0, 0.5, 0.0, 0.0, 0.0, 1),
          "spawn one-tick primed TNT");
    gm_runtime_tick(&r, idle);
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && shulker.health < 30.0F,
          "TNT explosion damages a represented shulker");
    CHECK(gm_runtime_shulker_bullet_count(&r) == 0,
          "TNT explosion attacks and removes a shulker bullet");
    gm_runtime_destroy(&r);
    return 1;
}

static int test_state_restore(void) {
    GmRuntime r;
    GmRuntimeShulker shulker;
    GmRuntimeShulkerBullet bullet;
    CHECK(init_runtime(&r), "initialize shulker state runtime");
    gm_runtime_set_pose(&r, 100.5, 64.0, 100.5, 0.0F, 0.0F);
    gm_world_set_block(r.world, 0, 63, 0, 201);
    CHECK(gm_runtime_spawn_shulker_state_fixture(
              &r, 601, 0, 64, 0, 0, 1, 30, 0, 0, 0, 0, 0,
              17, 42, 4, 8, 0, 27.5F, 3.0F, 0.2F, 0.25F,
              35.0F, -12.0F, UINT64_C(0x123456789abc)),
          "restore exact NoAI shulker state");
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && shulker.eid == 601 && shulker.no_ai
              && shulker.peek_tick == 30 && shulker.ticks_existed == 42
              && shulker.hurt_time == 4
              && shulker.hurt_resistant_time == 8
              && shulker.health == 27.5F && shulker.last_damage == 3.0F
              && shulker.prev_peek_amount == 0.2F
              && shulker.peek_amount == 0.25F
              && shulker.head_yaw == 35.0F
              && shulker.head_pitch == -12.0F
              && shulker.random_seed48 == UINT64_C(0x123456789abc),
          "NoAI shulker scalar state round-trips");
    CHECK(gm_runtime_spawn_shulker_bullet_state_fixture(
              &r, 602, 601, 2, 18, 7,
              3.5, 64.5, -2.5, 0.01, 0.02, -0.03,
              0.04, -0.05, 0.06, 170.0F, 12.0F,
              UINT64_C(0x23456789abcd)),
          "restore exact shulker bullet state");
    CHECK(gm_runtime_shulker_bullet_get(&r, 0, &bullet)
              && bullet.eid == 602 && bullet.owner_eid == 601
              && bullet.direction == 2 && bullet.steps == 18
              && bullet.ticks_existed == 7
              && bullet.x == 3.5 && bullet.y == 64.5 && bullet.z == -2.5
              && bullet.vx == 0.01 && bullet.vy == 0.02
              && bullet.vz == -0.03 && bullet.target_dx == 0.04
              && bullet.target_dy == -0.05 && bullet.target_dz == 0.06
              && bullet.yaw == 170.0F && bullet.pitch == 12.0F
              && bullet.random_seed48 == UINT64_C(0x23456789abcd),
          "shulker bullet scalar state round-trips");
    gm_runtime_tick_shulkers_fixture(&r);
    CHECK(gm_runtime_shulker_get(&r, 0, &shulker)
              && shulker.ticks_existed == 43
              && shulker.peek_time == 0 && shulker.attack_time == 0
              && shulker.watch_time == 0 && shulker.idle_look_time == 0,
          "NoAI state suppresses task scheduler while base update advances");
    gm_runtime_destroy(&r);
    return 1;
}

int main(void) {
    if (!test_damage_and_drop()
            || !test_guided_hit()
            || !test_block_impact()
            || !test_server_attack()
            || !test_live_arrow_collision()
            || !test_explosion_collision()
            || !test_state_restore())
        return 1;
    puts("shulker_runtime: PASS (armor, arrows, loot, bullet, levitation, "
         "attack, state restore)");
    return 0;
}
