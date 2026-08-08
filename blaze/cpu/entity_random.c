/* Java-vs-C reference for entity-local Random, blaze float, and fireball spread. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/entity_blaze_fireball.h"
#include "../core/potion_throwable.h"

static void emit_double(double value) {
    u64 bits;
    memcpy(&bits, &value, sizeof bits);
    printf("%016llx\n", (unsigned long long)bits);
}

static void emit_float(float value) {
    u32 bits;
    memcpy(&bits, &value, sizeof bits);
    printf("%08x\n", (unsigned)bits);
}

int main(int argc, char **argv) {
    i64 seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345LL;
    JavaGaussianRandom blaze, fireball;
    ebf_entity_random_init(&blaze, seed);
    emit_float(ebf_blaze_height_offset(&blaze));
    emit_double(jrand_gaussian_next(&blaze));
    emit_double(jrand_gaussian_next(&blaze));
    emit_double(jrand_gaussian_next(&blaze));

    ebf_entity_random_init(&fireball, seed);
    EbfVector aim = ebf_blaze_fireball_aim(
        &blaze, 2.25, -0.75, 4.5);
    emit_double(aim.x);
    emit_double(aim.y);
    emit_double(aim.z);
    EbfVector a = ebf_small_fireball_acceleration(
        &fireball, aim.x, aim.y, aim.z);
    emit_double(a.x);
    emit_double(a.y);
    emit_double(a.z);
    printf("%012llx\n", (unsigned long long)fireball.random.seed);
    printf("%d\n", fireball.have_next_next_gaussian);
    emit_double(fireball.next_next_gaussian);

    emit_double(ebf_blaze_fall_damping(0, -0.125));
    emit_double(ebf_blaze_fall_damping(1, -0.125));
    emit_double(ebf_blaze_height_impulse(
        -0.075, 9.6200000047683716, 6.5300000905990601, 0.5F));
    emit_double(ebf_blaze_height_impulse(
        -0.075, 6.0, 6.5300000905990601, 0.5F));

    JavaGaussianRandom throwable;
    ebf_entity_random_init(&throwable, seed);
    EbfVector heading = ebf_throwable_heading(
        &throwable, -0.3125, 0.625, 0.71875, 0.5F, 1.0F);
    emit_double(heading.x);
    emit_double(heading.y);
    emit_double(heading.z);
    printf("%012llx\n", (unsigned long long)throwable.random.seed);
    printf("%d\n", throwable.have_next_next_gaussian);
    emit_double(throwable.next_next_gaussian);

    PtAreaEffectCloud cloud;
    pt_cloud_init(&cloud);
    emit_float(cloud.radius);
    for (int age = 1; age <= 9; ++age) (void)pt_cloud_tick(&cloud);
    printf("%d\n", cloud.age);
    emit_float(cloud.radius);
    printf("%d\n", pt_cloud_tick(&cloud));
    emit_float(cloud.radius);
    pt_cloud_apply(&cloud);
    printf("%d\n", cloud.next_application);
    emit_float(cloud.radius);
    while (cloud.age < 30)
        if (pt_cloud_tick(&cloud) && pt_cloud_target_ready(&cloud))
            pt_cloud_apply(&cloud);
    printf("%d\n", cloud.age);
    printf("%d\n", cloud.next_application);
    emit_float(cloud.radius);
    printf("%d\n", pt_instant_health_delta(6, 0, 1.0, 0));
    printf("%d\n", pt_instant_health_delta(7, 1, 1.0, 0));
    printf("%d\n", pt_instant_health_delta(6, 1, 0.5, 1));
    printf("%d\n", pt_instant_health_delta(7, 1, 0.25, 1));
    printf("%d\n", pt_instant_health_delta(6, 0, 0.04, 0));
    printf("%d\n", pt_instant_health_delta(7, 0, 0.125, 0));
    printf("%d\n", pt_splash_effect_duration(3600, 0.5));
    printf("%d\n", pt_splash_effect_duration(3600, 0.005));
    printf("%d\n", pt_effect_is_ready(10, 50, 0));
    printf("%d\n", pt_effect_is_ready(10, 49, 0));
    printf("%d\n", pt_effect_is_ready(19, 12, 1));
    printf("%d\n", pt_effect_is_ready(19, 11, 1));
    printf("%d\n", pt_effect_is_ready(20, 5, 3));
    printf("%d\n", pt_effect_is_ready(17, 7, 0));
    emit_double(pt_effect_movement_multiplier(1, 0));
    emit_double(pt_effect_movement_multiplier(1, 1));
    emit_double(pt_effect_movement_multiplier(2, 0));
    emit_double(pt_effect_movement_multiplier(2, 1));
    emit_double(pt_effect_movement_multiplier(1, 0)
        * pt_effect_movement_multiplier(2, 0));
    emit_double(pt_effect_attack_bonus(5, 0));
    emit_double(pt_effect_attack_bonus(5, 1));
    emit_double(pt_effect_attack_bonus(18, 0));
    emit_double(pt_effect_attack_bonus(5, 0)
        + pt_effect_attack_bonus(18, 0));
    emit_float(pt_effect_jump_bonus(0));
    emit_float(pt_effect_jump_bonus(1));
    emit_float(pt_effect_resistance_damage(1.0F, 0));
    emit_float(pt_effect_resistance_damage(6.0F, 1));
    emit_float(pt_effect_resistance_damage(20.0F, 4));
    emit_double(pt_effect_levitation_motion(-0.08, 0));
    emit_double(pt_effect_levitation_motion(0.0, 2));
    emit_float(pt_effect_health_boost(10.0F, 0));
    emit_float(pt_effect_health_boost(10.0F, 2));
    {
        float absorption = 4.0F;
        emit_float(pt_effect_absorb_damage(6.0F, &absorption));
        emit_float(absorption);
        absorption = 8.0F;
        emit_float(pt_effect_absorb_damage(5.0F, &absorption));
        emit_float(absorption);
    }
    {
        int pulse = 0;
        printf("%d\n", pt_effect_air_step(47, 0, 0, &pulse));
        printf("%d\n", pt_effect_air_step(300, 1, 0, &pulse));
        printf("%d\n", pt_effect_air_step(-19, 1, 1, &pulse));
        printf("%d\n", pt_effect_air_step(-19, 1, 0, &pulse));
        printf("%d\n", pulse);
        printf("%d\n", pt_effect_air_step(0, 1, 0, &pulse));
        JavaGaussianRandom bubbles;
        ebf_entity_random_init(&bubbles, seed);
        for (int draw = 0; draw < 48; ++draw)
            (void)jrand_float(&bubbles.random);
        printf("%012llx\n", (unsigned long long)bubbles.random.seed);
    }
    return 0;
}
