/* CUDA twin of cpu/entity_random.c using the same live feature header. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/entity_blaze_fireball.h"
#include "../core/potion_throwable.h"

#define EBF_OUT 76

__device__ static u64 double_bits(double value) {
    return (u64)__double_as_longlong(value);
}

__global__ void run_entity_random(i64 seed, u64 *out) {
    if (threadIdx.x || blockIdx.x) return;
    JavaGaussianRandom blaze, fireball;
    int n = 0;
    ebf_entity_random_init(&blaze, seed);
    out[n++] = (u64)__float_as_uint(ebf_blaze_height_offset(&blaze));
    out[n++] = double_bits(jrand_gaussian_next(&blaze));
    out[n++] = double_bits(jrand_gaussian_next(&blaze));
    out[n++] = double_bits(jrand_gaussian_next(&blaze));

    ebf_entity_random_init(&fireball, seed);
    EbfVector aim = ebf_blaze_fireball_aim(
        &blaze, 2.25, -0.75, 4.5);
    out[n++] = double_bits(aim.x);
    out[n++] = double_bits(aim.y);
    out[n++] = double_bits(aim.z);
    EbfVector a = ebf_small_fireball_acceleration(
        &fireball, aim.x, aim.y, aim.z);
    out[n++] = double_bits(a.x);
    out[n++] = double_bits(a.y);
    out[n++] = double_bits(a.z);
    out[n++] = fireball.random.seed;
    out[n++] = (u64)fireball.have_next_next_gaussian;
    out[n++] = double_bits(fireball.next_next_gaussian);

    out[n++] = double_bits(ebf_blaze_fall_damping(0, -0.125));
    out[n++] = double_bits(ebf_blaze_fall_damping(1, -0.125));
    out[n++] = double_bits(ebf_blaze_height_impulse(
        -0.075, 9.6200000047683716, 6.5300000905990601, 0.5F));
    out[n++] = double_bits(ebf_blaze_height_impulse(
        -0.075, 6.0, 6.5300000905990601, 0.5F));

    JavaGaussianRandom throwable;
    ebf_entity_random_init(&throwable, seed);
    EbfVector heading = ebf_throwable_heading(
        &throwable, -0.3125, 0.625, 0.71875, 0.5F, 1.0F);
    out[n++] = double_bits(heading.x);
    out[n++] = double_bits(heading.y);
    out[n++] = double_bits(heading.z);
    out[n++] = throwable.random.seed;
    out[n++] = (u64)throwable.have_next_next_gaussian;
    out[n++] = double_bits(throwable.next_next_gaussian);

    PtAreaEffectCloud cloud;
    pt_cloud_init(&cloud);
    out[n++] = (u64)__float_as_uint(cloud.radius);
    for (int age = 1; age <= 9; ++age) (void)pt_cloud_tick(&cloud);
    out[n++] = (u64)cloud.age;
    out[n++] = (u64)__float_as_uint(cloud.radius);
    out[n++] = (u64)pt_cloud_tick(&cloud);
    out[n++] = (u64)__float_as_uint(cloud.radius);
    pt_cloud_apply(&cloud);
    out[n++] = (u64)cloud.next_application;
    out[n++] = (u64)__float_as_uint(cloud.radius);
    while (cloud.age < 30)
        if (pt_cloud_tick(&cloud) && pt_cloud_target_ready(&cloud))
            pt_cloud_apply(&cloud);
    out[n++] = (u64)cloud.age;
    out[n++] = (u64)cloud.next_application;
    out[n++] = (u64)__float_as_uint(cloud.radius);
    out[n++] = (u64)(i64)pt_instant_health_delta(6, 0, 1.0, 0);
    out[n++] = (u64)(i64)pt_instant_health_delta(7, 1, 1.0, 0);
    out[n++] = (u64)(i64)pt_instant_health_delta(6, 1, 0.5, 1);
    out[n++] = (u64)(i64)pt_instant_health_delta(7, 1, 0.25, 1);
    out[n++] = (u64)(i64)pt_instant_health_delta(6, 0, 0.04, 0);
    out[n++] = (u64)(i64)pt_instant_health_delta(7, 0, 0.125, 0);
    out[n++] = (u64)(i64)pt_splash_effect_duration(3600, 0.5);
    out[n++] = (u64)(i64)pt_splash_effect_duration(3600, 0.005);
    out[n++] = (u64)(i64)pt_effect_is_ready(10, 50, 0);
    out[n++] = (u64)(i64)pt_effect_is_ready(10, 49, 0);
    out[n++] = (u64)(i64)pt_effect_is_ready(19, 12, 1);
    out[n++] = (u64)(i64)pt_effect_is_ready(19, 11, 1);
    out[n++] = (u64)(i64)pt_effect_is_ready(20, 5, 3);
    out[n++] = (u64)(i64)pt_effect_is_ready(17, 7, 0);
    out[n++] = double_bits(pt_effect_movement_multiplier(1, 0));
    out[n++] = double_bits(pt_effect_movement_multiplier(1, 1));
    out[n++] = double_bits(pt_effect_movement_multiplier(2, 0));
    out[n++] = double_bits(pt_effect_movement_multiplier(2, 1));
    out[n++] = double_bits(pt_effect_movement_multiplier(1, 0)
        * pt_effect_movement_multiplier(2, 0));
    out[n++] = double_bits(pt_effect_attack_bonus(5, 0));
    out[n++] = double_bits(pt_effect_attack_bonus(5, 1));
    out[n++] = double_bits(pt_effect_attack_bonus(18, 0));
    out[n++] = double_bits(pt_effect_attack_bonus(5, 0)
        + pt_effect_attack_bonus(18, 0));
    out[n++] = (u64)__float_as_uint(pt_effect_jump_bonus(0));
    out[n++] = (u64)__float_as_uint(pt_effect_jump_bonus(1));
    out[n++] = (u64)__float_as_uint(
        pt_effect_resistance_damage(1.0F, 0));
    out[n++] = (u64)__float_as_uint(
        pt_effect_resistance_damage(6.0F, 1));
    out[n++] = (u64)__float_as_uint(
        pt_effect_resistance_damage(20.0F, 4));
    out[n++] = double_bits(pt_effect_levitation_motion(-0.08, 0));
    out[n++] = double_bits(pt_effect_levitation_motion(0.0, 2));
    out[n++] = (u64)__float_as_uint(pt_effect_health_boost(10.0F, 0));
    out[n++] = (u64)__float_as_uint(pt_effect_health_boost(10.0F, 2));
    {
        float absorption = 4.0F;
        out[n++] = (u64)__float_as_uint(
            pt_effect_absorb_damage(6.0F, &absorption));
        out[n++] = (u64)__float_as_uint(absorption);
        absorption = 8.0F;
        out[n++] = (u64)__float_as_uint(
            pt_effect_absorb_damage(5.0F, &absorption));
        out[n++] = (u64)__float_as_uint(absorption);
    }
    {
        int pulse = 0;
        out[n++] = (u64)(i64)pt_effect_air_step(47, 0, 0, &pulse);
        out[n++] = (u64)(i64)pt_effect_air_step(300, 1, 0, &pulse);
        out[n++] = (u64)(i64)pt_effect_air_step(-19, 1, 1, &pulse);
        out[n++] = (u64)(i64)pt_effect_air_step(-19, 1, 0, &pulse);
        out[n++] = (u64)(i64)pulse;
        out[n++] = (u64)(i64)pt_effect_air_step(0, 1, 0, &pulse);
        JavaGaussianRandom bubbles;
        ebf_entity_random_init(&bubbles, seed);
        for (int draw = 0; draw < 48; ++draw)
            (void)jrand_float(&bubbles.random);
        out[n++] = bubbles.random.seed;
    }
}

int main(int argc, char **argv) {
    i64 seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345LL;
    u64 *device, out[EBF_OUT];
    cudaMalloc(&device, sizeof out);
    run_entity_random<<<1, 1>>>(seed, device);
    cudaDeviceSynchronize();
    cudaMemcpy(out, device, sizeof out, cudaMemcpyDeviceToHost);
    cudaFree(device);
    printf("%08x\n", (unsigned)out[0]);
    for (int i = 1; i <= 9; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    printf("%012llx\n", (unsigned long long)out[10]);
    printf("%llu\n", (unsigned long long)out[11]);
    for (int i = 12; i <= 19; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    printf("%012llx\n", (unsigned long long)out[20]);
    printf("%llu\n", (unsigned long long)out[21]);
    printf("%016llx\n", (unsigned long long)out[22]);
    printf("%08x\n", (unsigned)out[23]);
    printf("%llu\n", (unsigned long long)out[24]);
    printf("%08x\n", (unsigned)out[25]);
    printf("%llu\n", (unsigned long long)out[26]);
    printf("%08x\n", (unsigned)out[27]);
    printf("%llu\n", (unsigned long long)out[28]);
    printf("%08x\n", (unsigned)out[29]);
    printf("%llu\n", (unsigned long long)out[30]);
    printf("%llu\n", (unsigned long long)out[31]);
    printf("%08x\n", (unsigned)out[32]);
    for (int i = 33; i < 47; ++i)
        printf("%lld\n", (long long)out[i]);
    for (int i = 47; i < 56; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    for (int i = 56; i < 61; ++i)
        printf("%08x\n", (unsigned)out[i]);
    for (int i = 61; i < 63; ++i)
        printf("%016llx\n", (unsigned long long)out[i]);
    for (int i = 63; i < 69; ++i)
        printf("%08x\n", (unsigned)out[i]);
    for (int i = 69; i < 75; ++i)
        printf("%lld\n", (long long)out[i]);
    printf("%012llx\n", (unsigned long long)out[75]);
    return 0;
}
