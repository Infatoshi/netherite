#include "game/runtime.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

typedef struct {
    int accepted;
    double target_vx, target_vy, target_vz;
    double player_vx, player_vy, player_vz;
    int player_sprinting;
} PlayerAttackMotion;

typedef struct {
    int result;
    float target_health;
    int target_fire_ticks;
} PlayerAttackFire;

static PlayerAttackFire player_attack_fire_after(
        const PsvPlayer *source, float target_health, int target_fire_ticks,
        int hurt_resistant, float last_damage) {
    PlayerAttackFire out;
    GmMobLive mobs;
    PsvPlayer player = *source;
    McSinTable sin_table;
    memset(&out, 0, sizeof out);
    mc_sin_table_init(&sin_table);
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn_exact(
        &mobs, GM_MOB_PIG, 1,
        player.ent.posX,
        player.ent.posY + PSV_EYE_HEIGHT - 0.45,
        player.ent.posZ - 2.0,
        0.0, 0.0, 0.0, 0.0F, target_health, 1,
        0, 0, hurt_resistant);
    if (slot < 0) return out;
    mobs.fire_ticks[slot] = target_fire_ticks;
    mobs.entity_last_damage[slot] = last_damage;
    mobs.player_ticks_since_last_swing = 5;
    out.result = gm_mobs_player_attack(
        &mobs, (const struct PsvPlayer *)&player, 0, 0,
        (const struct McSinTable *)&sin_table, NULL,
        0.0F, 1.0, 0, 0, 0, NULL, 0.0F, NULL);
    const EwStore *store = mobs.current ? &mobs.b : &mobs.a;
    out.target_health = store->health[slot];
    out.target_fire_ticks = mobs.fire_ticks[slot];
    return out;
}

static PlayerAttackMotion player_attack_motion_after(
        const PsvPlayer *source, int swing_ticks, int target_on_ground,
        double target_vx, double target_vy, double target_vz) {
    PlayerAttackMotion out;
    GmMobLive mobs;
    PsvPlayer player = *source;
    McSinTable sin_table;
    memset(&out, 0, sizeof out);
    mc_sin_table_init(&sin_table);
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn_exact(
        &mobs, GM_MOB_PIG, 1,
        player.ent.posX,
        player.ent.posY + PSV_EYE_HEIGHT - 0.45,
        player.ent.posZ - 2.0,
        target_vx, target_vy, target_vz, 0.0F, 10.0F, 1, 0, 0, 0);
    if (slot < 0) return out;
    mobs.a.on_ground[slot] = mobs.b.on_ground[slot] = target_on_ground;
    mobs.player_ticks_since_last_swing = swing_ticks;
    out.accepted = gm_mobs_player_attack(
        &mobs, (const struct PsvPlayer *)&player, 0, 0,
        (const struct McSinTable *)&sin_table, NULL,
        0.0F, 1.0, 0, 0, 0, NULL, 0.0F, NULL) == 2;
    const EwStore *store = mobs.current ? &mobs.b : &mobs.a;
    out.target_vx = store->vx[slot];
    out.target_vy = store->vy[slot];
    out.target_vz = store->vz[slot];
    out.player_vx = player.ent.motionX;
    out.player_vy = player.ent.motionY;
    out.player_vz = player.ent.motionZ;
    out.player_sprinting = player.sprinting;
    return out;
}

static float player_attack_health_after(
        const PsvPlayer *source, int target_type, float target_health,
        float attack_bonus, int swing_ticks,
        int on_ladder, int in_water, int riding, PsvPlayer *after) {
    GmMobLive mobs;
    GmEntityView entity;
    PsvPlayer player = *source;
    McSinTable sin_table;
    mc_sin_table_init(&sin_table);
    gm_mobs_init(&mobs, 0);
    double x = player.ent.posX;
    double y = player.ent.posY + PSV_EYE_HEIGHT - 0.45;
    double z = player.ent.posZ - 2.0;
    int passive = target_type == GM_MOB_PIG || target_type == GM_MOB_COW
        || target_type == GM_MOB_SHEEP || target_type == GM_MOB_CHICKEN;
    int spawned = passive
        ? gm_mobs_spawn_exact(
            &mobs, target_type, 1, x, y, z, 0.0, 0.0, 0.0, 0.0f,
            target_health, 1, 0, 0, 0)
        : gm_mobs_spawn(&mobs, target_type, x, y, z);
    if (spawned < 0) return -1.0f;
    mobs.player_ticks_since_last_swing = swing_ticks;
    int result = gm_mobs_player_attack(
            &mobs, (const struct PsvPlayer *)&player, 0, 0,
            (const struct McSinTable *)&sin_table, NULL,
            attack_bonus, 1.0f, on_ladder, in_water, riding, NULL,
            0.0F, NULL);
    if (result != 2) return -1.0f;
    if (after) *after = player;
    if (gm_mobs_fill_views(&mobs, &entity, 1) != 1) return -1.0f;
    return entity.health;
}

static float player_attack_health(
        const PsvPlayer *source, int target_type, float target_health,
        float attack_bonus, int swing_ticks,
        int on_ladder, int in_water, int riding) {
    return player_attack_health_after(
        source, target_type, target_health, attack_bonus, swing_ticks,
        on_ladder, in_water, riding, NULL);
}

int main(void)
{
    GmConfig cfg;
    GmRuntime r;
    GmPlayerView view;
    GmAction idle;
    char err[256];

    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "player-effect runtime initializes");
    if (fail) return 1;

    CHECK(gm_runtime_potion_add_flags(&r, 16, 1, 40, 1, 0)
              && gm_runtime_potion_add_flags(&r, 16, 0, 60, 0, 1)
              && r.potion_count == 1
              && r.potions[0].amplifier == 1
              && r.potions[0].duration == 40
              && !r.potions[0].ambient
              && !r.potions[0].hide_particles,
          "weaker non-ambient effect clears ambient and owns visibility");
    CHECK(gm_runtime_potion_add_flags(&r, 16, 2, 30, 1, 0)
              && r.potions[0].amplifier == 2
              && r.potions[0].duration == 30
              && !r.potions[0].ambient
              && r.potions[0].hide_particles,
          "stronger effect replaces strength/duration but preserves ambient");
    gm_runtime_potions_clear(&r);

    double base = (double)gm_world_surface_y(r.world, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, base, 8.5, 180.0f, 0.0f,
        0.0, -0.0784000015258789, 0.0, 1, 0.0f);
    CHECK(gm_runtime_potion_add(&r, 25, 0, 3)
              && r.player.levitation_amplifier == 0
              && r.server_player.levitation_amplifier == 0,
          "Levitation enters both player travel paths");

    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.y == base
              && r.server_player.ent.motionY == 0.009800000190734865,
          "Levitation replaces ground gravity with the exact rise impulse");

    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(fabs(view.y - (base + 0.009800000190734865)) < 1e-5
              && r.server_player.ent.motionY == 0.017483200489807137,
          "Levitation keeps exact motion across the local packet boundary");

    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.potion_count == 0
              && r.player.levitation_amplifier == -1
              && r.server_player.levitation_amplifier == -1,
          "expired levitation is removed before same-tick travel");

    gm_runtime_set_pose_state(
        &r, 8.5, base, 8.5, 180.0f, 0.0f,
        0.0, -0.0784000015258789, 0.0, 1, 0.0f);
    CHECK(gm_runtime_potion_add(&r, 8, 1, 3)
              && r.player.jump_boost_amplifier == 1
              && r.server_player.jump_boost_amplifier == 1,
          "Jump Boost enters both player travel paths");

    GmAction jump = idle;
    jump.jump = 1;
    gm_runtime_tick(&r, jump);
    gm_runtime_view(&r, &view);
    CHECK(fabs(view.y - (base + 0.61999998986721)) < 1e-5
              && r.player.ent.motionY == 0.5292000003695486,
          "Jump Boost II adds its exact client jump impulse");

    gm_runtime_tick(&r, idle);
    CHECK(r.server_player.ent.motionY == 0.5292000003695486,
          "Jump Boost II reaches integrated-server jump prediction");
    gm_runtime_potions_clear(&r);
    CHECK(r.player.jump_boost_amplifier == -1
              && r.server_player.jump_boost_amplifier == -1,
          "clearing Jump Boost removes it from both travel paths");

    gm_runtime_set_pose_state(
        &r, 8.5, base, 8.5, 180.0f, 0.0f,
        0.0, -0.02, 0.0, 1, 0.0f);
    gm_world_set_block(r.world, 8, (int)base, 8, 9);
    gm_world_set_block(r.world, 8, (int)base + 1, 8, 9);
    CHECK(gm_runtime_set_air(&r, 293)
              && gm_runtime_potion_add(&r, 13, 0, 3),
          "Water Breathing fixture initializes");
    for (int i = 0; i < 3; ++i)
        gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.air == 293 && view.potion_count == 0,
          "Water Breathing protects air through its expiry tick");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.air == 292,
          "air loss resumes one tick after Water Breathing expires");

    gm_runtime_potions_clear(&r);
    gm_world_set_block(r.world, 8, (int)base, 8, 0);
    gm_world_set_block(r.world, 8, (int)base + 1, 8, 0);
    gm_runtime_set_pose_state(
        &r, 8.5, base + 9.0, 8.5, 180.0f, 0.0f,
        0.0, 0.0, 0.0, 0, 0.0f);
    CHECK(gm_runtime_potion_add(&r, 8, 1, 40),
          "Jump Boost fall fixture initializes");
    float fall_health = 20.0f;
    for (int i = 0; i < 30 && fall_health == 20.0f; ++i) {
        gm_runtime_tick(&r, idle);
        gm_runtime_view(&r, &view);
        fall_health = view.health;
    }
    CHECK(fall_health == 16.0f,
          "Jump Boost II subtracts two points from nine-block fall damage");

    gm_runtime_potions_clear(&r);
    pv_set_health(&r.vitals, 20.0f);
    r.player.health = r.server_player.health = r.vitals.health;
    r.vitals.foodTimer = 0;
    CHECK(gm_runtime_set_fire(&r, 42)
              && gm_runtime_potion_add(&r, 12, 0, 3),
          "Fire Resistance fixture initializes");
    for (int i = 0; i < 3; ++i)
        gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.health == 20.0f && view.potion_count == 0,
          "Fire Resistance suppresses burn damage through its expiry tick");
    for (int i = 0; i < 20; ++i)
        gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.health == 19.0f && view.hurt_time == 9,
          "burn damage resumes at the next 20-tick boundary");

    gm_runtime_potions_clear(&r);
    r.vitals.exhaustion = 0.0f;
    CHECK(gm_runtime_potion_add(&r, 17, 1, 3),
          "Hunger II fixture initializes");
    for (int i = 0; i < 3; ++i)
        gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(fabsf(r.vitals.exhaustion - 0.03f) < 1e-7f
              && view.potion_count == 0,
          "Hunger II adds exhaustion through its expiry tick");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(fabsf(r.vitals.exhaustion - 0.03f) < 1e-7f,
          "Hunger exhaustion stops after expiry");

    gm_runtime_potions_clear(&r);
    gm_runtime_set_vitals(&r, r.vitals.health, 11);
    gm_runtime_set_food_stats(&r, 2.5F, 0.0F);
    CHECK(gm_runtime_potion_add(&r, 23, 2, 1)
              && gm_runtime_potion_add(&r, 26, 1, 2)
              && gm_runtime_potion_add(&r, 27, 0, 2)
              && r.player_luck == 1.0F,
          "Saturation III and opposing Luck modifiers initialize");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.food == 14.0F && r.vitals.saturation == 8.5F
              && view.potion_count == 2 && r.player_luck == 1.0F,
          "Saturation updates FoodStats and expires before Luck/Unluck");
    gm_runtime_potions_clear(&r);
    CHECK(r.player_luck == 0.0F,
          "clearing Luck and Unluck restores the base attribute");

    gm_runtime_potions_clear(&r);
    pv_set_health(&r.vitals, 20.0f);
    r.player.health = r.server_player.health = r.vitals.health;
    r.vitals.foodTimer = 0;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_hurt_time = 0;
    CHECK(gm_runtime_potion_add(&r, 19, 1, 12),
          "Poison II fixture initializes");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.health == 19.0f && view.hurt_time == 10
              && view.potion_count == 1
              && view.potions[0].duration == 11,
          "Poison II applies on its 12-tick cadence");
    for (int i = 0; i < 3; ++i)
        gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.health == 19.0f && view.hurt_time == 7
              && view.potions[0].duration == 8,
          "Poison II ages without off-cadence damage");

    gm_runtime_potions_clear(&r);
    pv_set_health(&r.vitals, 1.0f);
    r.player.health = r.server_player.health = r.vitals.health;
    r.vitals.foodTimer = 0;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_hurt_time = 0;
    CHECK(gm_runtime_potion_add(&r, 19, 1, 12),
          "Poison floor fixture initializes");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.health == 1.0f && view.hurt_time == 0,
          "Poison does not reduce health below one");

    gm_runtime_potions_clear(&r);
    pv_set_health(&r.vitals, 20.0f);
    r.player.health = r.server_player.health = r.vitals.health;
    r.vitals.foodTimer = 0;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_hurt_time = 0;
    CHECK(gm_runtime_set_fire(&r, 42)
              && gm_runtime_potion_add(&r, 10, 0, 52),
          "Regeneration fire-recovery fixture initializes");
    for (int i = 0; i < 3; ++i)
        gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.health == 20.0f && view.hurt_time == 9
              && r.vitals.foodTimer == 0
              && view.potion_count == 1
              && view.potions[0].duration == 49,
          "Regeneration heals the duration-50 scheduled burn in one tick");

    gm_runtime_potions_clear(&r);
    pv_set_health(&r.vitals, 20.0f);
    r.player.health = r.server_player.health = r.vitals.health;
    r.vitals.foodTimer = 0;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_hurt_time = 0;
    CHECK(gm_runtime_set_fire(&r, -20)
              && gm_runtime_potion_add(&r, 20, 1, 20),
          "Wither II fixture initializes");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.health == 19.0f && view.hurt_time == 10
              && view.potion_count == 1
              && view.potions[0].duration == 19,
          "Wither II applies on its 20-tick cadence");

    gm_runtime_potions_clear(&r);
    CHECK(gm_runtime_potion_add(&r, 3, 1, 180)
              && r.haste_amplifier == 1
              && r.fatigue_amplifier == -1
              && fabs(r.player_attack_speed_multiplier
                      - 1.2000000029802322) < 1e-12,
          "Haste II enters mining and attack-speed paths");
    gm_runtime_potions_clear(&r);
    CHECK(gm_runtime_potion_add(&r, 4, 0, 240)
              && r.haste_amplifier == -1
              && r.fatigue_amplifier == 0
              && fabs(r.player_attack_speed_multiplier
                      - 0.8999999985098839) < 1e-12,
          "Mining Fatigue I enters mining and attack-speed paths");
    gm_runtime_potions_clear(&r);
    CHECK(r.haste_amplifier == -1 && r.fatigue_amplifier == -1
              && r.player_attack_speed_multiplier == 1.0,
          "Clearing mining effects restores base attributes");

    pv_set_health(&r.vitals, 20.0f);
    r.player.health = r.server_player.health = r.vitals.health;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_hurt_time = 0;
    CHECK(gm_runtime_potion_add(&r, 11, 0, 10)
              && r.resistance_amplifier == 0
              && gm_mobs_attack_player(
                  &r.mobs, (struct PvStats *)&r.vitals,
                  &r.player.inv, 1.0f, 0),
          "Resistance I incoming hit is accepted");
    CHECK(fabs(r.vitals.health - 19.2f) < 1e-6
              && r.mobs.player_hurt_time == 10,
          "Resistance I reduces one incoming point to 0.8");
    gm_runtime_potions_clear(&r);
    CHECK(r.resistance_amplifier == -1
              && r.mobs.player_resistance_amplifier == -1,
          "Clearing Resistance restores base incoming damage");

    pv_set_health(&r.vitals, 20.0f);
    r.player.health = r.server_player.health = r.vitals.health;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_hurt_time = 0;
    r.mobs.player_last_damage = 0.0f;
    CHECK(gm_runtime_potion_add(&r, 22, 0, 3)
              && r.mobs.player_absorption == 4.0f,
          "Absorption I grants four gold-heart points");
    CHECK(gm_mobs_attack_player(
              &r.mobs, (struct PvStats *)&r.vitals,
              &r.player.inv, 1.0f, 0) == 1
              && r.vitals.health == 20.0f
              && r.mobs.player_absorption == 3.0f
              && r.mobs.player_hurt_time == 10,
          "Absorption consumes damage before health");
    gm_runtime_potions_clear(&r);
    CHECK(r.mobs.player_absorption == 0.0f,
          "Clearing Absorption removes the remaining gold hearts");

    pv_set_health(&r.vitals, 20.0f);
    r.player.health = r.server_player.health = r.vitals.health;
    r.vitals.foodLevel = 20;
    r.vitals.saturation = 5.0f;
    r.vitals.exhaustion = 0.0f;
    r.vitals.foodTimer = 0;
    CHECK(gm_runtime_potion_add(&r, 21, 1, 3),
          "Health Boost II fixture initializes");
    gm_runtime_view(&r, &view);
    CHECK(view.max_health == 28.0f && view.health == 20.0f,
          "Health Boost II adds eight points of maximum health");
    gm_runtime_tick(&r, idle);
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.max_health == 28.0f && view.health == 20.0f
              && r.vitals.foodTimer == 2,
          "natural regeneration sees the Health Boost capacity");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(view.potion_count == 0 && view.max_health == 20.0f
              && view.health == 20.0f && r.vitals.foodTimer == 0,
          "Health Boost expiry restores the cap before food update");

    {
        GmMobLive mobs;
        GmEntityView entity;
        PsvPlayer attacker = r.player;
        isr_init(&attacker.inv);
        attacker.yaw = 180.0f;
        attacker.pitch = 0.0f;
        attacker.ent.onGround = 0;
        attacker.fall_distance = 1.0f;
        attacker.sprinting = 0;
        attacker.blindness = 0;
        CHECK(player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 0, 0, 0)
                  == 8.5f,
              "full-cooldown falling melee applies the exact 1.5x critical");
        attacker.blindness = 1;
        CHECK(player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 0, 0, 0)
                  == 9.0f,
              "Blindness suppresses an otherwise valid critical");
        attacker.blindness = 0;
        CHECK(player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 1, 0, 0)
                  == 9.0f
              && player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 0, 1, 0)
                  == 9.0f
              && player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 0, 0, 1)
                  == 9.0f,
              "ladder, water, and riding each suppress critical damage");
        attacker.sprinting = 1;
        CHECK(player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 0, 0, 0)
                  == 9.0f,
              "sprinting suppresses critical damage");
        attacker.sprinting = 0;
        attacker.ent.onGround = 1;
        CHECK(player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 0, 0, 0)
                  == 9.0f,
              "on-ground state suppresses critical damage");
        attacker.ent.onGround = 0;
        attacker.fall_distance = 0.0f;
        CHECK(player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 0, 0, 0)
                  == 9.0f,
              "zero fall distance suppresses critical damage");
        attacker.fall_distance = 1.0f;
        CHECK(player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 4, 0, 0, 0)
                  == 9.152f,
              "0.9 cooldown boundary is not a critical hit");
        ICStack weapon = ic_mk(280, 1, 0);
        weapon.n_enchants = 1;
        weapon.enchants[0].id = 16;
        weapon.enchants[0].level = 5;
        isr_set_stack(&attacker.inv, 0, weapon);
        CHECK(player_attack_health(
                  &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5, 0, 0, 0)
                  == 5.5f,
              "Sharpness is added after rather than multiplied by critical damage");
        weapon.enchants[0].id = 17;
        weapon.enchants[0].level = 1;
        isr_set_stack(&attacker.inv, 0, weapon);
        CHECK(player_attack_health(
                  &attacker, EW_TYPE_ZOMBIE, 20.0f, 0.0f, 5, 0, 0, 0)
                  == 16.064f,
              "Smite and zombie armor preserve exact post-critical ordering");

        PsvPlayer after;
        weapon = ic_mk(276, 1, 0);
        isr_set_stack(&attacker.inv, 0, weapon);
        float weapon_health = player_attack_health_after(
                  &attacker, EW_TYPE_ZOMBIE, 20.0f, 0.0f, 12,
                  0, 0, 0, &after);
        if (float_bits(weapon_health) != 0x411ab022U
                || isr_get_stack(&after.inv, 0).meta != 1)
            fprintf(stderr, "sword critical: health=%a bits=%08x meta=%d\n",
                weapon_health, float_bits(weapon_health),
                isr_get_stack(&after.inv, 0).meta);
        CHECK(float_bits(weapon_health) == 0x411ab022U
                  && isr_get_stack(&after.inv, 0).meta == 1,
              "diamond-sword critical matches zombie armor and durability");
        weapon = ic_mk(279, 1, 0);
        isr_set_stack(&attacker.inv, 0, weapon);
        CHECK(float_bits(player_attack_health_after(
                  &attacker, EW_TYPE_ZOMBIE, 20.0f, 0.0f, 12,
                  0, 0, 0, &after)) == 0x4177617cU
                  && isr_get_stack(&after.inv, 0).meta == 2,
              "partial diamond-axe hit matches damage and two durability");
        static const struct {
            int item;
            uint32_t health_bits;
            int wear;
        } weapon_cases[] = {
            {256, 0x410d3e77U, 2}, {257, 0x410d9fd3U, 2},
            {258, 0x40f8495cU, 2}, {267, 0x40fbdcf0U, 1},
            {268, 0x410949a6U, 1}, {269, 0x4115947bU, 2},
            {270, 0x4116cfeaU, 2}, {271, 0x4105436cU, 2},
            {272, 0x41039c0fU, 1}, {273, 0x41116979U, 2},
            {274, 0x411237deU, 2}, {275, 0x40fb3fa7U, 2},
            {276, 0x40f081c3U, 1}, {277, 0x41091375U, 2},
            {278, 0x410907c8U, 2}, {279, 0x40f4f9dbU, 2},
            {283, 0x410949a6U, 1}, {284, 0x4115947bU, 2},
            {285, 0x4116cfeaU, 2}, {286, 0x4102d2f2U, 2},
            {290, 0x411bd4feU, 1}, {291, 0x4118ed91U, 1},
            {292, 0x41141687U, 1}, {293, 0x41080000U, 1},
            {294, 0x411bd4feU, 1},
        };
        for (size_t i = 0; i < sizeof weapon_cases / sizeof weapon_cases[0]; ++i) {
            weapon = ic_mk(weapon_cases[i].item, 1, 0);
            isr_set_stack(&attacker.inv, 0, weapon);
            weapon_health = player_attack_health_after(
                &attacker, GM_MOB_PIG, 10.0f, 0.0f, 5,
                0, 0, 0, &after);
            int wear = isr_get_stack(&after.inv, 0).meta;
            if (float_bits(weapon_health) != weapon_cases[i].health_bits
                    || wear != weapon_cases[i].wear)
                fprintf(stderr,
                    "weapon %d: health=%a bits=%08x wear=%d\n",
                    weapon_cases[i].item, weapon_health,
                    float_bits(weapon_health), wear);
            CHECK(float_bits(weapon_health) == weapon_cases[i].health_bits
                      && wear == weapon_cases[i].wear,
                  "weapon material matrix matches damage, cooldown, and wear");
        }

        PlayerAttackMotion motion;
        isr_init(&attacker.inv);
        attacker.ent.onGround = 1;
        attacker.fall_distance = 0.0F;
        attacker.sprinting = 0;
        attacker.ent.motionX = attacker.ent.motionY = attacker.ent.motionZ = 0.0;
        motion = player_attack_motion_after(&attacker, 5, 1, 0.0, 0.0, 0.0);
        CHECK(motion.accepted
                  && double_bits(motion.target_vx) == 0x0000000000000000ULL
                  && double_bits(motion.target_vy) == 0x3fd99999a0000000ULL
                  && double_bits(motion.target_vz) == 0xbfd99999a0000000ULL,
              "ordinary accepted hit applies exact grounded target knockback");

        attacker.sprinting = 1;
        attacker.ent.motionX = 1.0;
        attacker.ent.motionZ = -2.0;
        motion = player_attack_motion_after(
            &attacker, 5, 1, 0.2, 0.3, -0.4);
        CHECK(motion.accepted
                  && double_bits(motion.target_vx) == 0x3fa9999999999991ULL
                  && double_bits(motion.target_vy) == 0x3fd99999a0000000ULL
                  && double_bits(motion.target_vz) == 0xbfe999999b333333ULL
                  && double_bits(motion.player_vx) == 0x3fe3333333333333ULL
                  && double_bits(motion.player_vz) == 0xbff3333333333333ULL
                  && !motion.player_sprinting,
              "full-strength sprint hit adds exact impulse and slows attacker");

        attacker.sprinting = 0;
        ICStack knockback_weapon = ic_mk(280, 1, 0);
        knockback_weapon.n_enchants = 1;
        knockback_weapon.enchants[0].id = 19;
        knockback_weapon.enchants[0].level = 2;
        isr_set_stack(&attacker.inv, 0, knockback_weapon);
        motion = player_attack_motion_after(
            &attacker, 5, 1, 0.2, 0.3, -0.4);
        CHECK(motion.accepted
                  && double_bits(motion.target_vx) == 0x3fa9999999999988ULL
                  && double_bits(motion.target_vy) == 0x3fd99999a0000000ULL
                  && double_bits(motion.target_vz) == 0xbff4cccccd99999aULL
                  && double_bits(motion.player_vx) == 0x3fe3333333333333ULL
                  && double_bits(motion.player_vz) == 0xbff3333333333333ULL
                  && !motion.player_sprinting,
              "Knockback II applies exact second impulse and attacker slowdown");

        isr_init(&attacker.inv);
        attacker.sprinting = 1;
        attacker.ent.motionX = attacker.ent.motionZ = 0.0;
        motion = player_attack_motion_after(&attacker, 4, 1, 0.0, 0.0, 0.0);
        CHECK(motion.accepted
                  && double_bits(motion.target_vz) == 0xbfd99999a0000000ULL
                  && motion.player_sprinting,
              "partial-cooldown sprint keeps only primary target knockback");

        attacker.sprinting = 0;
        knockback_weapon.enchants[0].level = 1;
        isr_set_stack(&attacker.inv, 0, knockback_weapon);
        motion = player_attack_motion_after(&attacker, 4, 1, 0.0, 0.0, 0.0);
        CHECK(motion.accepted
                  && double_bits(motion.target_vx) == 0xbc91a62640000000ULL
                  && double_bits(motion.target_vy) == 0x3fd99999a0000000ULL
                  && double_bits(motion.target_vz) == 0xbfe6666668000000ULL,
              "Knockback I remains active below the sprint-strength boundary");

        PlayerAttackFire fire;
        isr_init(&attacker.inv);
        ICStack fire_weapon = ic_mk(280, 1, 0);
        fire_weapon.n_enchants = 1;
        fire_weapon.enchants[0].id = 20;
        fire_weapon.enchants[0].level = 1;
        isr_set_stack(&attacker.inv, 0, fire_weapon);
        fire = player_attack_fire_after(&attacker, 10.0F, 0, 0, 0.0F);
        CHECK(fire.result == 2 && fire.target_health == 9.0F
                  && fire.target_fire_ticks == 80,
              "Fire Aspect I preignites then commits four seconds");
        fire_weapon.enchants[0].level = 2;
        isr_set_stack(&attacker.inv, 0, fire_weapon);
        fire = player_attack_fire_after(&attacker, 10.0F, 0, 0, 0.0F);
        CHECK(fire.result == 2 && fire.target_fire_ticks == 160,
              "Fire Aspect II commits eight seconds");
        fire_weapon.enchants[0].level = 1;
        isr_set_stack(&attacker.inv, 0, fire_weapon);
        fire = player_attack_fire_after(&attacker, 10.0F, 120, 0, 0.0F);
        CHECK(fire.result == 2 && fire.target_fire_ticks == 120,
              "Fire Aspect cannot shorten an existing longer burn");
        fire = player_attack_fire_after(&attacker, 10.0F, 0, 20, 2.0F);
        CHECK(fire.result == 1 && fire.target_health == 10.0F
                  && fire.target_fire_ticks == 0,
              "rejected Fire Aspect hit rolls back its preignition");
        fire = player_attack_fire_after(&attacker, 10.0F, 120, 20, 2.0F);
        CHECK(fire.result == 1 && fire.target_fire_ticks == 120,
              "rejected hit preserves a target that was already burning");
        fire = player_attack_fire_after(&attacker, 1.0F, 0, 0, 0.0F);
        CHECK(fire.result == 2 && fire.target_health == 0.0F
                  && fire.target_fire_ticks == 80,
              "lethal Fire Aspect hit has fire active at the death boundary");

        GmLiveSim fire_drops;
        uint64_t fire_math_seed = UINT64_C(0x1234abcd330e);
        int fire_next_eid = 9000;
        GmMobDeathContext fire_death = {
            1, &fire_math_seed, &fire_next_eid
        };
        gm_live_init(&fire_drops, 0, 4);
        gm_mobs_init(&mobs, 0);
        int fire_pig = gm_mobs_spawn(
            &mobs, GM_MOB_PIG, attacker.ent.posX,
            attacker.ent.posY + PSV_EYE_HEIGHT - 0.45,
            attacker.ent.posZ - 2.0);
        CHECK(fire_pig > 0, "ordinary Fire Aspect pig initializes");
        if (fire_pig > 0) {
            mobs.a.health[fire_pig] = mobs.b.health[fire_pig] = 1.0F;
            mobs.player_ticks_since_last_swing = 5;
            CHECK(gm_mobs_player_attack(
                      &mobs, (const struct PsvPlayer *)&attacker, 0, 0,
                      (const struct McSinTable *)&r.sin_table, &fire_drops,
                      0.0F, 1.0, 0, 0, 0, &fire_death,
                      0.0F, NULL) == 2,
                  "ordinary lethal Fire Aspect hit is accepted");
            int cooked = 0, raw = 0;
            for (int i = 0; i < GM_LIVE_MAX; ++i) {
                cooked += fire_drops.ents[i].active
                    && fire_drops.ents[i].item == 320;
                raw += fire_drops.ents[i].active
                    && fire_drops.ents[i].item == 319;
            }
            CHECK(cooked == 1 && raw == 0,
                  "Fire Aspect preignition selects cooked lethal pig loot");
        }

        gm_mobs_init(&mobs, 0);
        CHECK(gm_mobs_spawn_exact(
                  &mobs, GM_MOB_PIG, 1,
                  r.player.ent.posX + r.ox,
                  r.player.ent.posY + PSV_EYE_HEIGHT - 0.45,
                  r.player.ent.posZ + r.oz - 2.0,
                  0.0, 0.0, 0.0, 0.0f, 10.0f, 1, 0, 0, 0) >= 0,
              "Strength melee target initializes");
        mobs.player_ticks_since_last_swing = 5;
        CHECK(gm_mobs_player_attack(
                  &mobs, (const struct PsvPlayer *)&r.player,
                  r.ox, r.oz,
                  (const struct McSinTable *)&r.sin_table, NULL,
                  3.0f, 1.0f, 0, 0, 0, NULL, 0.0F, NULL) == 2,
              "Strength I melee hit is accepted");
        CHECK(gm_mobs_fill_views(&mobs, &entity, 1) == 1
                  && entity.health == 6.0f,
              "Strength I raises empty-hand full-cooldown damage to four");

        gm_mobs_init(&mobs, 0);
        CHECK(gm_mobs_spawn_exact(
                  &mobs, GM_MOB_PIG, 1,
                  r.player.ent.posX + r.ox,
                  r.player.ent.posY + PSV_EYE_HEIGHT - 0.45,
                  r.player.ent.posZ + r.oz - 2.0,
                  0.0, 0.0, 0.0, 0.0f, 10.0f, 1, 0, 0, 0) >= 0,
              "Weakness melee target initializes");
        mobs.player_ticks_since_last_swing = 5;
        CHECK(gm_mobs_player_attack(
                  &mobs, (const struct PsvPlayer *)&r.player,
                  r.ox, r.oz,
                  (const struct McSinTable *)&r.sin_table, NULL,
                  -4.0f, 1.0f, 0, 0, 0, NULL, 0.0F, NULL) == 1,
              "Weakness I zero-damage hit is rejected");
        CHECK(gm_mobs_fill_views(&mobs, &entity, 1) == 1
                  && entity.health == 10.0f
                  && entity.hurt_time == 0,
              "Weakness I causes neither damage nor hurt state");

        PsvPlayer sweep_player;
        psv_player_init(&sweep_player);
        sweep_player.ent.posX = 8.5;
        sweep_player.ent.posY = 80.0;
        sweep_player.ent.posZ = 8.5;
        sweep_player.ent.box = psv_player_box(8.5, 80.0, 8.5);
        sweep_player.ent.onGround = 1;
        sweep_player.yaw = 180.0F;
        sweep_player.pitch = 0.0F;
        sweep_player.fall_distance = 0.0F;
        sweep_player.movement_speed_multiplier = 1.0;
        sweep_player.inv.current_item = 0;
        isr_set_stack(&sweep_player.inv, 0, ic_mk(276, 1, 0));

        gm_mobs_init(&mobs, 0);
        int sweep_primary = gm_mobs_spawn_exact(
            &mobs, GM_MOB_PIG, 1, 8.5, 81.17, 6.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        int sweep_neighbor = gm_mobs_spawn_exact(
            &mobs, GM_MOB_PIG, 2, 9.5, 81.17, 6.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        mobs.a.on_ground[sweep_neighbor] =
            mobs.b.on_ground[sweep_neighbor] = 1;
        mobs.player_ticks_since_last_swing = 12;
        GmPlayerAttackOutcome sweep_outcome;
        CHECK(sweep_primary > 0 && sweep_neighbor > 0
                  && gm_mobs_player_attack(
                      &mobs, (const struct PsvPlayer *)&sweep_player, 0, 0,
                      (const struct McSinTable *)&r.sin_table, NULL,
                      0.0F, 1.0, 0, 0, 0, NULL,
                      0.0F, &sweep_outcome) == 2,
              "full-cooldown grounded sword sweep is accepted");
        const EwStore *sweep_store = mobs.current ? &mobs.b : &mobs.a;
        CHECK(sweep_outcome.accepted && sweep_outcome.sweep
                  && !sweep_outcome.critical && !sweep_outcome.strong
                  && sweep_outcome.sweep_hits == 1
                  && sweep_store->health[sweep_primary] == 3.0000009536743164F
                  && sweep_store->health[sweep_neighbor] == 9.0F,
              "sweep outcome and primary/secondary damage match Java");

        gm_mobs_init(&mobs, 0);
        sweep_primary = gm_mobs_spawn_exact(
            &mobs, GM_MOB_PIG, 1, 8.5, 81.17, 6.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        sweep_neighbor = gm_mobs_spawn_exact(
            &mobs, GM_MOB_PIG, 2, 9.5, 81.17, 6.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        mobs.player_ticks_since_last_swing = 12;
        CHECK(gm_mobs_player_attack(
                  &mobs, (const struct PsvPlayer *)&sweep_player, 0, 0,
                  (const struct McSinTable *)&r.sin_table, NULL,
                  0.0F, 1.0, 0, 0, 0, NULL,
                  0.10000000149011612F, &sweep_outcome) == 2,
              "moving full-cooldown sword hit is accepted");
        sweep_store = mobs.current ? &mobs.b : &mobs.a;
        CHECK(sweep_outcome.accepted && !sweep_outcome.sweep
                  && sweep_outcome.strong && sweep_outcome.sweep_hits == 0
                  && sweep_store->health[sweep_neighbor] == 10.0F,
              "exact movement threshold suppresses sweep and selects strong");

        gm_mobs_init(&mobs, 0);
        sweep_primary = gm_mobs_spawn_exact(
            &mobs, GM_MOB_PIG, 1, 40.5, 81.17, -9.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        sweep_neighbor = gm_mobs_spawn_exact(
            &mobs, GM_MOB_PIG, 2, 41.5, 81.17, -9.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        mobs.player_ticks_since_last_swing = 12;
        CHECK(sweep_primary > 0 && sweep_neighbor > 0
                  && gm_mobs_player_attack(
                      &mobs, (const struct PsvPlayer *)&sweep_player,
                      32, -16,
                      (const struct McSinTable *)&r.sin_table, NULL,
                      0.0F, 1.0, 0, 0, 0, NULL,
                      0.0F, &sweep_outcome) == 2,
              "shifted-origin grounded sword sweep is accepted");
        sweep_store = mobs.current ? &mobs.b : &mobs.a;
        CHECK(sweep_outcome.sweep && sweep_outcome.sweep_hits == 1
                  && sweep_store->health[sweep_neighbor] == 9.0F,
              "shifted-origin sweep uses world coordinates for range");

        ICStack sweeping_weapon = ic_mk(276, 1, 0);
        sweeping_weapon.n_enchants = 1;
        sweeping_weapon.enchants[0].id = 22;
        sweeping_weapon.enchants[0].level = 3;
        isr_set_stack(&sweep_player.inv, 0, sweeping_weapon);
        gm_mobs_init(&mobs, 0);
        sweep_primary = gm_mobs_spawn_exact(
            &mobs, GM_MOB_PIG, 1, 8.5, 81.17, 6.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        sweep_neighbor = gm_mobs_spawn_exact(
            &mobs, GM_MOB_PIG, 2, 9.5, 81.17, 6.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        mobs.player_ticks_since_last_swing = 12;
        CHECK(sweep_primary > 0 && sweep_neighbor > 0
                  && gm_mobs_player_attack(
                      &mobs, (const struct PsvPlayer *)&sweep_player, 0, 0,
                      (const struct McSinTable *)&r.sin_table, NULL,
                      0.0F, 1.0, 0, 0, 0, NULL,
                      0.0F, &sweep_outcome) == 2,
              "Sweeping Edge III attack is accepted");
        sweep_store = mobs.current ? &mobs.b : &mobs.a;
        CHECK(sweep_outcome.sweep && sweep_outcome.sweep_hits == 1
                  && sweep_store->health[sweep_primary]
                      == 3.0000009536743164F
                  && sweep_store->health[sweep_neighbor]
                      == 3.7500009536743164F,
              "Sweeping Edge III secondary damage matches Java");
    }

    gm_runtime_potions_clear(&r);
    gm_runtime_set_pose_state(
        &r, 8.5, base, 8.5, 180.0f, 0.0f,
        0.0, -0.0784000015258789, 0.0, 1, 0.0f);
    r.player.sprinting = 0;
    r.player.sprint_toggle_timer = 0;
    r.player.prev_move_forward = 0.0f;
    r.player.prev_sneak = 0;
    r.vitals.foodLevel = 20;
    r.player.food = r.server_player.food = 20.0f;
    CHECK(gm_runtime_potion_add(&r, 15, 0, 3)
              && r.player.blindness && r.server_player.blindness,
          "Blindness enters both player movement paths");
    GmAction sprint = idle;
    sprint.forward = 1.0f;
    sprint.sprint = 1;
    gm_runtime_tick(&r, sprint);
    CHECK(!r.player.sprinting && r.player.sprint_toggle_timer == 0
              && r.player.blindness,
          "Blindness blocks ctrl-sprint and double-tap arming");
    gm_runtime_tick(&r, sprint);
    CHECK(!r.player.sprinting && r.player.blindness,
          "Blindness blocks sprint through its last represented duration");
    gm_runtime_tick(&r, sprint);
    CHECK(r.player.sprinting && !r.player.blindness
              && !r.server_player.blindness,
          "sprint can start in the same movement tick Blindness expires");

    gm_runtime_potions_clear(&r);
    r.player.sprinting = 1;
    r.player.prev_move_forward = 1.0f;
    CHECK(gm_runtime_potion_add(&r, 15, 0, 2),
          "active-sprint Blindness fixture initializes");
    gm_runtime_tick(&r, sprint);
    CHECK(r.player.sprinting && r.player.blindness,
          "Blindness does not forcibly stop an already active sprint");

    gm_runtime_potions_clear(&r);
    r.player_time_in_portal = 0.0F;
    r.player_prev_time_in_portal = 0.0F;
    r.player_in_portal = 0;
    CHECK(gm_runtime_potion_add(&r, 9, 0, 62),
          "Nausea client-ramp fixture initializes");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(r.potions[0].duration == 61
              && float_bits(view.portal) == float_bits(0.006666667F)
              && float_bits(view.portal_prev) == float_bits(0.0F),
          "post-age Nausea duration 61 advances the exact client float ramp");
    gm_runtime_tick(&r, idle);
    gm_runtime_view(&r, &view);
    CHECK(r.potions[0].duration == 60 && view.portal == 0.0F
              && float_bits(view.portal_prev) == float_bits(0.006666667F),
          "post-age Nausea duration 60 takes the client decay branch");
    r.player_time_in_portal = 0.25F;
    r.player_prev_time_in_portal = 0.2F;
    gm_runtime_potions_clear(&r);
    CHECK(r.player_time_in_portal == 0.0F
              && r.player_prev_time_in_portal == 0.0F,
          "explicit Nausea removal clears both client interpolation endpoints");
    CHECK(float_bits(gm_runtime_client_portal_step(0.0F, 1, 0))
                  == float_bits(0.0125F)
              && gm_runtime_client_portal_step(0.999F, 1, 0) == 1.0F
              && gm_runtime_client_portal_step(0.02F, 0, 60) == 0.0F,
          "physical portal ramp and clamp match EntityPlayerSP float branches");
    r.player_time_in_portal = 0.375F;
    r.player_prev_time_in_portal = 0.25F;
    r.player_in_portal = 1;
    GmAction parked = idle;
    parked.server_only = 1;
    gm_runtime_tick(&r, parked);
    CHECK(r.player_time_in_portal == 0.375F
              && r.player_prev_time_in_portal == 0.25F
              && r.player_in_portal == 1,
          "parked-client server tick freezes all client portal state");

    gm_runtime_destroy(&r);
    if (fail) return 1;
    puts("player_effects: PASS");
    return 0;
}
