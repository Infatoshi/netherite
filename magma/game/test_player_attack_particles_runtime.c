#include "game/runtime.h"

#include <math.h>
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
    CASE_SWEEP_LETHAL,
    CASE_CRITICAL,
    CASE_MAGIC_CRITICAL,
    CASE_REJECTED
};

static uint64_t double_raw_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static int run_case(int kind) {
    GmConfig cfg;
    GmRuntime runtime;
    GmAction idle, attack;
    GmRuntimeParticleEvent first, second, third;
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
    if (kind == CASE_SWEEP || kind == CASE_SWEEP_LETHAL)
        isr_set_stack(&runtime.player.inv, 0, ic_mk(276, 1, 0));
    if (kind == CASE_MAGIC_CRITICAL) {
        ICStack enchanted = ic_mk(280, 1, 0);
        enchanted.n_enchants = 1;
        enchanted.enchants[0].id = 16;
        enchanted.enchants[0].level = 1;
        isr_set_stack(&runtime.player.inv, 0, enchanted);
    }

    int target = gm_mobs_spawn_exact(
        &runtime.mobs, GM_MOB_PIG, 7001,
        8.5, base + PSV_EYE_HEIGHT - 0.45, 6.5,
        0.0, 0.0, 0.0, 0.0F,
        kind == CASE_SWEEP_LETHAL ? 1.0F : 10.0F, 1, 0, 0, 0);
    CHECK(target > 0, "attack particle target initializes");
    if (kind == CASE_REJECTED) {
        runtime.mobs.entity_hurt_resistant[target] = 20;
        runtime.mobs.entity_last_damage[target] = 2.0F;
    }

    gm_runtime_tick(&runtime, attack);
    runtime.mobs.player_ticks_since_last_swing = 12;
    runtime.player.ent.onGround = kind == CASE_SWEEP
        || kind == CASE_SWEEP_LETHAL || kind == CASE_REJECTED;
    runtime.player.fall_distance = runtime.player.ent.onGround ? 0.0F : 1.0F;
    runtime.player.movement_speed_multiplier = 1.0;
    gm_runtime_tick(&runtime, idle);

    int count = gm_runtime_particle_event_count(&runtime);
    if (kind == CASE_SWEEP || kind == CASE_SWEEP_LETHAL) {
        CHECK(count == (kind == CASE_SWEEP_LETHAL ? 1 : 2),
              "sweep emits the expected sweep/damage packet count");
        CHECK(gm_runtime_particle_event_get(&runtime, 0, &first)
                  && (kind == CASE_SWEEP_LETHAL
                      || gm_runtime_particle_event_get(&runtime, 1, &second)),
              "sweep particle rows are readable");
        float yaw = runtime.server_player.yaw * 0.017453292F;
        double dx = (double)-mc_sin(&runtime.sin_table, yaw);
        double dz = (double)mc_cos(&runtime.sin_table, yaw);
        CHECK(double_raw_bits(dx) == UINT64_C(0xbca1a62640000000)
                  && double_raw_bits(dz) == UINT64_C(0xbff0000000000000),
              "native sweep offsets match real-Java raw doubles");
        CHECK(first.kind == GM_PARTICLE_SWEEP_ATTACK
                  && first.count == 0 && first.entity_eid == -1
                  && first.x == 8.5 + dx
                  && first.y == base + (double)1.8F * 0.5
                  && first.z == 8.5 + dz
                  && first.offset_x == dx && first.offset_y == 0.0
                  && first.offset_z == dz && first.speed == 0.0,
              "sweep descriptor preserves Java yaw offset and zero speed");
        if (kind == CASE_SWEEP_LETHAL) {
            CHECK((runtime.mobs.current ? runtime.mobs.b.health[target]
                                        : runtime.mobs.a.health[target])
                      == 0.0F,
                  "overkill fixture loses only one health");
        } else {
            double target_y = base + PSV_EYE_HEIGHT - 0.45;
            CHECK(second.kind == GM_PARTICLE_DAMAGE_INDICATOR
                      && second.count == 3
                      && second.x == 8.5
                      && second.y == target_y + (double)(0.9F * 0.5F)
                      && second.z == 6.5
                      && second.offset_x == 0.1
                      && second.offset_y == 0.0
                      && second.offset_z == 0.1
                      && second.speed == 0.2,
                  "seven health damage emits exact three-particle descriptor");
        }
    } else if (kind == CASE_CRITICAL) {
        CHECK(count == 1
                  && gm_runtime_particle_event_get(&runtime, 0, &first),
              "plain critical emits one attached emitter");
        CHECK(first.kind == GM_PARTICLE_CRIT && first.count == -1
                  && first.entity_eid == 7001
                  && first.x == 8.5
                  && first.y == base + PSV_EYE_HEIGHT - 0.45
                  && first.z == 6.5
                  && first.entity_width == 0.9F
                  && first.entity_height == 0.9F,
              "critical emitter preserves target feet and dimensions");
    } else if (kind == CASE_MAGIC_CRITICAL) {
        CHECK(count == 3
                  && gm_runtime_particle_event_get(&runtime, 0, &first)
                  && gm_runtime_particle_event_get(&runtime, 1, &second)
                  && gm_runtime_particle_event_get(&runtime, 2, &third),
              "enchanted critical emits three ordered particle rows");
        CHECK(first.kind == GM_PARTICLE_CRIT && first.count == -1
                  && second.kind == GM_PARTICLE_CRIT_MAGIC
                  && second.count == -1
                  && third.kind == GM_PARTICLE_DAMAGE_INDICATOR
                  && third.count == 1,
              "crit precedes magic crit and damage indicator");
    } else {
        CHECK(count == 0,
              "rejected damage emits no combat particles");
    }

    gm_runtime_destroy(&runtime);
    return 0;
}

int main(void) {
    CHECK(run_case(CASE_SWEEP) == 0, "sweep particle runtime case");
    CHECK(run_case(CASE_SWEEP_LETHAL) == 0,
          "lethal sweep particle runtime case");
    CHECK(run_case(CASE_CRITICAL) == 0, "critical particle runtime case");
    CHECK(run_case(CASE_MAGIC_CRITICAL) == 0,
          "magic critical particle runtime case");
    CHECK(run_case(CASE_REJECTED) == 0,
          "rejected particle runtime case");
    puts("player attack particle runtime: PASS (sweep/crit/magic/damage/order)");
    return 0;
}
