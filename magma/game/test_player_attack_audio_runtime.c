#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        return 1; \
    } \
} while (0)

enum {
    CASE_SWEEP,
    CASE_MOVING_SWORD,
    CASE_NODAMAGE,
    CASE_DELTA,
    CASE_SPRINT,
    CASE_SPRINT_NODAMAGE
};

enum {
    TARGET_SEED48 = 123456789,
    NEIGHBOR_SEED48 = 987654321
};

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static int target_sound_exact(
        const GmRuntimeSoundEvent *event, int sound, int category, int eid,
        double x, double y, double z, float volume, uint32_t pitch_bits) {
    return event->sound == sound
        && event->category == category
        && event->eid == eid
        && event->x == x && event->y == y && event->z == z
        && event->volume == volume
        && float_bits(event->pitch) == pitch_bits;
}

static int player_sound_exact(
        const GmRuntimeSoundEvent *event, int sound,
        double x, double y, double z) {
    return event->sound == sound
        && event->category == GM_SOUND_CATEGORY_PLAYERS
        && event->x == x && event->y == y && event->z == z
        && event->volume == 1.0F && event->pitch == 1.0F;
}

static int run_case(int kind) {
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle, attack;
    GmRuntimeSoundEvent events[3];
    char err[256];

    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    attack = idle;
    attack.attack = 1;
    attack.do_break = 1;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err), err);

    double base = (double)gm_world_surface_y(runtime.world, 8, 8);
    gm_runtime_set_pose_state(
        &runtime, 8.5, base, 8.5, 180.0F, 0.0F,
        0.0, -0.0784000015258789, 0.0, 1, 0.0F);
    if (kind == CASE_SWEEP || kind == CASE_MOVING_SWORD)
        isr_set_stack(&runtime.player.inv, 0, ic_mk(276, 1, 0));

    double target_y = base + PSV_EYE_HEIGHT - 0.45;
    int primary = gm_mobs_spawn_exact(
        &runtime.mobs, GM_MOB_PIG, 7001,
        8.5, target_y, 6.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
    int neighbor = -1;
    if (kind == CASE_SWEEP || kind == CASE_MOVING_SWORD) {
        neighbor = gm_mobs_spawn_exact(
            &runtime.mobs, GM_MOB_PIG, 7002,
            9.5, target_y, 6.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
        runtime.mobs.a.on_ground[neighbor] =
            runtime.mobs.b.on_ground[neighbor] = 1;
    }
    CHECK(primary > 0 && (neighbor > 0 || kind >= CASE_NODAMAGE),
          "attack targets initialize");
    CHECK(gm_mobs_set_entity_random_state(
              &runtime.mobs, 7001, TARGET_SEED48, 0, 0.0),
          "primary target RNG initializes");
    if (neighbor > 0)
        CHECK(gm_mobs_set_entity_random_state(
                  &runtime.mobs, 7002, NEIGHBOR_SEED48, 0, 0.0),
              "sweep-neighbor RNG initializes");
    if (kind == CASE_NODAMAGE || kind == CASE_SPRINT_NODAMAGE) {
        runtime.mobs.entity_hurt_resistant[primary] = 20;
        runtime.mobs.entity_last_damage[primary] = 2.0F;
    } else if (kind == CASE_DELTA) {
        runtime.mobs.entity_hurt_resistant[primary] = 20;
        runtime.mobs.entity_last_damage[primary] = 0.5F;
    }

    gm_runtime_tick(&runtime, attack);
    runtime.mobs.player_ticks_since_last_swing =
        kind == CASE_SWEEP || kind == CASE_MOVING_SWORD ? 12 : 5;
    runtime.player.ent.onGround = 1;
    runtime.player.fall_distance = 0.0F;
    runtime.player.movement_speed_multiplier = 1.0;
    if (kind == CASE_MOVING_SWORD) {
        runtime.server_prev_distance_walked_modified = 0.0F;
        runtime.server_distance_walked_modified = 0.2F;
    }
    if (kind == CASE_SPRINT || kind == CASE_SPRINT_NODAMAGE)
        runtime.player.sprinting = 1;
    gm_runtime_tick(&runtime, idle);

    int count = gm_runtime_sound_event_count(&runtime);
    int expected_count = kind == CASE_SWEEP ? 3
        : kind == CASE_MOVING_SWORD ? 2
        : kind == CASE_SPRINT ? 3
        : kind == CASE_SPRINT_NODAMAGE ? 2 : 1;
    CHECK(count == expected_count, "attack emits the expected sound rows");
    for (int i = 0; i < count; ++i)
        CHECK(gm_runtime_sound_event_get(&runtime, i, &events[i]),
              "attack sound is readable");
    if (kind == CASE_SWEEP) {
        const EwStore *store = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        CHECK(target_sound_exact(
                  &events[0], GM_SOUND_PIG_HURT,
                  GM_SOUND_CATEGORY_NEUTRAL, 7001,
                  8.5, target_y, 6.5, 1.0F, UINT32_C(0x3f82b1e2))
                  && target_sound_exact(
                      &events[1], GM_SOUND_PIG_HURT,
                      GM_SOUND_CATEGORY_NEUTRAL, 7002,
                      9.5, target_y, 6.5, 1.0F, UINT32_C(0x3f8516b8))
                  && player_sound_exact(
                      &events[2], GM_SOUND_PLAYER_ATTACK_SWEEP,
                      8.5, base, 8.5)
                  && store->health[neighbor] == 9.0F,
              "primary and neighbor hurt precede sweep sound");
    } else if (kind == CASE_MOVING_SWORD) {
        const EwStore *store = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        CHECK(target_sound_exact(
                  &events[0], GM_SOUND_PIG_HURT,
                  GM_SOUND_CATEGORY_NEUTRAL, 7001,
                  8.5, target_y, 6.5, 1.0F, UINT32_C(0x3f82b1e2))
                  && player_sound_exact(
                      &events[1], GM_SOUND_PLAYER_ATTACK_STRONG,
                      8.5, base, 8.5)
                  && store->health[neighbor] == 10.0F,
              "target hurt precedes moving-sword strong sound");
    } else if (kind == CASE_NODAMAGE) {
        CHECK(player_sound_exact(
                  &events[0], GM_SOUND_PLAYER_ATTACK_NODAMAGE,
                  8.5, base, 8.5),
              "rejected damage emits nodamage");
    } else if (kind == CASE_DELTA) {
        CHECK(player_sound_exact(
                  &events[0], GM_SOUND_PLAYER_ATTACK_STRONG,
                  8.5, base, 8.5),
              "accepted resistant-window delta does not replay hurt sound");
    } else if (kind == CASE_SPRINT) {
        CHECK(player_sound_exact(
                  &events[0], GM_SOUND_PLAYER_ATTACK_KNOCKBACK,
                  8.5, base, 8.5)
                  && target_sound_exact(
                      &events[1], GM_SOUND_PIG_HURT,
                      GM_SOUND_CATEGORY_NEUTRAL, 7001,
                      8.5, target_y, 6.5, 1.0F,
                      UINT32_C(0x3f82b1e2))
                  && player_sound_exact(
                      &events[2], GM_SOUND_PLAYER_ATTACK_STRONG,
                      8.5, base, 8.5),
              "sprint knockback precedes target hurt and strong sound");
    } else {
        CHECK(player_sound_exact(
                  &events[0], GM_SOUND_PLAYER_ATTACK_KNOCKBACK,
                  8.5, base, 8.5)
                  && player_sound_exact(
                      &events[1], GM_SOUND_PLAYER_ATTACK_NODAMAGE,
                      8.5, base, 8.5),
              "rejected sprint attack emits knockback before nodamage");
    }

    gm_runtime_destroy(&runtime);
    return 0;
}

static int run_living_case(
        int type, int size, float health, int target_sound,
        int category, float volume, int weak_attack) {
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle, attack;
    GmRuntimeSoundEvent target_event, player_event;
    float target_width, target_height;
    char err[256];

    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    attack = idle;
    attack.attack = 1;
    attack.do_break = 1;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err), err);

    double base = (double)gm_world_surface_y(runtime.world, 8, 8);
    ehs_size_scaled((u8)type, size, &target_width, &target_height);
    (void)target_width;
    double target_y = base + PSV_EYE_HEIGHT - (double)target_height * 0.5;
    gm_runtime_set_pose_state(
        &runtime, 8.5, base, 8.5, 180.0F, 0.0F,
        0.0, -0.0784000015258789, 0.0, 1, 0.0F);
    CHECK(gm_mobs_spawn_exact_sized(
              &runtime.mobs, type, 7101,
              8.5, target_y, 6.5, 0.0, 0.0, 0.0,
              0.0F, health, size, 1, 0, 0, 0) > 0,
          "living target initializes");
    CHECK(gm_mobs_set_entity_random_state(
              &runtime.mobs, 7101, TARGET_SEED48, 0, 0.0),
          "living target RNG initializes");

    gm_runtime_tick(&runtime, attack);
    runtime.mobs.player_ticks_since_last_swing = weak_attack ? 1 : 5;
    runtime.player.ent.onGround = weak_attack;
    runtime.player.fall_distance = weak_attack ? 0.0F : 1.0F;
    gm_runtime_tick(&runtime, idle);

    CHECK(gm_runtime_sound_event_count(&runtime) == 2
              && gm_runtime_sound_event_get(&runtime, 0, &target_event)
              && gm_runtime_sound_event_get(&runtime, 1, &player_event),
          "living hit emits target then player sound");
    uint32_t pitch_bits = type == GM_MOB_PIGMAN
        ? UINT32_C(0x3f6a3a5b) : UINT32_C(0x3f82b1e2);
    int exact = target_sound_exact(
        &target_event, target_sound, category, 7101,
        8.5, target_y, 6.5, volume, pitch_bits)
        && player_sound_exact(
            &player_event, weak_attack
                ? GM_SOUND_PLAYER_ATTACK_WEAK
                : GM_SOUND_PLAYER_ATTACK_CRIT,
            8.5, base, 8.5);
    if (!exact)
        fprintf(stderr, "target type=%d size=%d health=%g got="
                "sound:%d category:%d volume:%08x pitch:%08x player:%d\n",
            type, size, (double)health, target_event.sound,
            target_event.category, float_bits(target_event.volume),
            float_bits(target_event.pitch), player_event.sound);
    CHECK(exact,
          "living hurt/death scalar and attack ordering are exact");

    gm_runtime_destroy(&runtime);
    return 0;
}

typedef struct {
    int type, size;
    float health, volume;
    int hurt_sound, death_sound, category;
} LivingSoundFamily;

static const LivingSoundFamily LIVING_SOUND_FAMILIES[] = {
    {GM_MOB_PIG, 1, 10.0F, 1.0F,
        GM_SOUND_PIG_HURT, GM_SOUND_PIG_DEATH, GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_COW, 1, 10.0F, 0.4F,
        GM_SOUND_COW_HURT, GM_SOUND_COW_DEATH, GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_SHEEP, 1, 8.0F, 1.0F,
        GM_SOUND_SHEEP_HURT, GM_SOUND_SHEEP_DEATH, GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_CHICKEN, 1, 4.0F, 1.0F,
        GM_SOUND_CHICKEN_HURT, GM_SOUND_CHICKEN_DEATH,
        GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_ZOMBIE, 1, 20.0F, 1.0F,
        GM_SOUND_ZOMBIE_HURT, GM_SOUND_ZOMBIE_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_PIGMAN, 1, 20.0F, 1.0F,
        GM_SOUND_PIGMAN_HURT, GM_SOUND_PIGMAN_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_SKELETON, 1, 20.0F, 1.0F,
        GM_SOUND_SKELETON_HURT, GM_SOUND_SKELETON_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_WITHER_SKELETON, 1, 20.0F, 1.0F,
        GM_SOUND_WITHER_SKELETON_HURT, GM_SOUND_WITHER_SKELETON_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_CREEPER, 1, 20.0F, 1.0F,
        GM_SOUND_CREEPER_HURT, GM_SOUND_CREEPER_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_SPIDER, 1, 16.0F, 1.0F,
        GM_SOUND_SPIDER_HURT, GM_SOUND_SPIDER_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_CAVE_SPIDER, 1, 12.0F, 1.0F,
        GM_SOUND_SPIDER_HURT, GM_SOUND_SPIDER_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_ENDERMAN, 1, 40.0F, 1.0F,
        GM_SOUND_ENDERMAN_HURT, GM_SOUND_ENDERMAN_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_BLAZE, 1, 20.0F, 1.0F,
        GM_SOUND_BLAZE_HURT, GM_SOUND_BLAZE_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_GHAST, 1, 10.0F, 10.0F,
        GM_SOUND_GHAST_HURT, GM_SOUND_GHAST_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_SILVERFISH, 1, 8.0F, 1.0F,
        GM_SOUND_SILVERFISH_HURT, GM_SOUND_SILVERFISH_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_VILLAGER, 1, 20.0F, 1.0F,
        GM_SOUND_VILLAGER_HURT, GM_SOUND_VILLAGER_DEATH,
        GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_WITCH, 1, 26.0F, 1.0F,
        GM_SOUND_WITCH_HURT, GM_SOUND_WITCH_DEATH,
        GM_SOUND_CATEGORY_HOSTILE},
    {GM_MOB_SLIME, 1, 1.0F, 0.4F,
        GM_SOUND_SMALL_SLIME_HURT, GM_SOUND_SMALL_SLIME_DEATH,
        GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_SLIME, 2, 4.0F, 0.8F,
        GM_SOUND_SLIME_HURT, GM_SOUND_SLIME_DEATH,
        GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_SLIME, 4, 16.0F, 1.6F,
        GM_SOUND_SLIME_HURT, GM_SOUND_SLIME_DEATH,
        GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_MAGMA, 1, 1.0F, 0.4F,
        GM_SOUND_SMALL_MAGMA_HURT, GM_SOUND_SMALL_MAGMA_DEATH,
        GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_MAGMA, 2, 4.0F, 0.8F,
        GM_SOUND_MAGMA_HURT, GM_SOUND_MAGMA_DEATH,
        GM_SOUND_CATEGORY_NEUTRAL},
    {GM_MOB_MAGMA, 4, 16.0F, 1.6F,
        GM_SOUND_MAGMA_HURT, GM_SOUND_MAGMA_DEATH,
        GM_SOUND_CATEGORY_NEUTRAL},
};

int main(void) {
    CHECK(run_case(CASE_SWEEP) == 0, "sweep runtime case");
    CHECK(run_case(CASE_MOVING_SWORD) == 0, "moving sword runtime case");
    CHECK(run_case(CASE_NODAMAGE) == 0, "nodamage runtime case");
    CHECK(run_case(CASE_DELTA) == 0, "delta runtime case");
    CHECK(run_case(CASE_SPRINT) == 0, "sprint runtime case");
    CHECK(run_case(CASE_SPRINT_NODAMAGE) == 0,
          "rejected sprint runtime case");
    for (unsigned i = 0;
            i < sizeof LIVING_SOUND_FAMILIES / sizeof LIVING_SOUND_FAMILIES[0];
            ++i) {
        const LivingSoundFamily *family = &LIVING_SOUND_FAMILIES[i];
        int weak_hurt = (family->type == GM_MOB_SLIME
                || family->type == GM_MOB_MAGMA) && family->size == 1;
        CHECK(run_living_case(
                  family->type, family->size, family->health,
                  family->hurt_sound, family->category,
                  family->volume, weak_hurt) == 0,
              "living hurt runtime case");
        CHECK(run_living_case(
                  family->type, family->size,
                  family->type == GM_MOB_MAGMA && family->size == 4
                      ? 0.25F : 1.0F,
                  family->death_sound, family->category,
                  family->volume, 0) == 0,
              "living death runtime case");
    }
    puts("player attack audio runtime: PASS "
         "(52 hurt/death/rejected/order cases)");
    return 0;
}
