/* entity_blaze_fireball.h - vanilla 1.11.2 blaze float and fireball math.
 *
 * Entity owns a java.util.Random. Its constructor consumes two nextLong calls
 * for the UUID before EntityBlaze can redraw heightOffset or EntityFireball
 * can consume three Gaussian spread samples. This header keeps that order in
 * one CPU/CUDA-safe implementation shared by magma and its Java golden.
 */
#ifndef MC_ENTITY_BLAZE_FIREBALL_H
#define MC_ENTITY_BLAZE_FIREBALL_H

#include "mc_rng.h"

typedef struct {
    double x, y, z;
} EbfVector;

MC_HD static inline void ebf_entity_random_init(
        JavaGaussianRandom *random, i64 constructor_seed) {
    jrand_gaussian_set(random, constructor_seed);
    (void)jrand_long(&random->random);
    (void)jrand_long(&random->random);
}

MC_HD static inline float ebf_blaze_height_offset(
        JavaGaussianRandom *random) {
    return 0.5F + (float)jrand_gaussian_next(random) * 3.0F;
}

MC_HD static inline double ebf_blaze_fall_damping(
        int on_ground, double motion_y) {
    return !on_ground && motion_y < 0.0 ? motion_y * 0.6 : motion_y;
}

MC_HD static inline double ebf_blaze_height_impulse(
        double motion_y, double target_eye_y, double blaze_eye_y,
        float height_offset) {
    if (target_eye_y > blaze_eye_y + (double)height_offset) {
        const double lift = 0.30000001192092896;
        motion_y += (lift - motion_y) * lift;
    }
    return motion_y;
}

MC_HD static inline EbfVector ebf_blaze_fireball_aim(
        JavaGaussianRandom *blaze_random,
        double x, double y, double z) {
    EbfVector out;
    double distance_sq = x * x + y * y + z * z;
    float spread = (float)sqrt((double)(float)sqrt(distance_sq)) * 0.5F;
    out.x = x + jrand_gaussian_next(blaze_random) * (double)spread;
    out.y = y;
    out.z = z + jrand_gaussian_next(blaze_random) * (double)spread;
    return out;
}

MC_HD static inline EbfVector ebf_small_fireball_acceleration(
        JavaGaussianRandom *random, double x, double y, double z) {
    EbfVector out;
    x += jrand_gaussian_next(random) * 0.4;
    y += jrand_gaussian_next(random) * 0.4;
    z += jrand_gaussian_next(random) * 0.4;
    /* EntityFireball calls MathHelper.sqrt(double), whose result is float. */
    double length = (double)(float)sqrt(x * x + y * y + z * z);
    out.x = x / length * 0.1;
    out.y = y / length * 0.1;
    out.z = z / length * 0.1;
    return out;
}

/* EntityThrowable.setThrowableHeading. Callers supply the float-rounded
 * MathHelper sin/cos direction and add thrower motion afterwards. Potions,
 * snowballs, eggs, pearls, and XP bottles share this exact normalization and
 * three-Gaussian inaccuracy path. */
MC_HD static inline EbfVector ebf_throwable_heading(
        JavaGaussianRandom *random, double x, double y, double z,
        float velocity, float inaccuracy) {
    EbfVector out;
    float length = (float)sqrt(x * x + y * y + z * z);
    x /= (double)length;
    y /= (double)length;
    z /= (double)length;
    x += jrand_gaussian_next(random)
        * 0.007499999832361937 * (double)inaccuracy;
    y += jrand_gaussian_next(random)
        * 0.007499999832361937 * (double)inaccuracy;
    z += jrand_gaussian_next(random)
        * 0.007499999832361937 * (double)inaccuracy;
    out.x = x * (double)velocity;
    out.y = y * (double)velocity;
    out.z = z * (double)velocity;
    return out;
}

#endif /* MC_ENTITY_BLAZE_FIREBALL_H */
